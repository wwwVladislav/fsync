#include "files.h"
#include <futils/md5.h>
#include <futils/static_allocator.h>
#include <futils/log.h>
#include <string.h>
#include <fcommon/limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <pthread.h>
#include <binn.h>

static char const TBL_FILES[] = "/files";
static char const TBL_IDS[] = "/ids";

static char const *fdb_files_tbl(fuuid_t const *uuid, char *buf, size_t size)
{
    if (size < sizeof(fuuid_t) * 2 + sizeof TBL_FILES)
        return 0;
    char *ret = buf;
    fuuid2str(uuid, buf, size);
    size_t uuid_len = strlen(buf);
    buf += uuid_len;
    size -= uuid_len;
    strncpy(buf, TBL_FILES, size);
    return ret;
}

static char const *fdb_ids_tbl(fuuid_t const *uuid, char *buf, size_t size)
{
    if (size < sizeof(fuuid_t) * 2 + sizeof TBL_IDS)
        return 0;
    char *ret = buf;
    fuuid2str(uuid, buf, size);
    size_t uuid_len = strlen(buf);
    buf += uuid_len;
    size -= uuid_len;
    strncpy(buf, TBL_IDS, size);
    return ret;
}

enum
{
    FDB_FILES_LIST_SIZE     = 10000,    // Max number of files for sync for all nodes
    FDB_MAX_REQUESTED_PARTS =   128,     // Max number of requested parts
    FDB_MAX_SYNC_FILES      =   64,      // Max number of synchronized files
};

typedef struct
{
    fuuid_t      uuid;
    ffile_info_t info;
} fdb_file_info_t;

typedef struct
{
    uint32_t    file_id;
    time_t      requested_time;
    uint32_t    part;
} fdb_requested_part_t;

typedef struct
{
    uint32_t    file_id;
    uint64_t    size;
    uint64_t    received_size;
    uint32_t    threshold_delta_time;
    uint32_t    requested_parts_threshold;
} fdb_sync_file_t;

typedef struct
{
    pthread_mutex_t      mutex;
    char                 fla_buf[FSTATIC_ALLOCATOR_MEM_NEED(FDB_FILES_LIST_SIZE, sizeof(fdb_file_info_t))]; // buffer for files list allocator
    fstatic_allocator_t *files_list_allocator;                                                              // files list allocator
    fdb_file_info_t     *files_list[FDB_FILES_LIST_SIZE];                                                   // files list
    size_t               files_list_size;                                                                   // files list size
    uint32_t             next_id;                                                                           // id generator
    fdb_sync_file_t      sync_files[FDB_MAX_SYNC_FILES];                                                    // synchronized files
    uint32_t             sync_files_size;                                                                   // number of synchronized files
    fdb_requested_part_t requested_parts[FDB_MAX_REQUESTED_PARTS];                                          // requested parts
    uint32_t             requested_parts_size;                                                              // number of requested parts
} fdb_sync_files_t;

static fdb_sync_files_t sync;

#define fsdb_push_lock(mutex)                       \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fsdb_pop_lock() pthread_cleanup_pop(1);


struct fdb_files_transaction
{
    fdb_t                 *pdb;
    fdb_transaction_t      files_transaction;
    fdb_map_t              files_map;
    fdb_ids_transaction_t *ids_transaction;
};

fdb_files_transaction_t *fdb_files_transaction_start(fdb_t *pdb, fuuid_t const *uuid)
{
    if (!pdb || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_files_transaction_t *transaction = malloc(sizeof(fdb_files_transaction_t));
    if (!transaction)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(transaction, 0, sizeof *transaction);
    transaction->pdb = fdb_retain(pdb);
    if (!fdb_transaction_start(pdb, &transaction->files_transaction))
    {
        FS_ERR("Transaction wasn't started");
        fdb_files_transaction_abort(transaction);
        return 0;
    }

    char files_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_FILES] = { 0 };
    fdb_files_tbl(uuid, files_tbl_name, sizeof files_tbl_name);

    if (!fdb_map_open(&transaction->files_transaction, files_tbl_name, FDB_MAP_CREATE, &transaction->files_map))
    {
        FS_ERR("Map wasn't created");
        fdb_files_transaction_abort(transaction);
        return 0;
    }

    char ids_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_IDS] = { 0 };
    fdb_ids_tbl(uuid, ids_tbl_name, sizeof ids_tbl_name);

    transaction->ids_transaction = fdb_ids_transaction_start(pdb, ids_tbl_name);
    if (!transaction->ids_transaction)
    {
        FS_ERR("Map wasn't created");
        fdb_files_transaction_abort(transaction);
        return 0;
    }

    return transaction;
}

