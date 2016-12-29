#include "files.h"
#include <futils/md5.h>
#include <futils/static_allocator.h>
#include <futils/log.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

// TODO: Use SQLite DB

enum
{
    FDB_FILES_LIST_SIZE   = 10000   // Max number of files for sync for all nodes
};

typedef struct
{
    fuuid_t      uuid;
    ffile_info_t info;
} fdb_file_info_t;

typedef struct
{
    pthread_mutex_t      mutex;
    char                 fla_buf[FSTATIC_ALLOCATOR_MEM_NEED(FDB_FILES_LIST_SIZE, sizeof(fdb_file_info_t))]; // buffer for files list allocator
    fstatic_allocator_t *files_list_allocator;                                                              // files list allocator
    fdb_file_info_t     *files_list[FDB_FILES_LIST_SIZE];                                                   // files list
    size_t               files_list_size;                                                                   // files list size
    uint32_t             next_id;
} fdb_sync_files_t;

static fdb_sync_files_t sync;

#define fsdb_push_lock(mutex)                       \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fsdb_pop_lock() pthread_cleanup_pop(1);


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

bool fdb_sync_file_add(fuuid_t const *uuid, ffile_info_t const *info)
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
                file->info.id = sync.next_id++;
            sync.files_list[sync.files_list_size++] = file;
            qsort(sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);
            ret = true;
        }
        else
            FS_WARN("Unable to allocate memory for changed file name");
    }
    else
    {
        (*inf)->info.mod_time = info->mod_time;
        (*inf)->info.sync_time = info->sync_time;
        (*inf)->info.digest = info->digest;
        (*inf)->info.size = info->size;
        (*inf)->info.is_exist = info->is_exist;
        ret = true;
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_sync_file_del(fuuid_t const *uuid, char const *path)
{
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
}

bool fdb_sync_file_get(fuuid_t const *uuid, char const *path, ffile_info_t *info)
{
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
}

bool fdb_sync_file_del_all(fuuid_t const *uuid)
{
    // TODO
    return true;
}

bool fdb_sync_file_update(fuuid_t const *uuid, ffile_info_t const *info)
{
    fdb_init();

    fdb_file_info_t **inf = 0;

    fsdb_push_lock(sync.mutex);

    fdb_file_info_t const key = { *uuid, *info };
    fdb_file_info_t const *pkey = &key;
    inf = (fdb_file_info_t **)bsearch(&pkey, sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);

    if (inf)
    {
        (*inf)->info.mod_time = info->mod_time;
        (*inf)->info.sync_time = info->sync_time;
        (*inf)->info.digest = info->digest;
        (*inf)->info.size = info->size;
        (*inf)->info.is_exist = info->is_exist;
    }

    fsdb_pop_lock();

    return inf ? true : false;
}

bool fdb_sync_file_path(fuuid_t const *uuid, uint32_t id, char *path, size_t size)
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

struct fdb_syncfiles_iterator
{
    fdb_iterator_t type;
    fuuid_t        uuid0;
    fuuid_t        uuid1;
    size_t         idx;
};

fdb_sync_files_iterator_t *fdb_sync_files_iterator(fuuid_t const *uuid)
{
    fdb_sync_files_iterator_t *piterator = malloc(sizeof(fdb_sync_files_iterator_t));
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

fdb_sync_files_iterator_t *fdb_sync_files_iterator_diff(fuuid_t const *uuid0, fuuid_t const *uuid1)
{
    if (!uuid0 || !uuid1) return 0;

    fdb_sync_files_iterator_t *piterator = malloc(sizeof(fdb_sync_files_iterator_t));
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

void fdb_sync_files_iterator_free(fdb_sync_files_iterator_t *piterator)
{
    if (piterator)
        free(piterator);
}

bool fdb_sync_files_iterator_first(fdb_sync_files_iterator_t *piterator, ffile_info_t *info, fdb_diff_kind_t *diff_kind)
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

bool fdb_sync_files_iterator_next(fdb_sync_files_iterator_t *piterator, ffile_info_t *info, fdb_diff_kind_t *diff_kind)
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

bool fdb_sync_files_iterator_uuid(fdb_sync_files_iterator_t *piterator, ffile_info_t *uuid)
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