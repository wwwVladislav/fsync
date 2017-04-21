#include "files.h"
#include "statuses.h"
#include <futils/md5.h>
#include <futils/static_allocator.h>
#include <futils/log.h>
#include <string.h>
#include <fcommon/limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <pthread.h>
#include <binn.h>

static char const TBL_FILE_INFO[] = "/file.info";
static char const TBL_FILE_ID[] = "/file.id";
static char const TBL_FILE_PATH_ID[] = "/file.path.id";
static char const TBL_FILE_STATUS[] = "/file.status";

static char const *fdb_tbl_name(fuuid_t const *uuid, char *buf, size_t size, char const *tbl)
{
    if (size < sizeof(fuuid_t) * 2 + strlen(tbl) + 1)
        return 0;
    char *ret = buf;
    fuuid2str(uuid, buf, size);
    size_t uuid_len = strlen(buf);
    buf += uuid_len;
    size -= uuid_len;
    strncpy(buf, tbl, size);
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


struct fdb_files_map
{
    volatile uint32_t   ref_counter;
    fdb_map_t           files_map;
    fdb_map_t           path_ids_map;
    fdb_map_t           ids_map;
};

fdb_files_map_t *fdb_files_ex(fdb_transaction_t *transaction, fuuid_t const *uuid, bool ids_generator)
{
    if (!transaction || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_files_map_t *files_map = malloc(sizeof(fdb_files_map_t));
    if (!files_map)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(files_map, 0, sizeof *files_map);

    files_map->ref_counter = 1;

    // id->file_info
    char files_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_FILE_INFO] = { 0 };
    fdb_tbl_name(uuid, files_tbl_name, sizeof files_tbl_name, TBL_FILE_INFO);

    if (!fdb_map_open(transaction, files_tbl_name, FDB_MAP_CREATE | FDB_MAP_INTEGERKEY, &files_map->files_map))
    {
        FS_ERR("Map wasn't created");
        fdb_transaction_abort(transaction);
        fdb_files_release(files_map);
        return 0;
    }

    // path->id
    char path_ids_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_FILE_PATH_ID] = { 0 };
    fdb_tbl_name(uuid, path_ids_tbl_name, sizeof path_ids_tbl_name, TBL_FILE_PATH_ID);

    if (!fdb_map_open(transaction, path_ids_tbl_name, FDB_MAP_CREATE, &files_map->path_ids_map))
    {
        FS_ERR("Map wasn't created");
        fdb_transaction_abort(transaction);
        fdb_files_release(files_map);
        return 0;
    }

    // ids
    if (ids_generator)
    {
        char ids_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_FILE_ID] = { 0 };
        fdb_tbl_name(uuid, ids_tbl_name, sizeof ids_tbl_name, TBL_FILE_ID);

        if (!fdb_ids_map_open(transaction, ids_tbl_name, &files_map->ids_map))
        {
            FS_ERR("Map wasn't created");
            fdb_transaction_abort(transaction);
            fdb_files_release(files_map);
            return 0;
        }
    }

    return files_map;
}

fdb_files_map_t *fdb_files(fdb_transaction_t *transaction, fuuid_t const *uuid)
{
    return fdb_files_ex(transaction, uuid, true);
}

fdb_files_map_t *fdb_files_retain(fdb_files_map_t *files_map)
{
    if (files_map)
        files_map->ref_counter++;
    else
        FS_ERR("Invalid files map");
    return files_map;
}

void fdb_files_release(fdb_files_map_t *files_map)
{
    if (files_map)
    {
        if (!files_map->ref_counter)
            FS_ERR("Invalid files map");
        else if (!--files_map->ref_counter)
        {
            fdb_map_close(&files_map->files_map);
            fdb_map_close(&files_map->ids_map);
            fdb_map_close(&files_map->path_ids_map);
            free(files_map);
        }
    }
    else
        FS_ERR("Invalid files map");
}

bool fdb_files_statuses(fdb_transaction_t *transaction, fuuid_t const *uuid, fdb_map_t *pmap)
{
    if (!transaction || !uuid || !pmap)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    char file_status_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_FILE_STATUS] = { 0 };
    fdb_tbl_name(uuid, file_status_tbl_name, sizeof file_status_tbl_name, TBL_FILE_STATUS);

    return fdb_statuses_map_open(transaction, file_status_tbl_name, pmap);
}

static char STR_PATH[] = "path";
static char STR_MTIME[] = "mtime";
static char STR_STIME[] = "stime";
static char STR_DIGEST[] = "digest";
static char STR_SIZE[] = "size";
static char STR_STATUS[] = "status";