void fdb_files_transaction_commit(fdb_files_transaction_t *transaction)
{
    if (transaction)
    {
        fdb_transaction_commit(&transaction->files_transaction);
        fdb_ids_transaction_commit(transaction->ids_transaction);
        fdb_map_close(&transaction->files_map);
        fdb_release(transaction->pdb);
        free(transaction);
    }
}

void fdb_files_transaction_abort(fdb_files_transaction_t *transaction)
{
    if (transaction)
    {
        fdb_transaction_abort(&transaction->files_transaction);
        fdb_ids_transaction_abort(transaction->ids_transaction);
        fdb_map_close(&transaction->files_map);
        fdb_release(transaction->pdb);
        free(transaction);
    }
}

static binn * fdb_file_info_marshal(ffile_info_t const *info)
{
    binn *obj = binn_object();
    if (!binn_object_set_uint32(obj, "id", info->id)
         || !binn_object_set_uint64(obj, "mtime", (uint64_t)info->mod_time)
         || !binn_object_set_uint64(obj, "stime", (uint64_t)info->sync_time)
         || !binn_object_set_blob(obj, "digest", (void *)info->digest.data, sizeof info->digest.data)
         || !binn_object_set_uint64(obj, "size", info->size)
         || !binn_object_set_uint32(obj, "status", info->status))
    {
        binn_free(obj);
        obj = 0;
    }
    return obj;
}

static bool fdb_file_info_unmarshal(ffile_info_t *info, void const *data)
{
    if (!info || !data)
        return false;
    int digest_size = 0;
    binn *obj = binn_open((void *)data);
    if (!obj)
        return false;
    info->id = binn_object_uint32(obj, "id");
    info->mod_time = (time_t)binn_object_uint64(obj, "mtime");
    info->sync_time = (time_t)binn_object_uint64(obj, "stime");
    memcpy(info->digest.data, binn_object_blob(obj, "digest", &digest_size), sizeof info->digest.data);
    info->size = binn_object_uint64(obj, "size");
    info->status = binn_object_uint32(obj, "status");
    binn_free(obj);
    return true;
}

// TODO: for windows paths must be compared in lower case
static int fscompare(const void *pa, const void *pb)
{
    fdb_file_info_t const *lhs = *(fdb_file_info_t const **)pa;
    fdb_file_info_t const *rhs = *(fdb_file_info_t const **)pb;

    int ret = memcmp(&lhs->uuid, &rhs->uuid, sizeof rhs->uuid);
    if (ret == 0)
        return strncmp(lhs->info.path, rhs->info.path, sizeof rhs->info.path);

    return ret;
}

static void fdb_init()
{
    if (sync.files_list_allocator)
        return;

    if (fstatic_allocator_create(sync.fla_buf, sizeof sync.fla_buf, sizeof(fdb_file_info_t), &sync.files_list_allocator) != FSUCCESS)
    {
        FS_ERR("The allocator for file paths isn't created");
        fstatic_allocator_delete(sync.files_list_allocator);
        return;
    }

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    sync.mutex = mutex_initializer;
}

bool fdb_file_add(fdb_files_transaction_t *transaction, ffile_info_t *info)
{
    if (!transaction || !info || info->id != FINVALID_ID)
        return false;

    fdb_file_del(transaction, info->path);

    if (!fdb_id_generate(transaction->ids_transaction, &info->id))
        return false;

    binn *binfo = fdb_file_info_marshal(info);
    if (!binfo)
        return false;

    fdb_data_t const key = { strlen(info->path), info->path };
    fdb_data_t const value = { binn_size(binfo), binn_ptr(binfo) };

    bool ret = fdb_map_put(&transaction->files_map, &transaction->files_transaction, &key, &value);

    binn_free(binfo);

    return ret;
}

