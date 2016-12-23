#include "sync.h"
#include "fsutils.h"
#include "config.h"
#include <futils/log.h>
#include <futils/queue.h>
#include <futils/static_allocator.h>
#include <messages.h>
#define RSYNC_NO_STDIO_INTERFACE
#include <librsync.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

enum
{
    FSYNC_INBUF_SIZE        = 32 * 1024,
    FSYNC_SIGBUF_SIZE       = 16 * 1024,
    FSYNC_FILES_LIST_SIZE   = 512,
    FSYNC_QUEUEBUF_SIZE     = 256 * 1024
};

typedef struct
{
    char    path[FSMAX_PATH + 1];
    time_t  modification_time;
} fchanged_file_t;

struct fsync
{
    volatile bool        is_active;

    fuuid_t              uuid;

    sem_t                events_sem;                                                                         // semaphore for events waiting
    fring_queue_t       *events_queue;                                                                       // events queue
    char                 queue_buf[FSYNC_QUEUEBUF_SIZE];                                                     // buffer for file events queue

    char                 fla_buf[FSTATIC_ALLOCATOR_MEM_NEED(FSYNC_FILES_LIST_SIZE, sizeof(fchanged_file_t))];// buffer for files list allocator
    fstatic_allocator_t *files_list_allocator;                                                               // files list allocator
    fchanged_file_t     *files_list[FSYNC_FILES_LIST_SIZE];                                                  // TODO: use prefix tree
    size_t               files_list_size;                                                                    // files list size

    pthread_t            thread;
    fsdir_listener_t    *dir_listener;
    time_t               sync_time;

    fmsgbus_t           *msgbus;
};

static int fsdir_compare(const void *pa, const void *pb)
{
    fchanged_file_t const *lhs = *(fchanged_file_t const **)pa;
    fchanged_file_t const *rhs = *(fchanged_file_t const **)pb;

#   ifdef _WIN32
    // TODO: for windows paths must be compared in lower case
    return strncmp(lhs->path, rhs->path, sizeof rhs->path);
#   else
    return strncmp(lhs->path, rhs->path, sizeof rhs->path);
#   endif
}

static void fsdir_evt_handler(fsdir_event_t const *event, void *arg)
{
    fsync_t *psync = (fsync_t*)arg;
    if (fring_queue_push_back(psync->events_queue, event, offsetof(fsdir_event_t, path) + strlen(event->path) + 1) == FSUCCESS)
        sem_post(&psync->events_sem);
    else
        FS_WARN("Unable to push the file system event into the queue");
}

static void fsync_add_file2list(fsync_t *psync, fchanged_file_t const *file)
{
    fchanged_file_t *changed_file = (fchanged_file_t *)fstatic_alloc(psync->files_list_allocator);

    if (changed_file)
    {
        memcpy(changed_file, file, sizeof *file);
        psync->files_list[psync->files_list_size++] = changed_file;
        qsort(psync->files_list, psync->files_list_size, sizeof(fchanged_file_t*), fsdir_compare);
    }
    else
        FS_WARN("Unable to allocate memory for changed file name");
}

static void fsync_remove_file_from_list(fsync_t *psync, size_t idx)
{
    fstatic_free(psync->files_list_allocator, psync->files_list[idx]);
    for(++idx; idx < psync->files_list_size; ++idx)
        psync->files_list[idx - 1] = psync->files_list[idx];
    psync->files_list[psync->files_list_size--] = 0;
}

static void fsync_status_handler(fsync_t *psync, uint32_t msg_type, fmsg_node_status_t const *msg, uint32_t size)
{
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        // Another node wants to sync folders
        FS_INFO("UUID %llx%llx is ready for sync", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

        // TODO: start synchronization
    }
}

static void fsync_msgbus_retain(fsync_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS, (fmsg_handler_t)fsync_status_handler, psync);
}

static void fsync_msgbus_release(fsync_t *psync)
{
    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS, (fmsg_handler_t)fsync_status_handler);
    fmsgbus_release(psync->msgbus);
}