static binn * fdb_file_info_marshal(ffile_info_t const *info)
{
    binn *obj = binn_object();
    if (!binn_object_set_str(obj, STR_PATH, (char *)info->path)
         || !binn_object_set_uint64(obj, STR_MTIME, (uint64_t)info->mod_time)
         || !binn_object_set_uint64(obj, STR_STIME, (uint64_t)info->sync_time)
         || !binn_object_set_blob(obj, STR_DIGEST, (void *)info->digest.data, sizeof info->digest.data)
         || !binn_object_set_uint64(obj, STR_SIZE, info->size)
         || !binn_object_set_uint32(obj, STR_STATUS, info->status))
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
    char const *path = binn_object_str(obj, STR_PATH);
    if (path) strncpy(info->path, path, sizeof info->path);
    info->mod_time = (time_t)binn_object_uint64(obj, STR_MTIME);
    info->sync_time = (time_t)binn_object_uint64(obj, STR_STIME);
    memcpy(info->digest.data, binn_object_blob(obj, STR_DIGEST, &digest_size), sizeof info->digest.data);
    info->size = binn_object_uint64(obj, STR_SIZE);
    info->status = binn_object_uint32(obj, STR_STATUS);
    binn_free(obj);
    return true;
}

// TODO: delete
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

bool fdb_file_add(fdb_files_map_t *files_map, fdb_transaction_t *transaction, ffile_info_t *info)
{
    if (!transaction || !info)
        return false;

    ffile_info_t old_info = { 0 };

    if (!fdb_file_get_by_path(files_map, transaction, info->path, strlen(info->path), &old_info))
    {
        if (info->id == FINVALID_ID
            && !fdb_id_generate(&files_map->ids_map, transaction, &info->id))
            return false;
    }
    else
        info->id = old_info.id;

    binn *binfo = fdb_file_info_marshal(info);
    if (!binfo)
        return false;

    fdb_data_t const file_id = { sizeof info->id, &info->id };
    fdb_data_t const file_info = { binn_size(binfo), binn_ptr(binfo) };
    fdb_data_t const file_path = { strlen(info->path), info->path };

    bool ret = fdb_map_put(&files_map->files_map, transaction, &file_id, &file_info)
                && fdb_map_put(&files_map->path_ids_map, transaction, &file_path, &file_id);

    binn_free(binfo);

    return ret;
}

bool fdb_file_add_unique(fdb_files_map_t *files_map, fdb_transaction_t *transaction, ffile_info_t *info)
{
    if (!transaction || !info || info->id != FINVALID_ID)
        return false;

    uint32_t id = FINVALID_ID;

    if (!fdb_file_id(files_map, transaction, info->path, strlen(info->path), &id))
    {
        if (!fdb_id_generate(&files_map->ids_map, transaction, &info->id))
            return false;

        binn *binfo = fdb_file_info_marshal(info);
        if (!binfo)
            return false;

        fdb_data_t const file_id = { sizeof info->id, &info->id };
        fdb_data_t const file_info = { binn_size(binfo), binn_ptr(binfo) };
        fdb_data_t const file_path = { strlen(info->path), info->path };

        bool ret = fdb_map_put(&files_map->files_map, transaction, &file_id, &file_info)
                    && fdb_map_put(&files_map->path_ids_map, transaction, &file_path, &file_id);

        binn_free(binfo);

        return ret;
    }

    return false;
}

bool fdb_file_del(fdb_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id)
{
    if (!transaction)
        return false;

    ffile_info_t info;
    if (fdb_file_get(files_map, transaction, id, &info))
    {
        fdb_data_t const file_id = { sizeof id, &id };
        fdb_data_t const file_path = { strlen(info.path), info.path };

        return fdb_map_del(&files_map->files_map, transaction, &file_id, 0)
                && fdb_map_del(&files_map->path_ids_map, transaction, &file_path, 0)
                && fdb_id_free(&files_map->ids_map, transaction, info.id);
    }
    return false;
}

bool fdb_file_get(fdb_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, ffile_info_t *info)
{
    if (!transaction || !info)
        return false;
    info->id = id;
    fdb_data_t const file_id = { sizeof id, &id };
    fdb_data_t file_info = { 0 };
    return fdb_map_get(&files_map->files_map, transaction, &file_id, &file_info)
            && fdb_file_info_unmarshal(info, file_info.data);
}

bool fdb_file_id(fdb_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, uint32_t *id)
{
    if (!transaction || !path || !id)
        return false;
    fdb_data_t const file_path = { size, (void*)path };
    fdb_data_t file_id = { 0 };
    if (fdb_map_get(&files_map->path_ids_map, transaction, &file_path, &file_id))
    {
        *id = *(uint32_t*)file_id.data;
        return true;
    }
    return false;
}

bool fdb_file_get_by_path(fdb_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, ffile_info_t *info)
{
    uint32_t id = FINVALID_ID;
    return fdb_file_id(files_map, transaction, path, size, &id)
            && fdb_file_get(files_map, transaction, id, info);
}

bool fdb_file_del_all(fuuid_t const *uuid)
{
    // TODO
    return true;
}

bool fdb_file_path(fdb_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, char *path, size_t size)
{
    if (!transaction || !path)
        return false;

    ffile_info_t info = { 0 };

    if (fdb_file_get(files_map, transaction, id, &info))
    {
        strncpy(path, info.path, size);
        return true;
    }

    return false;
}

struct fdb_files_iterator
{
    fdb_transaction_t       *transaction;
    fdb_files_map_t         *files_map;
    fdb_cursor_t             cursor;
};

