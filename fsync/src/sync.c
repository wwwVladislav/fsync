#include "sync.h"
#include "fsutils.h"
#include "fcomposer.h"
#include <config.h>
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
    FSYNC_MAX_QUEUE_ITEMS   = 256,
    FSYNC_QUEUEBUF_SIZE     = FSYNC_MAX_QUEUE_ITEMS * sizeof(fsdir_event_t)
};

struct fsync
{
    fuuid_t              uuid;
    char                 dir[FMAX_PATH];

    volatile bool        is_events_queue_processing_active;
    pthread_t            events_queue_processing_thread;
    sem_t                events_queue_sem;                                                                  // semaphore for events waiting
    fring_queue_t       *events_queue;                                                                      // events queue
    char                 events_queue_buf[FSYNC_QUEUEBUF_SIZE];                                             // buffer for file events queue

    volatile bool        is_sync_active;
    pthread_t            sync_thread;
    sem_t                sync_sem;
    fcomposer_t          file_composer;

    fsdir_listener_t    *dir_listener;

    fmsgbus_t           *msgbus;

    pthread_mutex_t      id_mutex;
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
        sem_post(&psync->events_queue_sem);
    else
        FS_WARN("Unable to push the file system event into the queue");
}

static void fsync_notify_files_diff(fsync_t *psync, fuuid_t const *uuid)
{
    fdb_sync_files_iterator_t *it = fdb_sync_files_iterator_diff(&psync->uuid, uuid);
    if (it)
    {
        fmsg_sync_files_list_t files_list;
        files_list.uuid = psync->uuid;
        files_list.destination = *uuid;
        files_list.is_last = false;
        files_list.files_num = 0;

        ffile_info_t info;
        bool have_diff = false;

        for (bool ret = fdb_sync_files_iterator_first(it, &info, 0);
             psync->is_events_queue_processing_active && ret;
             ret = fdb_sync_files_iterator_next(it, &info, 0))
        {
            have_diff = true;
            fsync_file_info_t *file_info = &files_list.files[files_list.files_num];
            file_info->id     = info.id;
            file_info->digest = info.digest;
            file_info->size   = info.size;
            memcpy(file_info->path, info.path, sizeof info.path);

            if (++files_list.files_num >= sizeof files_list.files / sizeof *files_list.files)
            {
                if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                    FS_ERR("Files list not published");
                files_list.files_num = 0;
            }
        }

        fdb_sync_files_iterator_free(it);

        if (have_diff)
        {
            files_list.is_last = true;
            if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                FS_ERR("Files list not published");
        }
    }
}

static void fsync_status_handler(fsync_t *psync, uint32_t msg_type, fmsg_node_status_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
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
                 psync->is_events_queue_processing_active && ret;
                 ret = fdb_sync_files_iterator_next(it, &info, 0))
            {
                fsync_file_info_t *file_info = &files_list.files[files_list.files_num];
                file_info->id     = info.id;
                file_info->digest = info.digest;
                file_info->size   = info.size;
                memcpy(file_info->path, info.path, sizeof info.path);

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
    (void)msg_type;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("UUID %llx%llx sent files list for synchronization", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

        time_t const cur_time = time(0);
        bool is_need_sync = false;

        for(uint32_t i = 0; i < msg->files_num; ++i)
        {
            ffile_info_t info = { 0 };
            info.id = msg->files[i].id;
            memcpy(info.path, msg->files[i].path, sizeof msg->files[i].path);
            info.mod_time = cur_time;
            info.digest = msg->files[i].digest;
            info.size = msg->files[i].size;
            info.is_exist = true;
            fdb_sync_file_add(&msg->uuid, &info);

            ffile_info_t own_file_info;
            if (!fdb_sync_file_get(&psync->uuid, info.path, &own_file_info))
            {
                own_file_info = info;
                own_file_info.id = FINVALID_ID;
                own_file_info.is_exist = false;
                fdb_sync_file_add(&psync->uuid, &own_file_info);
                is_need_sync = true;
            }
            else if (memcmp(&own_file_info.digest, &info.digest, sizeof info.digest) != 0)
            {
                own_file_info.is_exist = false;
                fdb_sync_file_add(&psync->uuid, &own_file_info);
                is_need_sync = true;
            }
        }

        if (is_need_sync)
            sem_post(&psync->sync_sem);

        if (msg->is_last)
            fsync_notify_files_diff(psync, &msg->uuid);
    }
}

