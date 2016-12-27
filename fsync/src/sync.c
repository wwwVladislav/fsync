#include "sync.h"
#include "fsutils.h"
#include "config.h"
#include <fdb/sync/files.h>
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
#include <stdio.h>

enum
{
    FSYNC_FILES_LIST_SIZE   = 1000,                     // Max number of files for sync
    FSYNC_QUEUEBUF_SIZE     = 256 * 1024
};

struct fsync
{
    volatile bool        is_active;

    fuuid_t              uuid;
    char                 dir[FSMAX_PATH];

    sem_t                events_sem;                                                                         // semaphore for events waiting
    fring_queue_t       *events_queue;                                                                       // events queue
    char                 queue_buf[FSYNC_QUEUEBUF_SIZE];                                                     // buffer for file events queue

    pthread_t            thread;
    fsdir_listener_t    *dir_listener;
    time_t               sync_time;

    fmsgbus_t           *msgbus;
};

static void fsdir_evt_handler(fsdir_event_t const *event, void *arg)
{
    fsync_t *psync = (fsync_t*)arg;
    if (fring_queue_push_back(psync->events_queue, event, offsetof(fsdir_event_t, path) + strlen(event->path) + 1) == FSUCCESS)
        sem_post(&psync->events_sem);
    else
        FS_WARN("Unable to push the file system event into the queue");
}

static void fsync_status_handler(fsync_t *psync, uint32_t msg_type, fmsg_node_status_t const *msg, uint32_t size)
{
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        // Another node wants to sync folders
        FS_INFO("UUID %llx%llx is ready for sync", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

        fdb_sync_files_iterator_t *it = fdb_sync_files_iterator(&psync->uuid);
        if (it)
        {
            fmsg_sync_files_list_t files_list;
            files_list.uuid = psync->uuid;
            files_list.destination = msg->uuid;
            files_list.is_last = false;
            files_list.files_num = 0;

            ffile_info_t info;
            for (bool ret = fdb_sync_files_iterator_first(it, &info);
                 psync->is_active && ret;
                 ret = fdb_sync_files_iterator_next(it, &info))
            {
                fsync_file_info_t *file_info = &files_list.files[files_list.files_num];
                memcpy(file_info->path,     info.path,   sizeof info.path);
                memcpy(&file_info->digest, &info.digest, sizeof info.digest);

                if (++files_list.files_num >= sizeof files_list.files / sizeof *files_list.files)
                {
                    if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                        FS_ERR("Files list not published");
                    files_list.files_num = 0;
                }
            }
            fdb_sync_files_iterator_free(it);

            files_list.is_last = true;
            if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                FS_ERR("Files list not published");
        }
    }
}

static void fsync_sync_files_list_handler(fsync_t *psync, uint32_t msg_type, fmsg_sync_files_list_t const *msg, uint32_t size)
{
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("UUID %llx%llx sent files list for synchronization", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

        for(uint32_t i = 0; i < msg->files_num; ++i)
        {
            ffile_info_t info = { 0 };
            info.digest = msg->files[i].digest;
            memcpy(info.path, msg->files[i].path, sizeof msg->files[i].path);
            fdb_sync_file_add(&msg->uuid, &info);
        }

        if (msg->is_last)
        {
            FS_INFO("Request files from UUID %llx%llx", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);
            // TODO
        }
    }
}

static void fsync_msgbus_retain(fsync_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,     (fmsg_handler_t)fsync_status_handler, psync);
    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST, (fmsg_handler_t)fsync_sync_files_list_handler, psync);
}

static void fsync_msgbus_release(fsync_t *psync)
{
    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,     (fmsg_handler_t)fsync_status_handler);
    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST, (fmsg_handler_t)fsync_sync_files_list_handler);
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
            ffile_info_t file = { 0 };
            strncpy(file.path, event->path, sizeof file.path);
            file.mod_time = cur_time;
            fdb_sync_file_add(&psync->uuid, &file);

            if (fring_queue_pop_front(psync->events_queue) != FSUCCESS)
                FS_WARN("Unable to pop the file system event from the queue");
        }

        // Sync changed files in tree
        if (cur_time - psync->sync_time >= FSYNC_TIMEOUT / 2)
        {
            fdb_sync_files_iterator_t *it = fdb_sync_files_iterator(&psync->uuid);
            if (it)
            {
                ffile_info_t info;
                for (bool ret = fdb_sync_files_iterator_first(it, &info);
                     ret;
                     ret = fdb_sync_files_iterator_next(it, &info))
                {
                    if (info.sync_time < info.mod_time
                        && cur_time - info.mod_time >= FSYNC_TIMEOUT)
                    {
                        // TODO
                        FS_INFO("Sync: [%d] %s", info.mod_time, info.path);
                        info.sync_time = cur_time;
                        fdb_sync_file_update(&psync->uuid, &info);
                    }
                }
                fdb_sync_files_iterator_free(it);
            }

            psync->sync_time = cur_time;
        }
    }

    return 0;
}

static void fsync_scan_dir(fsync_t *psync)
{
    fdb_sync_file_del_all(&psync->uuid);

    fsiterator_t *it = fsdir_iterator(psync->dir);

    for(dirent_t entry; fsdir_iterator_next(it, &entry);)
    {
        if (entry.type == FS_REG)
        {
            ffile_info_t info;

            size_t len = fsdir_iterator_full_path(it, &entry, info.path, sizeof info.path);

            if (len <= sizeof info.path)
            {
                if (fsfile_md5sum(info.path, &info.digest))
                    fdb_sync_file_add(&psync->uuid, &info);
            }
            else
                FS_ERR("Full path length of \'%s\' file is too long.", entry.name);
        }
    }
    fsdir_iterator_free(it);
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

    char *dst = psync->dir;
    for(; *dir && dst - psync->dir + 1 < sizeof psync->dir; ++dst, ++dir)
        *dst = *dir == '\\' ? '/' : *dir;
    *dst = 0;

    fsync_msgbus_retain(psync, pmsgbus);

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

    if (!fsdir_listener_add_path(psync->dir_listener, psync->dir))
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

    fsync_scan_dir(psync);

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
        sem_destroy(&psync->events_sem);
        fsync_msgbus_release(psync);
        free(psync);
    }
}