static void *fsync_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_active = true;

    while(psync->is_active)
    {
        struct timespec tm = { time(0) + FSYNC_TIMEOUT / 2, 0 };
        while (sem_timedwait(&psync->events_sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!psync->is_active)
            break;

        time_t const cur_time = time(0);
        fsdir_event_t *event = 0;
        uint32_t event_size = 0;

        // Copy data from queue into the list
        if (fring_queue_front(psync->events_queue, (void **)&event, &event_size) == FSUCCESS)
        {
            fchanged_file_t file;
            strncpy(file.path, event->path, sizeof file.path);
            file.modification_time = cur_time;
            fchanged_file_t const *pfile = &file;

            fchanged_file_t *changed_file = bsearch(&pfile, psync->files_list, psync->files_list_size, sizeof(fchanged_file_t*), fsdir_compare);
            if (!changed_file)
                fsync_add_file2list(psync, pfile);
            else
                changed_file->modification_time = cur_time;

            if (fring_queue_pop_front(psync->events_queue) != FSUCCESS)
                FS_WARN("Unable to pop the file system event from the queue");
        }

        // Sync changed files in tree
        if (cur_time - psync->sync_time >= FSYNC_TIMEOUT / 2)
        {
            for(size_t i = 0; i < psync->files_list_size;)
            {
                time_t const modification_time = psync->files_list[i]->modification_time;

                if (cur_time - modification_time >= FSYNC_TIMEOUT)
                {
                    // TODO
                    FS_INFO("Sync: [%d] %s", modification_time, psync->files_list[i]->path);
                    fsync_remove_file_from_list(psync, i);
                }
                else
                    ++i;
            }

            psync->sync_time = cur_time;
        }
    }

    return 0;
}

fsync_t *fsync_create(fmsgbus_t *pmsgbus, char const *dir, fuuid_t const *uuid)
{
    if (!pmsgbus)
    {
        FS_ERR("Invalid messages bus");
        return 0;
    }

    if (!dir || !*dir)
    {
        FS_ERR("Invalid directory path");
        return 0;
    }

    if (!uuid)
    {
        FS_ERR("Invalid UUID");
        return 0;
    }

    fsync_t *psync = malloc(sizeof(fsync_t));
    if (!psync)
    {
        FS_ERR("Unable to allocate memory for directories synchronizer");
        return 0;
    }
    memset(psync, 0, sizeof *psync);

    psync->uuid = *uuid;

    fsync_msgbus_retain(psync, pmsgbus);

    if (fstatic_allocator_create(psync->fla_buf, sizeof psync->fla_buf, sizeof(fsdir_event_t), &psync->files_list_allocator) != FSUCCESS)
    {
        FS_ERR("The allocator for file paths isn't created");
        free(psync);
        return 0;
    }

    if (fring_queue_create(psync->queue_buf, sizeof psync->queue_buf, &psync->events_queue) != FSUCCESS)
    {
        FS_ERR("The file system events queue isn't created");
        free(psync);
        return 0;
    }

    if (sem_init(&psync->events_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        free(psync);
        return 0;
    }

    psync->dir_listener = fsdir_listener_create();
    if (!psync->dir_listener)
    {
        free(psync);
        return 0;
    }

    if (!fsdir_listener_reg_handler(psync->dir_listener, fsdir_evt_handler, psync))
    {
        fsync_free(psync);
        return 0;
    }

    if (!fsdir_listener_add_path(psync->dir_listener, dir))
    {
        fsync_free(psync);
        return 0;
    }

    int rc = pthread_create(&psync->thread, 0, fsync_thread, (void*)psync);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directories synchronization. Error: %d", rc);
        fsync_free(psync);
        return 0;
    }

    static struct timespec const ts = { 1, 0 };
    while(!psync->is_active)
        nanosleep(&ts, NULL);

    fmsg_node_status_t const status = { *uuid, FSTATUS_READY4SYNC };
    if (fmsgbus_publish(psync->msgbus, FNODE_STATUS, &status, sizeof status) != FSUCCESS)
        FS_ERR("Node status not published");

    return psync;
}

void fsync_free(fsync_t *psync)
{
    if (psync)
    {
        if (psync->is_active)
        {
            psync->is_active = false;
            sem_post(&psync->events_sem);
            pthread_join(psync->thread, 0);
        }
        fsdir_listener_free(psync->dir_listener);
        fring_queue_free(psync->events_queue);
        fstatic_allocator_delete(psync->files_list_allocator);
        sem_destroy(&psync->events_sem);
        fsync_msgbus_release(psync);
        free(psync);
    }
}