struct fdb_files_diff_iterator
{
    fdb_transaction_t       *transaction;
    fdb_files_map_t         *files_map_1;
    fdb_files_map_t         *files_map_2;
    fdb_cursor_t             cursor;
};

fdb_files_iterator_t *fdb_files_iterator(fdb_files_map_t *files_map, fdb_transaction_t *transaction)
{
    if (!transaction)
        return 0;

    fdb_files_iterator_t *piterator = malloc(sizeof(fdb_files_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory sync files iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->transaction = transaction;
    piterator->files_map = fdb_files_retain(files_map);

    if (!fdb_cursor_open(&files_map->path_ids_map, transaction, &piterator->cursor))
    {
        fdb_files_iterator_free(piterator);
        return 0;
    }

    return piterator;
}

void fdb_files_iterator_free(fdb_files_iterator_t *piterator)
{
    if (piterator)
    {
        fdb_files_release(piterator->files_map);
        fdb_cursor_close(&piterator->cursor);
        free(piterator);
    }
}

bool fdb_files_iterator_first(fdb_files_iterator_t *piterator, ffile_info_t *info)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path = { 0 };
    fdb_data_t file_id = { 0 };

    return fdb_cursor_get(&piterator->cursor, &file_path, &file_id, FDB_FIRST)
            && fdb_file_get(piterator->files_map, piterator->transaction, *(uint32_t*)file_id.data, info);
}

bool fdb_files_iterator_next(fdb_files_iterator_t *piterator, ffile_info_t *info)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path = { 0 };
    fdb_data_t file_id = { 0 };

    return fdb_cursor_get(&piterator->cursor, &file_path, &file_id, FDB_NEXT)
            && fdb_file_get(piterator->files_map, piterator->transaction, *(uint32_t*)file_id.data, info);
}

fdb_files_diff_iterator_t *fdb_files_diff_iterator(fdb_files_map_t *map_1, fdb_files_map_t *map_2, fdb_transaction_t *transaction)
{
    if (!transaction || !map_1 || !map_2)
        return 0;

    fdb_files_diff_iterator_t *piterator = malloc(sizeof(fdb_files_diff_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory sync files iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->transaction = transaction;
    piterator->files_map_1 = fdb_files_retain(map_1);
    piterator->files_map_2 = fdb_files_retain(map_2);

    if (!fdb_cursor_open(&map_1->path_ids_map, transaction, &piterator->cursor))
    {
        fdb_files_diff_iterator_free(piterator);
        return 0;
    }

    return piterator;
}

void fdb_files_diff_iterator_free(fdb_files_diff_iterator_t *piterator)
{
    if (piterator)
    {
        fdb_files_release(piterator->files_map_1);
        fdb_files_release(piterator->files_map_2);
        fdb_cursor_close(&piterator->cursor);
        free(piterator);
    }
}

bool fdb_files_diff_iterator_first(fdb_files_diff_iterator_t *piterator, ffile_info_t *info, fdb_diff_kind_t *diff_kind)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path_1 = { 0 };
    fdb_data_t file_id_1 = { 0 };

    bool first = true;

    while (fdb_cursor_get(&piterator->cursor, &file_path_1, &file_id_1, first ? FDB_FIRST : FDB_NEXT))
    {
        uint32_t id_2 = FINVALID_ID;

        if (!fdb_file_id(piterator->files_map_2, piterator->transaction, (char const*)file_path_1.data, file_path_1.size, &id_2))
        {
            if (!fdb_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (diff_kind)
                *diff_kind = FDB_FILE_ABSENT;
            return true;
        }
        else
        {
            ffile_info_t info_2 = { 0 };

            if (!fdb_file_get(piterator->files_map_2, piterator->transaction, id_2, &info_2)
                || !fdb_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (memcmp(&info->digest, &info_2.digest, sizeof info->digest) != 0)
            {
                if (diff_kind)
                    *diff_kind = FDB_DIFF_CONTENT;
                return true;
            }
        }

        first = false;
    }

    return false;

}

bool fdb_files_diff_iterator_next(fdb_files_diff_iterator_t *piterator, ffile_info_t *info, fdb_diff_kind_t *diff_kind)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path_1 = { 0 };
    fdb_data_t file_id_1 = { 0 };

    while (fdb_cursor_get(&piterator->cursor, &file_path_1, &file_id_1, FDB_NEXT))
    {
        uint32_t id_2 = FINVALID_ID;

        if (!fdb_file_id(piterator->files_map_2, piterator->transaction, (char const*)file_path_1.data, file_path_1.size, &id_2))
        {
            if (!fdb_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (diff_kind)
                *diff_kind = FDB_FILE_ABSENT;
            return true;
        }
        else
        {
            ffile_info_t info_2 = { 0 };

            if (!fdb_file_get(piterator->files_map_2, piterator->transaction, id_2, &info_2)
                || !fdb_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (memcmp(&info->digest, &info_2.digest, sizeof info->digest) != 0)
            {
                if (diff_kind)
                    *diff_kind = FDB_DIFF_CONTENT;
                return true;
            }
        }
    }

    return false;
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
