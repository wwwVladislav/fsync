#include "search_engine.h"
#include <futils/fs.h>
#include <futils/log.h>
#include <fdb/sync/dirs.h>
#include <fdb/sync/files.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <ctype.h>
#endif

static struct timespec const F1_SEC = { 1, 0 };

struct search_engine
{
    volatile uint32_t   ref_counter;
    fmsgbus_t          *pmsgbus;
    fdb_t              *db;

    volatile bool       is_active;
    pthread_t           scan_thread;
    sem_t               sem;

    fuuid_t             uuid;
};

static bool fsearch_engine_get_first_scan_dir(fsearch_engine_t *pengine, fdir_info_t *dir_info, fdir_scan_status_t *scan_status)
{
    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pengine->db, &transaction))
    {
        fdb_dirs_scan_status_t *dir_scan_status = fdb_dirs_scan_status(&transaction);
        if(dir_scan_status)
        {
            if (fdb_dirs_scan_status_get(dir_scan_status, &transaction, scan_status))
            {
                fdb_dirs_t *dirs = fdb_dirs(&transaction);
                if (dirs)
                {
                    ret = fdb_dirs_get(dirs, &transaction, scan_status->id, dir_info);
                    fdb_dirs_release(dirs);
                } else FS_ERR("Dirs map wasn't opened");

            }
            fdb_dirs_scan_status_release(dir_scan_status);
        } else FS_ERR("Statuses map wasn't opened");

        fdb_transaction_abort(&transaction);
    }

    return ret;
}

static void fsearch_engine_del_scan_dir_info(fsearch_engine_t *pengine, fdir_scan_status_t const *scan_status)
{
    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pengine->db, &transaction))
    {
        fdb_dirs_scan_status_t *dir_scan_status = fdb_dirs_scan_status(&transaction);
        if(dir_scan_status)
        {
            if (fdb_dirs_scan_status_del(dir_scan_status, &transaction, scan_status))
                fdb_transaction_commit(&transaction);
            fdb_dirs_scan_status_release(dir_scan_status);
        }
        else FS_ERR("Statuses map wasn't opened");

        fdb_transaction_abort(&transaction);
    }
}

static void fsearch_engine_update_scan_dir_info(fsearch_engine_t *pengine, fdir_scan_status_t *scan_status, char const *dir)
{
    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pengine->db, &transaction))
    {
        fdb_dirs_scan_status_t *dir_scan_status = fdb_dirs_scan_status(&transaction);
        if(dir_scan_status)
        {
            fdir_scan_status_t new_scan_status = { scan_status->id };
            strncpy(new_scan_status.path, dir, sizeof new_scan_status.path);

            if (fdb_dirs_scan_status_update(dir_scan_status, &transaction, scan_status, &new_scan_status))
            {
                fdb_transaction_commit(&transaction);
                memcpy(scan_status->path, new_scan_status.path, sizeof new_scan_status.path);
            }
            else FS_ERR("Unable to delete the item from statuses map");
            fdb_dirs_scan_status_release(dir_scan_status);
        }
        else FS_ERR("Statuses map wasn't opened");

        fdb_transaction_abort(&transaction);
    }
}

static void fsearch_engine_add_file(fsearch_engine_t *pengine, char const *path, uint64_t file_size)
{
    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pengine->db, &transaction))
    {
        fdb_files_t *files = fdb_files(&transaction, &pengine->uuid);
        if (files)
        {
            ffile_info_t info;
            strncpy(info.path, path, sizeof info.path);
            info.size = file_size;

            if (fdb_files_add(files, &transaction, &info))
                fdb_transaction_commit(&transaction);
            else FS_ERR("Unable to store the file info");

            fdb_files_release(files);
        }
        fdb_transaction_abort(&transaction);
    }
}

static void fsearch_engine_scan_dir(fsearch_engine_t *pengine, fdir_info_t const *dir_info, fdir_scan_status_t *scan_status)
{
    fsiterator_t *it = fsdir_iterator(dir_info->path);
    if (it)
    {
        fsdir_iterator_seek(it, scan_status->path);

        for(dirent_t entry; pengine->is_active && fsdir_iterator_next(it, &entry);)
        {
            switch(entry.type)
            {
                case FS_REG:
                {
                    char path[FMAX_PATH];
                    size_t path_len = fsdir_iterator_path(it, &entry, path, sizeof path);
                    if (path_len <= sizeof path)
                    {
                        char full_path[FMAX_PATH];
                        fsdir_iterator_full_path(it, &entry, full_path, sizeof full_path);
                        uint64_t file_size = 0;
                        fsfile_size(full_path, &file_size);
                        fsearch_engine_add_file(pengine, path, file_size);
                    }
                    break;
                }

                case FS_DIR:
                {
                    char dir_path[FMAX_PATH];
                    size_t path_len = fsdir_iterator_directory(it, dir_path, sizeof dir_path);
                    if (path_len <= sizeof dir_path)
                        fsearch_engine_update_scan_dir_info(pengine, scan_status, dir_path);
                    break;
                }

                default:
                    break;
            }
        }
        fsdir_iterator_free(it);
    }
}