static void fsync_file_part_request_handler(fsync_t *psync, uint32_t msg_type, fmsg_file_part_request_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("UUID %llx%llx requests file content. Id=%u, part=%u", msg->uuid.data.u64[0], msg->uuid.data.u64[1], msg->id, msg->block_number);

        char path[FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        if (fdb_sync_file_path(&psync->uuid, msg->id, path + len, sizeof path - len))
        {
            int fd = open(path, O_BINARY | O_RDONLY);
            if (fd != -1)
            {
                fmsg_file_part_t part;
                part.uuid         = psync->uuid;
                part.destination  = msg->uuid;
                part.id           = msg->id;
                part.block_number = msg->block_number;

                if (lseek(fd, part.block_number * sizeof part.data, SEEK_SET) >= 0)
                {
                    ssize_t size = read(fd, part.data, sizeof part.data);
                    if (size > 0)
                    {
                        part.size = size;
                        if (fmsgbus_publish(psync->msgbus, FFILE_PART, &part, sizeof part) != FSUCCESS)
                            FS_ERR("File part message not published");
                    }
                    else FS_ERR("File reading failed");
                }
                else FS_ERR("lseek failed");

                close(fd);
            }
            else FS_ERR("Unable to open the file: \'%s\'", path);
        }
    }
}

static void fsync_file_part_handler(fsync_t *psync, uint32_t msg_type, fmsg_file_part_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) != 0)
    {
        FS_INFO("File part received from UUID %llx%llx. id=%u, block=%u, size=%u", msg->uuid.data.u64[0], msg->uuid.data.u64[1], msg->id, msg->block_number, msg->size);

        char path[FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        if (fdb_sync_file_path(&msg->uuid, msg->id, path + len, sizeof path - len))
        {
            int fd = open(path, O_CREAT | O_BINARY | O_WRONLY);
            if (fd != -1)
            {
                if (lseek(fd, msg->block_number * sizeof msg->data, SEEK_SET) >= 0)
                {
                    if (write(fd, msg->data, msg->size) < 0)
                        FS_ERR("Unable to write data into the file: \'%s\'", path);
                }
                else FS_ERR("lseek failed");

                close(fd);
            }
            else FS_ERR("Unable to open the file: \'%s\'", path);
        }
    }
}

static void fsync_msgbus_retain(fsync_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,          (fmsg_handler_t)fsync_status_handler,            psync);
    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST,      (fmsg_handler_t)fsync_sync_files_list_handler,   psync);
    fmsgbus_subscribe(psync->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)fsync_file_part_request_handler, psync);
    fmsgbus_subscribe(psync->msgbus, FFILE_PART,            (fmsg_handler_t)fsync_file_part_handler,         psync);

}

static void fsync_msgbus_release(fsync_t *psync)
{
    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,        (fmsg_handler_t)fsync_status_handler);
    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST,    (fmsg_handler_t)fsync_sync_files_list_handler);
    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)fsync_file_part_request_handler);
    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART,          (fmsg_handler_t)fsync_file_part_handler);
    fmsgbus_release(psync->msgbus);
}

