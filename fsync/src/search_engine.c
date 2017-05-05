#include "search_engine.h"
#include "fsutils.h"
#include <futils/log.h>
#include <fdb/sync/dirs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

static struct timespec const F1_SEC = { 1, 0 };

struct search_engine
{
    volatile uint32_t   ref_counter;
    fmsgbus_t          *pmsgbus;
    fdb_t              *db;

    volatile bool       is_active;
    pthread_t           scan_thread;
    sem_t               sem;
};

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

        /*
        fsynchronizer_t *synchronizer = fsynchronizer_create(psync->msgbus, psync->db, &psync->uuid, psync->dir);
        if (synchronizer)
        {
            while(psync->is_sync_active && fsynchronizer_update(synchronizer));
            fsynchronizer_free(synchronizer);
        }
        */
    }

    return 0;
}

fsearch_engine_t *fsearch_engine(fmsgbus_t *pmsgbus, fdb_t *db)
{
    if (!pmsgbus || !db)
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

    if (sem_init(&pengine->sem, 0, 0) == -1)
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

    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pengine->db, &transaction))
    {
        fdb_dirs_t *dirs = fdb_dirs(&transaction);
        if (dirs)
        {
            ret = fdb_dirs_add(dirs, &transaction, dir, 0);
            if (ret)
                fdb_transaction_commit(&transaction);
            fdb_dirs_release(dirs);
        }
        fdb_transaction_abort(&transaction);
    }

    return ret;
}
