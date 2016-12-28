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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

    pthread_mutex_t      id_mutex;
    uint32_t             next_id;
};

#define fsync_push_lock(mutex)                      \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fsync_pop_lock() pthread_cleanup_pop(1);

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
    (void)size;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
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
            for (bool ret = fdb_sync_files_iterator_first(it, &info, 0);
                 psync->is_active && ret;
                 ret = fdb_sync_files_iterator_next(it, &info, 0))
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
    (void)size;
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

            fmsg_request_files_content_t files_req;
            files_req.uuid = psync->uuid;
            files_req.destination = msg->uuid;
            files_req.files_num = 0;

            fdb_sync_files_iterator_t *it = fdb_sync_files_iterator_diff(&msg->uuid, &psync->uuid);
            if (it)
            {
                ffile_info_t info;

                for (bool ret = fdb_sync_files_iterator_first(it, &info, 0);
                     psync->is_active && ret;
                     ret = fdb_sync_files_iterator_next(it, &info, 0))
                {
                    char *path = files_req.files[files_req.files_num];
                    memcpy(path, info.path, sizeof info.path);

                    if (++files_req.files_num >= sizeof files_req.files / sizeof *files_req.files)
                    {
                        if (fmsgbus_publish(psync->msgbus, FREQUEST_FILES_CONTENT, &files_req, sizeof files_req) != FSUCCESS)
                            FS_ERR("Files list wasn't requested");
                        files_req.files_num = 0;
                    }
                }
                fdb_sync_files_iterator_free(it);

                if (files_req.files_num)
                {
                    if (fmsgbus_publish(psync->msgbus, FREQUEST_FILES_CONTENT, &files_req, sizeof files_req) != FSUCCESS)
                        FS_ERR("Files list wasn't requested");
                }
            }
        }
    }
}

static void fsync_req_files_content_handler(fsync_t *psync, uint32_t msg_type, fmsg_request_files_content_t const *msg, uint32_t size)
{
    (void)size;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("UUID %llx%llx requests files content", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

        char path[FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        for(uint32_t i = 0; psync->is_active && i < msg->files_num; ++i)
        {
            fmsg_file_info_t info;
            info.uuid = psync->uuid;
            info.destination = msg->uuid;
            memcpy(info.path, msg->files[i], sizeof info.path);
            fsync_push_lock(psync->id_mutex);
            info.id = psync->next_id++;
            fsync_pop_lock();

            if (strlen(info.path) + len > sizeof path)
            {
                FS_WARN("File path length is too long: \'%s\'", info.path);
                continue;
            }

            strncpy(path + len, info.path, sizeof path - len);

            if (!fsfile_size(path, &info.size))
            {
                FS_WARN("File \'%s\' is not accessible", info.path);
                continue;
            }

            if (fmsgbus_publish(psync->msgbus, FFILE_INFO, &info, sizeof info) == FSUCCESS)
            {
                FS_INFO("File info was sent. Size of \'%s\' is %d bytes", info.path, info.size);

                int fd = open(path, O_RDONLY);
                if (fd != -1)
                {
                    fmsg_file_part_t file_part;
                    file_part.uuid = psync->uuid;
                    file_part.destination = msg->uuid;
                    file_part.id = info.id;
                    file_part.offset = 0;
                    file_part.size = 0;

                    ssize_t size;
                    while((size = read(fd, file_part.data, sizeof file_part.data)) > 0)
                    {
                        file_part.size = (uint16_t)size;

                        if (fmsgbus_publish(psync->msgbus, FFILE_PART, &file_part, sizeof file_part) != FSUCCESS)
                            FS_ERR("Unable to send the file part: offset=%llu, size=%u", file_part.offset, file_part.size);
                        else
                            FS_INFO("File part was sent. path=\'%s\', offset=%llu, size=%u", info.path, file_part.offset, file_part.size);

                        file_part.offset += file_part.size;
                    }

                    close(fd);
                }
                else
                    FS_ERR("Unable to open the file: \'%s\'", path);
            }
            else FS_ERR("File info wasn't sent");
        }
    }
}

static void fsync_file_info_handler(fsync_t *psync, uint32_t msg_type, fmsg_file_info_t const *msg, uint32_t size)
{
    (void)size;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("File info received from UUID %llx%llx. path=\'%s\', id=%u, size=%llu", msg->uuid.data.u64[0], msg->uuid.data.u64[1], msg->path, msg->id, msg->size);

        char path[FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        if (strlen(msg->path) + len <= sizeof path)
        {
            strncpy(path + len, msg->path, sizeof path - len);

            int fd = open(path, O_CREAT | O_EXCL | O_WRONLY);
            if (fd != -1)
            {
                ftruncate(fd, msg->size);
                close(fd);
            }
            else
                FS_ERR("Unable to create new file: \'%s\'", path);
        }
        else FS_WARN("File path length is too long: \'%s\'", msg->path);
    }
}

static void fsync_file_part_handler(fsync_t *psync, uint32_t msg_type, fmsg_file_part_t const *msg, uint32_t size)
{
    (void)size;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("File part received from UUID %llx%llx. id=%u, offset=%llu, size=%u", msg->uuid.data.u64[0], msg->uuid.data.u64[1], msg->id, msg->offset, msg->size);
        // TODO
    }
}

static void fsync_msgbus_retain(fsync_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,           (fmsg_handler_t)fsync_status_handler,            psync);
    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST,       (fmsg_handler_t)fsync_sync_files_list_handler,   psync);
    fmsgbus_subscribe(psync->msgbus, FREQUEST_FILES_CONTENT, (fmsg_handler_t)fsync_req_files_content_handler, psync);
    fmsgbus_subscribe(psync->msgbus, FFILE_INFO,             (fmsg_handler_t)fsync_file_info_handler,         psync);
    fmsgbus_subscribe(psync->msgbus, FFILE_PART,             (fmsg_handler_t)fsync_file_part_handler,         psync);

}

static void fsync_msgbus_release(fsync_t *psync)
{
    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,           (fmsg_handler_t)fsync_status_handler);
    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST,       (fmsg_handler_t)fsync_sync_files_list_handler);
    fmsgbus_unsubscribe(psync->msgbus, FREQUEST_FILES_CONTENT, (fmsg_handler_t)fsync_req_files_content_handler);
    fmsgbus_unsubscribe(psync->msgbus, FFILE_INFO,             (fmsg_handler_t)fsync_file_info_handler);
    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART,             (fmsg_handler_t)fsync_file_part_handler);
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
                for (bool ret = fdb_sync_files_iterator_first(it, &info, 0);
                     ret;
                     ret = fdb_sync_files_iterator_next(it, &info, 0))
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

            char full_path[FMAX_PATH];
            size_t full_path_len = fsdir_iterator_full_path(it, &entry, full_path, sizeof full_path);
            if (full_path_len <= sizeof full_path)
            {
                if (fsfile_md5sum(full_path, &info.digest))
                {
                    fsdir_iterator_path(it, &entry, info.path, sizeof info.path);
                    fdb_sync_file_add(&psync->uuid, &info);
                }
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

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    psync->id_mutex = mutex_initializer;

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