static void *fsearch_engine_dirs_scan_thread(void *param)
{
    fsearch_engine_t *pengine = (fsearch_engine_t*)param;
    pengine->is_active = true;

    while(pengine->is_active)
    {
        while(pengine->is_active && sem_wait(&pengine->sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!pengine->is_active)
            break;

        bool dir_is_valid;

        do
        {
            fdir_scan_status_t scan_status = { 0 };
            fdir_info_t dir_info = { 0 };

            dir_is_valid = fsearch_engine_get_first_scan_dir(pengine, &dir_info, &scan_status);

            if (!pengine->is_active || !dir_is_valid)
                break;

            fsearch_engine_scan_dir(pengine, &dir_info, &scan_status);

            if (pengine->is_active)
                fsearch_engine_del_scan_dir_info(pengine, &scan_status);
        }
        while (pengine->is_active && dir_is_valid);
    }   // while(is_active)

    return 0;
}

fsearch_engine_t *fsearch_engine(fmsgbus_t *pmsgbus, fdb_t *db, fuuid_t const *uuid)
{
    if (!pmsgbus || !db || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fsearch_engine_t *pengine = malloc(sizeof(fsearch_engine_t));
    if (!pengine)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(pengine, 0, sizeof *pengine);

    pengine->ref_counter = 1;
    pengine->pmsgbus = fmsgbus_retain(pmsgbus);
    pengine->db = fdb_retain(db);
    pengine->uuid = *uuid;

    if (sem_init(&pengine->sem, 0, 1) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fsearch_engine_release(pengine);
        return 0;
    }

    int rc = pthread_create(&pengine->scan_thread, 0, fsearch_engine_dirs_scan_thread, (void*)pengine);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directories scan. Error: %d", rc);
        fsearch_engine_release(pengine);
        return 0;
    }

    while(!pengine->is_active)
        nanosleep(&F1_SEC, NULL);

    return pengine;
}

fsearch_engine_t *fsearch_engine_retain(fsearch_engine_t *pengine)
{
    if (pengine)
        pengine->ref_counter++;
    else
        FS_ERR("Invalid search engine");
    return pengine;
}

void fsearch_engine_release(fsearch_engine_t *pengine)
{
    if (pengine)
    {
        if (!pengine->ref_counter)
            FS_ERR("Invalid search engine");
        else if (!--pengine->ref_counter)
        {
            if (pengine->is_active)
            {
                pengine->is_active = false;
                sem_post(&pengine->sem);
                pthread_join(pengine->scan_thread, 0);
            }

            sem_destroy(&pengine->sem);
            fmsgbus_release(pengine->pmsgbus);
            fdb_release(pengine->db);
            free(pengine);
        }
    }
    else
        FS_ERR("Invalid search engine");
}

bool fsearch_engine_add_dir(fsearch_engine_t *pengine, char const *dir)
{
    if (!pengine || !dir)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    if (!fsdir_is_exist(dir))
    {
        FS_ERR("Directory isn't exist");
        return false;
    }

    size_t const path_len = strlen(dir);
    if (path_len >= FMAX_PATH)
    {
        FS_ERR("Path length is too long");
        return false;
    }

    #ifdef _WIN32
    // TODO: path can be utf-8 string
    char lpath[FMAX_PATH];
    for(size_t i = 0; i < path_len; ++i)
        lpath[i] = tolower(dir[i]);
    lpath[path_len] = 0;
    dir = lpath;
    #endif

    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pengine->db, &transaction))
    {
        fdb_dirs_t *dirs = fdb_dirs(&transaction);
        if (dirs)
        {
            uint32_t id;
            if (fdb_dirs_add_unique(dirs, &transaction, dir, &id))
            {
                fdb_dirs_scan_status_t *dir_scan_status = fdb_dirs_scan_status(&transaction);
                if(dir_scan_status)
                {
                    fdir_scan_status_t scan_status = { id };

                    if (fdb_dirs_scan_status_add(dir_scan_status, &transaction, &scan_status))
                    {
                        ret = true;
                        fdb_transaction_commit(&transaction);
                    }
                    fdb_dirs_scan_status_release(dir_scan_status);
                }
            } else FS_WARN("Directory isn't unique");
            fdb_dirs_release(dirs);
        }
        fdb_transaction_abort(&transaction);
    }

    if (ret)
        sem_post(&pengine->sem);

    return ret;
}
