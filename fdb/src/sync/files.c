#include "files.h"
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
            sync.files_list[sync.files_list_size++] = file;
            qsort(sync.files_list, sync.files_list_size, sizeof(fdb_sync_files_t*), fscompare);
        }
        else
        {
            FS_WARN("Unable to allocate memory for changed file name");
            return false;
        }
    }
    else
        memcpy(*inf, info, sizeof *info);

    fsdb_pop_lock();

    return true;
}

bool fdb_sync_file_del(fuuid_t const *uuid, char const *path)
{
    fdb_init();

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
        return true;
    }

    fsdb_pop_lock();

    return false;
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
        memcpy(*inf, info, sizeof *info);

    fsdb_pop_lock();

    return inf ? true : false;
}

struct fdb_syncfiles_iterator
{
    fuuid_t uuid;
    size_t  idx;
};

fdb_sync_files_iterator_t *fdb_sync_files_iterator(fuuid_t const *uuid)
{
    if (!uuid) return 0;

    fdb_sync_files_iterator_t *piterator = malloc(sizeof(fdb_sync_files_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory sync files iterator");
        return 0;
    }

    memcpy(&piterator->uuid, uuid, sizeof *uuid);
    piterator->idx = 0;

    return piterator;
}

void fdb_sync_files_iterator_free(fdb_sync_files_iterator_t *piterator)
{
    if (piterator)
        free(piterator);
}

bool fdb_sync_files_iterator_first(fdb_sync_files_iterator_t *piterator, ffile_info_t *info)
{
    if (!piterator || !info)
        return false;

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    for(piterator->idx = 0; piterator->idx < sync.files_list_size; ++piterator->idx)
    {
        if (memcmp(&sync.files_list[piterator->idx]->uuid, &piterator->uuid, sizeof piterator->uuid) == 0)
        {
            memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
            ret = true;
            break;
        }
    }

    fsdb_pop_lock();

    return ret;
}

bool fdb_sync_files_iterator_next(fdb_sync_files_iterator_t *piterator, ffile_info_t *info)
{
    if (!piterator || !info)
        return false;

    if (piterator->idx >= sync.files_list_size)
        return false;

    bool ret = false;

    fsdb_push_lock(sync.mutex);

    for(piterator->idx++; piterator->idx < sync.files_list_size; ++piterator->idx)
    {
        if (memcmp(&sync.files_list[piterator->idx]->uuid, &piterator->uuid, sizeof piterator->uuid) == 0)
        {
            memcpy(info, &sync.files_list[piterator->idx]->info, sizeof *info);
            ret = true;
            break;
        }
    }

    fsdb_pop_lock();

    return ret;
}