bool fdb_file_add_unique(fuuid_t const *uuid, ffile_info_t *info)
{
    fdb_init();

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    fdb_file_info_t const key = { *uuid, *info };
    fdb_file_info_t const *pkey = &key;
    fdb_file_info_t **inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

    if (!inf)
    {
        fdb_file_info_t *file = (fdb_file_info_t *)fstatic_alloc(sync.files_list_allocator);

        if (file)
        {
            memcpy(&file->uuid, uuid, sizeof *uuid);
            memcpy(&file->info, info, sizeof *info);
            if (file->info.id == FINVALID_ID)
                info->id = file->info.id = sync.next_id++;
            sync.files_list[sync.files_list_size++] = file;
            qsort(sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);
            ret = true;
        }
        else
            FS_WARN("Unable to allocate memory for changed file name");
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_file_del(fdb_files_transaction_t *transaction, char const *path)
{
    return false;
#if 0
    fdb_init();

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    fdb_file_info_t key = { *uuid };
    strncpy(key.info.path, path, sizeof key.info.path);

    fdb_file_info_t const *pkey = &key;
    fdb_file_info_t **inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

    if (inf)
    {
        fstatic_free(sync.files_list_allocator, *inf);
        size_t idx = inf - sync.files_list;
        for(++idx; idx < sync.files_list_size; ++idx)
            sync.files_list[idx - 1] = sync.files_list[idx];
        sync.files_list[sync.files_list_size--] = 0;
        ret = true;
    }

    fsdb_pop_lock();

    return ret;
#endif
}

bool fdb_file_get(fdb_files_transaction_t *transaction, char const *path, ffile_info_t *info)
{
    return false;
/*
    fdb_init();

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    fdb_file_info_t key = { *uuid };
    strncpy(key.info.path, path, sizeof key.info.path);

    fdb_file_info_t const *pkey = &key;
    fdb_file_info_t **inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

    if (inf)
    {
        *info = (*inf)->info;
        ret = true;
    }

    fsdb_pop_lock();

    return ret;
*/
}

bool fdb_file_get_if_not_exist(fuuid_t const *uuid, ffile_info_t *info)
{
    fdb_init();

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    for(size_t i = 0; i < sync.files_list_size; ++i)
    {
        if (memcmp(&sync.files_list[i]->uuid, uuid, sizeof *uuid) == 0
            && !(sync.files_list[i]->info.status & FFILE_IS_EXIST))
        {
            memcpy(info, &sync.files_list[i]->info, sizeof *info);
            ret = true;
            break;
        }
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_file_del_all(fuuid_t const *uuid)
{
    // TODO
    return true;
}

bool fdb_file_update(fuuid_t const *uuid, ffile_info_t const *info)
{
    fdb_init();

    fdb_file_info_t **inf = 0;

    fsdb_push_lock(sync.mutex);

    fdb_file_info_t const key = { *uuid, *info };
    fdb_file_info_t const *pkey = &key;
    inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

    if (inf)
    {
        (*inf)->info.id = info->id;
        (*inf)->info.mod_time = info->mod_time;
        (*inf)->info.sync_time = info->sync_time;
        (*inf)->info.digest = info->digest;
        (*inf)->info.size = info->size;
        (*inf)->info.status = info->status;
    }

    fsdb_pop_lock();

    return inf ? true : false;
}

bool fdb_file_path(fuuid_t const *uuid, uint32_t id, char *path, size_t size)
{
    fdb_init();

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    for(size_t i = 0; i < sync.files_list_size; ++i)
    {
        if (memcmp(&sync.files_list[i]->uuid, uuid, sizeof *uuid) == 0 && sync.files_list[i]->info.id == id)
        {
            strncpy(path, sync.files_list[i]->info.path, size);
            ret = true;
            break;
        }
    }

    fsdb_pop_lock();

    return ret;
}

typedef enum
{
    FDB_ITERATE_ALL = 0,
    FDB_ITERATE_UUID,
    FDB_ITERATE_DIFF
} fdb_iterator_t;

struct fdb_files_iterator
{
    fdb_iterator_t type;
    fuuid_t        uuid0;
    fuuid_t        uuid1;
    size_t         idx;
};

fdb_files_iterator_t *fdb_files_iterator(fuuid_t const *uuid)
{
    fdb_files_iterator_t *piterator = malloc(sizeof(fdb_files_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory sync files iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    if (uuid)
    {
        piterator->type = FDB_ITERATE_UUID;
        memcpy(&piterator->uuid0, uuid, sizeof *uuid);
    }
    else
        piterator->type = FDB_ITERATE_ALL;

    piterator->idx = 0;

    return piterator;
}

fdb_files_iterator_t *fdb_files_iterator_diff(fuuid_t const *uuid0, fuuid_t const *uuid1)
{
    if (!uuid0 || !uuid1) return 0;

    fdb_files_iterator_t *piterator = malloc(sizeof(fdb_files_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory sync files iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->type = FDB_ITERATE_DIFF;
    memcpy(&piterator->uuid0, uuid0, sizeof *uuid0);
    memcpy(&piterator->uuid1, uuid1, sizeof *uuid1);
    piterator->idx = 0;

    return piterator;
}

void fdb_files_iterator_free(fdb_files_iterator_t *piterator)
{
    if (piterator)
        free(piterator);
}

bool fdb_files_iterator_first(fdb_files_iterator_t *piterator, ffile_info_t *info, fdb_diff_kind_t *diff_kind)
{
    if (!piterator || !info)
        return false;

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    switch(piterator->type)
    {
        case FDB_ITERATE_ALL:
        {
            piterator->idx = 0;
            if (piterator->idx < sync.files_list_size)
            {
                memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
                ret = true;
            }
            break;
        }

        case FDB_ITERATE_UUID:
        {
            for(piterator->idx = 0; piterator->idx < sync.files_list_size; ++piterator->idx)
            {
                if (memcmp(&sync.files_list[piterator->idx]->uuid, &piterator->uuid0, sizeof piterator->uuid0) == 0)
                {
                    memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
                    ret = true;
                    break;
                }
            }
            break;
        }

        case FDB_ITERATE_DIFF:
        {
            for(piterator->idx = 0; piterator->idx < sync.files_list_size; ++piterator->idx)
            {
                if (memcmp(&sync.files_list[piterator->idx]->uuid, &piterator->uuid0, sizeof piterator->uuid0) == 0)
                {
                    fdb_file_info_t key = { piterator->uuid1 };
                    strncpy(key.info.path, sync.files_list[piterator->idx]->info.path, sizeof key.info.path);

                    fdb_file_info_t const *pkey = &key;
                    fdb_file_info_t **inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

                    if (!inf)
                    {
                        memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
                        if (diff_kind)
                            *diff_kind = FDB_FILE_ABSENT;
                        ret = true;
                        break;
                    }
/*
                    else if (memcmp(&sync.files_list[piterator->idx]->info.digest, &(*inf)->info.digest, sizeof (*inf)->info.digest))
                    {
                        if (diff_kind)
                            *diff_kind = FDB_DIFF_CONTENT;
                        ret = true;
                        break;
                    }
*/
                }
            }

            break;
        }
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_files_iterator_next(fdb_files_iterator_t *piterator, ffile_info_t *info, fdb_diff_kind_t *diff_kind)
{
    if (!piterator || !info)
        return false;

    bool ret = false;

    if (piterator->idx >= sync.files_list_size)
        return false;

    fsdb_push_lock(sync.mutex);

    switch(piterator->type)
    {
        case FDB_ITERATE_ALL:
        {
            piterator->idx++;
            if (piterator->idx < sync.files_list_size)
            {
                memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
                ret = true;
            }
            break;
        }

        case FDB_ITERATE_UUID:
        {
            for(piterator->idx++; piterator->idx < sync.files_list_size; ++piterator->idx)
            {
                if (memcmp(&sync.files_list[piterator->idx]->uuid, &piterator->uuid0, sizeof piterator->uuid0) == 0)
                {
                    memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
                    ret = true;
                    break;
                }
            }
            break;
        }

        case FDB_ITERATE_DIFF:
        {
            for(piterator->idx++; piterator->idx < sync.files_list_size; ++piterator->idx)
            {
                if (memcmp(&sync.files_list[piterator->idx]->uuid, &piterator->uuid0, sizeof piterator->uuid0) == 0)
                {
                    fdb_file_info_t key = { piterator->uuid1 };
                    strncpy(key.info.path, sync.files_list[piterator->idx]->info.path, sizeof key.info.path);

                    fdb_file_info_t const *pkey = &key;
                    fdb_file_info_t **inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

                    if (!inf)
                    {
                        memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
                        if (diff_kind)
                            *diff_kind = FDB_FILE_ABSENT;
                        ret = true;
                        break;
                    }
                    else if (memcmp(&sync.files_list[piterator->idx]->info.digest, &(*inf)->info.digest, sizeof (*inf)->info.digest))
                    {
                        if (diff_kind)
                            *diff_kind = FDB_DIFF_CONTENT;
                        ret = true;
                        break;
                    }
                }
            }

            break;
        }
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_files_iterator_uuid(fdb_files_iterator_t *piterator, ffile_info_t *uuid)
{
    if (!piterator || !uuid)
        return false;

    if (piterator->idx >= sync.files_list_size)
        return false;

    memcpy(uuid, &sync.files_list[piterator->idx]->uuid, sizeof *uuid);

    return true;
}

uint32_t fdb_get_uuids(fuuid_t uuids[FMAX_CONNECTIONS_NUM])
{
    uint32_t n = 0;

    fsdb_push_lock(sync.mutex);

    for(size_t i = 0; i < sync.files_list_size; ++i)
    {
        size_t j = 0;
        for(; j < n; ++j)
        {
            if(memcmp(&uuids[j], &sync.files_list[i]->uuid, sizeof(fuuid_t)) == 0)
                break;
        }
        if (j == n)
            uuids[n++] = sync.files_list[i]->uuid;
    }

    fsdb_pop_lock();

    return n;
}

uint32_t fdb_get_uuids_where_file_is_exist(fuuid_t uuids[FMAX_CONNECTIONS_NUM], uint32_t ids[FMAX_CONNECTIONS_NUM], char const *path)
{
    fdb_init();

    uint32_t n = 0;

    fsdb_push_lock(sync.mutex);

    for(size_t i = 0; i < sync.files_list_size; ++i)
    {
        if (strncmp(path, sync.files_list[i]->info.path, sizeof sync.files_list[i]->info.path) == 0
            && (sync.files_list[i]->info.status & FFILE_IS_EXIST))
        {
            uuids[n] = sync.files_list[i]->uuid;
            ids[n] = sync.files_list[i]->info.id;
            ++n;
        }
    }

    fsdb_pop_lock();

    return n;
}

bool fdb_sync_start(uint32_t id, uint32_t threshold_delta_time, uint32_t requested_parts_threshold, uint64_t size)
{
    fdb_init();

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    for(uint32_t i = 0; i < sync.sync_files_size; ++i)
    {
        if (sync.sync_files[i].file_id == id)
        {
            ret = true;
            break;
        }
    }

    if (!ret && sync.sync_files_size < sizeof sync.sync_files / sizeof *sync.sync_files)
    {
        sync.sync_files[sync.sync_files_size].file_id = id;
        sync.sync_files[sync.sync_files_size].received_size = 0;
        sync.sync_files[sync.sync_files_size].size = size;
        sync.sync_files[sync.sync_files_size].threshold_delta_time = threshold_delta_time;
        sync.sync_files[sync.sync_files_size].requested_parts_threshold = requested_parts_threshold;
        sync.sync_files_size++;
        ret = true;
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_sync_next_part(uint32_t id, uint32_t *part, bool *completed)
{
    fdb_init();

    *completed = false;

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    fdb_sync_file_t *sync_file = 0;

    for(uint32_t i = 0; i < sync.sync_files_size; ++i)
    {
        if (sync.sync_files[i].file_id == id)
        {
            sync_file = &sync.sync_files[i];
            break;
        }
    }

    if (sync_file && sync_file->received_size < sync_file->size)
    {
        time_t const now = time(0);

        uint32_t requested_parts = 0;
        int expired_request = -1;
        int last_requested_part = -1;

        for(int i = 0; i < sync.requested_parts_size; ++i)
        {
            uint32_t const delta_time = now - sync.requested_parts[i].requested_time;
            if (delta_time < sync_file->threshold_delta_time)
            {
                requested_parts++;
                if (id == sync.requested_parts[i].file_id)
                    last_requested_part = i;
            }
            else if (id == sync.requested_parts[i].file_id && expired_request == -1)
                expired_request = i;
        }

        if (requested_parts < sync_file->requested_parts_threshold)
        {
            if (expired_request != -1)
            {
                sync.requested_parts[expired_request].requested_time = now;
                *part = sync.requested_parts[expired_request].part;
                ret = true;
            }
            else
            {
                uint32_t const next_part = last_requested_part != -1 ? sync.requested_parts[last_requested_part].part + 1 : (sync_file->received_size / FSYNC_BLOCK_SIZE);
                uint64_t const psize = (uint64_t)next_part * FSYNC_BLOCK_SIZE;
                if (psize < sync_file->size)
                {
                    sync.requested_parts[sync.requested_parts_size].file_id = id;
                    sync.requested_parts[sync.requested_parts_size].part = next_part;
                    sync.requested_parts[sync.requested_parts_size].requested_time = now;
                    sync.requested_parts_size++;
                    *part = next_part;
                    ret = true;
                }
            }
        }
    }
    else
        *completed = true;

    fsdb_pop_lock();

    return ret;
}

void fdb_sync_part_received(fuuid_t const *uuid, uint32_t id, uint32_t part)
{
    fdb_init();

    fsdb_push_lock(sync.mutex);

    fdb_sync_file_t *sync_file = 0;

    for(uint32_t i = 0; i < sync.sync_files_size; ++i)
    {
        if (sync.sync_files[i].file_id == id)
        {
            sync_file = &sync.sync_files[i];
            break;
        }
    }

    if (sync_file && sync.requested_parts_size)
    {
        int i = 0;
        int first_requested_part = -1;

        for(; i < sync.requested_parts_size; ++i)
        {
            if (sync.requested_parts[i].file_id == id)
            {
                if (first_requested_part == -1)
                    first_requested_part = i;
                if (sync.requested_parts[i].part == part)
                    break;
            }
        }

        if (first_requested_part == i)
        {
            uint32_t j = (uint32_t)first_requested_part + 1;
            for(; j < sync.requested_parts_size; ++j)
            {
                if (sync.requested_parts[j].file_id == id)
                    break;
            }

            if(j < sync.requested_parts_size)
                sync_file->received_size += (j - i) * FSYNC_BLOCK_SIZE;
            else
                sync_file->received_size += FSYNC_BLOCK_SIZE;
        }

        if (i < sync.requested_parts_size)
        {
            for(++i; i < sync.requested_parts_size; ++i)
                sync.requested_parts[i - 1] = sync.requested_parts[i];
            --sync.requested_parts_size;
        }

        if (sync_file->received_size >= sync_file->size)
        {
            uint32_t i = sync_file - sync.sync_files;
            for(++i; i < sync.sync_files_size; ++i)
                sync.sync_files[i - 1] = sync.sync_files[i];
            --sync.sync_files_size;

            for(uint32_t i = 0; i < sync.files_list_size; ++i)
            {
                if (memcmp(&sync.files_list[i]->uuid, uuid, sizeof *uuid) == 0
                    && sync.files_list[i]->info.id == id)
                {
                    sync.files_list[i]->info.status |= FFILE_IS_EXIST;
                    break;
                }
            }
        }
    }

    fsdb_pop_lock();
}