static void *fsync_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_sync_active = true;

    while(psync->is_sync_active)
    {
        while (sem_wait(&psync->sync_sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ffile_info_t file_info;
        while (psync->is_sync_active && !fdb_sync_file_get_if_not_exist(&psync->uuid, &file_info))
        {
            // TODO: Find all active nodes.
            // Now we get all nodes.

            psync->file_composer.is_active = true;
            psync->file_composer.msgbus = psync->msgbus;
            psync->file_composer.nodes_num = fdb_get_uuids(&psync->file_composer.nodes);
            psync->file_composer.file_id = file_info.id;
            psync->file_composer.file_size = file_info.size;
            strncpy(psync->file_composer.path, file_info.path, sizeof file_info.path);

            // Find all nodes where the file is exist
            // Request parts from active nodes
        }

        psync->file_composer.is_active = false;
    }

    return false;
}

static void *fsync_events_queue_processing_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_events_queue_processing_active = true;

    size_t len = strlen(psync->dir);

    while(psync->is_events_queue_processing_active)
    {
        while (sem_wait(&psync->events_queue_sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!psync->is_events_queue_processing_active)
            break;

        time_t const cur_time = time(0);
        fsdir_event_t *event = 0;
        uint32_t event_size = 0;

        uint32_t new_files_num = 0;
        ffile_info_t new_files[FSYNC_MAX_QUEUE_ITEMS];

        // Copy data from queue into the list
        if (fring_queue_front(psync->events_queue, (void **)&event, &event_size) == FSUCCESS)
        {
            switch(event->action)
            {
                case FSDIR_ACTION_ADDED:
                {
                    ffile_info_t info = { 0 };
                    info.id = FINVALID_ID;
                    info.mod_time = cur_time;
                    info.is_exist = true;

                    if (fsfile_md5sum(event->path, &info.digest))
                    {
                        strncpy(info.path, event->path + len + 1, sizeof info.path - len - 1);
                        fsfile_size(event->path, &info.size);
                        if (fdb_sync_file_add_unique(&psync->uuid, &info))
                            memcpy(&new_files[new_files_num++], &info, sizeof info);
                    }

                    break;
                }

                case FSDIR_ACTION_REMOVED:  // TODO: remove file from other nodes
                case FSDIR_ACTION_MODIFIED: // TODO: use rsync for differences synchronization
                case FSDIR_ACTION_RENAMED:  // TODO: rename for other nodes
                case FSDIR_ACTION_REOPENED: // TODO: rescan whole directory
                default:
                    break;
            }

            if (fring_queue_pop_front(psync->events_queue) != FSUCCESS)
                FS_WARN("Unable to pop the file system event from the queue");
        }

        // Notify other nodes
/*
        fuuid_t uuids[FMAX_CONNECTIONS_NUM];
        uint32_t nodes_num = fdb_get_uuids(&uuids);

        for(uint32_t i = 0; i < new_files_num; ++i)
        {
            ffile_info_t *info = &new_files[i];
            info->id = FINVALID_ID;
            info->is_exist = false;

            for(uint32_t j = 0; j < nodes_num; ++j)
            {
                if (fdb_sync_file_add_unique(&uuids[j], info))
                {
                    // TODO
                }
            }
        }
*/
    }   // while(psync->is_active)

    return 0;
}

static void fsync_scan_dir(fsync_t *psync)
{
    fdb_sync_file_del_all(&psync->uuid);

    fsiterator_t *it = fsdir_iterator(psync->dir);

    time_t const cur_time = time(0);

    for(dirent_t entry; fsdir_iterator_next(it, &entry);)
    {
        if (entry.type == FS_REG)
        {
            ffile_info_t info = { 0 };
            info.id = FINVALID_ID;
            info.mod_time = cur_time;
            info.is_exist = true;

            char full_path[FMAX_PATH];
            size_t full_path_len = fsdir_iterator_full_path(it, &entry, full_path, sizeof full_path);
            if (full_path_len <= sizeof full_path)
            {
                if (fsfile_md5sum(full_path, &info.digest))
                {
                    fsdir_iterator_path(it, &entry, info.path, sizeof info.path);
                    fsfile_size(full_path, &info.size);

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

    if (fring_queue_create(psync->events_queue_buf, sizeof psync->events_queue_buf, &psync->events_queue) != FSUCCESS)
    {
        FS_ERR("The file system events queue isn't created");
        free(psync);
        return 0;
    }

    if (sem_init(&psync->events_queue_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fsync_free(psync);
        return 0;
    }

    if (sem_init(&psync->sync_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fsync_free(psync);
        return 0;
    }

    psync->dir_listener = fsdir_listener_create();
    if (!psync->dir_listener)
    {
        fsync_free(psync);
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

    static struct timespec const one_sec = { 1, 0 };

    int rc = pthread_create(&psync->sync_thread, 0, fsync_thread, (void*)psync);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directories synchronization. Error: %d", rc);
        fsync_free(psync);
        return 0;
    }

    while(!psync->is_sync_active)
        nanosleep(&one_sec, NULL);

    rc = pthread_create(&psync->events_queue_processing_thread, 0, fsync_events_queue_processing_thread, (void*)psync);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directories synchronization. Error: %d", rc);
        fsync_free(psync);
        return 0;
    }

    fsync_scan_dir(psync);

    while(!psync->is_events_queue_processing_active)
        nanosleep(&one_sec, NULL);

    fmsg_node_status_t const status = { *uuid, FSTATUS_READY4SYNC };
    if (fmsgbus_publish(psync->msgbus, FNODE_STATUS, &status, sizeof status) != FSUCCESS)
        FS_ERR("Node status not published");

    return psync;
}

void fsync_free(fsync_t *psync)
{
    if (psync)
    {
        if (psync->is_sync_active)
        {
            psync->is_sync_active = false;
            sem_post(&psync->sync_sem);
            pthread_join(psync->sync_thread, 0);
        }

        if (psync->is_events_queue_processing_active)
        {
            psync->is_events_queue_processing_active = false;
            sem_post(&psync->events_queue_sem);
            pthread_join(psync->events_queue_processing_thread, 0);
        }

        fsdir_listener_free(psync->dir_listener);
        fring_queue_free(psync->events_queue);
        sem_destroy(&psync->events_queue_sem);
        sem_destroy(&psync->sync_sem);
        fsync_msgbus_release(psync);
        free(psync);
    }
}
