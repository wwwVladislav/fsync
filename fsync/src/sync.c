#include "sync.h"
#include "fsutils.h"
#include "synchronizer.h"
#include <fcommon/limits.h>
#include <fcommon/messages.h>
#include <fdb/sync/sync_files.h>
#include <fdb/sync/statuses.h>
#include <fdb/sync/nodes.h>
#include <futils/log.h>
#include <futils/queue.h>
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
#include <stdbool.h>

static struct timespec const F1_SEC = { 1, 0 };
static struct timespec const F10_MSEC = { 0, 10000000 };

enum
{
    FSYNC_FILES_LIST_SIZE   = 1000,                     // Max number of files for sync
    FSYNC_MAX_QUEUE_ITEMS   = 256,
    FSYNC_QUEUEBUF_SIZE     = FSYNC_MAX_QUEUE_ITEMS * sizeof(fsdir_event_t)
};

struct fsync
{
    volatile uint32_t    ref_counter;
    fuuid_t              uuid;
    char                 dir[FMAX_PATH];

    volatile bool        is_events_queue_processing_active;
    pthread_t            events_queue_processing_thread;
    sem_t                events_queue_sem;                                                                  // semaphore for events waiting
    fring_queue_t       *events_queue;                                                                      // events queue
    char                 events_queue_buf[FSYNC_QUEUEBUF_SIZE];                                             // buffer for file events queue
    time_t               sync_time;

    volatile bool        is_sync_active;
    pthread_t            sync_thread;
    sem_t                sync_sem;

    fsdir_listener_t    *dir_listener;

    fmsgbus_t           *msgbus;
    fdb_t               *db;
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

static void fsync_status_handler(fsync_t *psync, uint32_t msg_type, fmsg_node_status_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) == 0)
        return;

    FS_INFO("UUID %llx%llx is ready for sync", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

    fdb_transaction_t transaction = { 0 };

    if (fdb_transaction_start(psync->db, &transaction))
    {
        fdb_sync_files_map_t *files_map = fdb_sync_files(&transaction, &psync->uuid);
        if (files_map)
        {
            fdb_sync_files_iterator_t *files_iterator = fdb_sync_files_iterator(files_map, &transaction);
            if (files_iterator)
            {
                fmsg_sync_files_list_t files_list;
                files_list.uuid = psync->uuid;
                files_list.destination = msg->uuid;
                files_list.is_last = false;
                files_list.files_num = 0;

                fsync_file_info_t info;

                for (bool st = fdb_sync_files_iterator_first(files_iterator, &info); st; st = fdb_sync_files_iterator_next(files_iterator, &info))
                {
                    if ((info.status & FFILE_IS_EXIST) != 0)
                    {
                        fmsg_sync_file_info_t *file_info = &files_list.files[files_list.files_num++];
                        file_info->id       = info.id;
                        file_info->digest   = info.digest;
                        file_info->size     = info.size;
                        file_info->is_exist = (info.status & FFILE_IS_EXIST) != 0;
                        memcpy(file_info->path, info.path, sizeof info.path);

                        if (files_list.files_num >= sizeof files_list.files / sizeof *files_list.files)
                        {
                            if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                                FS_ERR("Files list not published");
                            files_list.files_num = 0;
                        }
                    }
                }
                fdb_sync_files_iterator_free(files_iterator);

                files_list.is_last = true;
                if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                    FS_ERR("Files list not published");
            }

            fdb_sync_files_release(files_map);
        }

        fdb_transaction_abort(&transaction);
    }
}

static void fsync_file_info_get(fmsg_sync_file_info_t const *sync_info, fsync_file_info_t *info)
{
    time_t const cur_time = time(0);
    memset(info, 0, sizeof *info);

    info->id = sync_info->id;
    memcpy(info->path, sync_info->path, sizeof sync_info->path);
    info->mod_time = cur_time;
    info->digest = sync_info->digest;
    info->size = sync_info->size;
    info->status = FFILE_DIGEST_IS_CALCULATED;
    if (sync_info->is_exist)
        info->status |= FFILE_IS_EXIST;
}

static void fsync_notify_files_diff(fsync_t *psync, fuuid_t const *uuid)
{
    fdb_transaction_t transaction = { 0 };

    if (fdb_transaction_start(psync->db, &transaction))
    {
        fdb_sync_files_map_t *files_map_1 = fdb_sync_files(&transaction, &psync->uuid);
        if (files_map_1)
        {
            fdb_sync_files_map_t *files_map_2 = fdb_sync_files(&transaction, uuid);
            if (files_map_2)
            {
                fdb_sync_files_diff_iterator_t *diff = fdb_sync_files_diff_iterator(files_map_1, files_map_2, &transaction);
                if (diff)
                {
                    fmsg_sync_files_list_t files_list;
                    files_list.uuid = psync->uuid;
                    files_list.destination = *uuid;
                    files_list.is_last = false;
                    files_list.files_num = 0;

                    fsync_file_info_t info;
                    bool have_diff = false;

                    for (bool st = fdb_sync_files_diff_iterator_first(diff, &info, 0); st; st = fdb_sync_files_diff_iterator_next(diff, &info, 0))
                    {
                        have_diff = true;

                        fmsg_sync_file_info_t *file_info = &files_list.files[files_list.files_num++];
                        file_info->id       = info.id;
                        file_info->digest   = info.digest;
                        file_info->size     = info.size;
                        file_info->is_exist = (info.status & FFILE_IS_EXIST) != 0;
                        memcpy(file_info->path, info.path, sizeof info.path);

                        if (files_list.files_num >= sizeof files_list.files / sizeof *files_list.files)
                        {
                            if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                                FS_ERR("Files list not published");
                            files_list.files_num = 0;
                        }
                    }

                    fdb_sync_files_diff_iterator_free(diff);

                    if (have_diff)
                    {
                        files_list.is_last = true;
                        if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                            FS_ERR("Files list not published");
                    }
                }

                fdb_transaction_commit(&transaction);
                fdb_sync_files_release(files_map_2);
            }
            fdb_sync_files_release(files_map_1);
        }

        fdb_transaction_abort(&transaction);
    }
}

static void fsync_sync_files_list_handler(fsync_t *psync, uint32_t msg_type, fmsg_sync_files_list_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
    if (memcmp(&msg->uuid, &psync->uuid, sizeof psync->uuid) == 0)
        return;

    FS_INFO("UUID %llx%llx sent files list for synchronization", msg->uuid.data.u64[0], msg->uuid.data.u64[1]);

    bool is_need_sync = false;
    fdb_sync_files_map_t *files_map;

    fdb_transaction_t transaction = { 0 };

    // Remote node files list (+)
    if (fdb_transaction_start(psync->db, &transaction))
    {
        files_map = fdb_sync_files(&transaction, &msg->uuid);
        if (files_map)
        {
            FS_INFO("Received %u files info", msg->files_num);

            for(uint32_t i = 0; i < msg->files_num; ++i)
            {
                fsync_file_info_t info;
                fsync_file_info_get(msg->files + i, &info);

                if (!fdb_sync_file_add(files_map, &transaction, &info))
                {
                    FS_ERR("Transaction was aborted");
                    fdb_transaction_abort(&transaction);
                    return;
                }
            }

            fdb_transaction_commit(&transaction);
            fdb_sync_files_release(files_map);
        }
        else FS_ERR("Files map wasn't opened");

        fdb_transaction_abort(&transaction);
    }
    else FS_ERR("Transaction wasn't started");

    // Local files list (-)
    if (fdb_transaction_start(psync->db, &transaction))
    {
        fdb_map_t status_map = { 0 };
        files_map = fdb_sync_files(&transaction, &psync->uuid);
        if (files_map)
        {
            if (fdb_sync_files_statuses(&transaction, &psync->uuid, &status_map))
            {
                fmsg_sync_files_list_t files_list;
                files_list.uuid = psync->uuid;
                files_list.destination = msg->uuid;
                files_list.is_last = false;
                files_list.files_num = 0;

                fsync_file_info_t info;

                for(uint32_t i = 0; i < msg->files_num; ++i)
                {
                    fsync_file_info_get(msg->files + i, &info);
                    info.id = FINVALID_ID;
                    info.status = 0;

                    bool is_absent = fdb_sync_file_add_unique(files_map, &transaction, &info);

                    if (is_absent)
                    {
                        fdb_data_t const file_id = { sizeof info.id, &info.id };
                        fdb_statuses_map_put(&status_map, &transaction, FFILE_IS_EXIST, &file_id);

                        fmsg_sync_file_info_t *file_info = &files_list.files[files_list.files_num++];
                        file_info->id       = info.id;
                        file_info->digest   = info.digest;
                        file_info->size     = info.size;
                        file_info->is_exist = (info.status & FFILE_IS_EXIST) != 0;
                        memcpy(file_info->path, info.path, sizeof info.path);

                        if (files_list.files_num >= sizeof files_list.files / sizeof *files_list.files)
                        {
                            if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                                FS_ERR("Files list not published");
                            files_list.files_num = 0;
                        }
                    }

                    is_need_sync |= is_absent;
                }

                fdb_transaction_commit(&transaction);
                fdb_sync_files_release(files_map);
                fdb_map_close(&status_map);

                if (is_need_sync)
                {
                    files_list.is_last = true;
                    if (fmsgbus_publish(psync->msgbus, FSYNC_FILES_LIST, &files_list, sizeof files_list) != FSUCCESS)
                        FS_ERR("Files list not published");
                }
            }
            else FS_ERR("Statuses map wasn't opened");
        }
        else FS_ERR("Files map wasn't opened");

        fdb_transaction_abort(&transaction);
    }
    else FS_ERR("Transaction wasn't started");

    if (is_need_sync)
    {
        FS_INFO("Wake up the synchronization thread");
        sem_post(&psync->sync_sem);
    }

    if (msg->is_last)
    {
        FS_INFO("Notify files lists difference");
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

        char path[2 * FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        fdb_transaction_t transaction = { 0 };
        if (fdb_transaction_start(psync->db, &transaction))
        {
            fdb_sync_files_map_t *files_map = fdb_sync_files(&transaction, &psync->uuid);
            if (files_map)
            {
                if (fdb_sync_file_path(files_map, &transaction, msg->id, path + len, sizeof path - len))
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
                fdb_sync_files_release(files_map);
            }
            fdb_transaction_abort(&transaction);
        }
    }
}

static void fsync_msgbus_retain(fsync_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,          (fmsg_handler_t)fsync_status_handler,            psync);
    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST,      (fmsg_handler_t)fsync_sync_files_list_handler,   psync);
    fmsgbus_subscribe(psync->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)fsync_file_part_request_handler, psync);
}

static void fsync_msgbus_release(fsync_t *psync)
{
    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,        (fmsg_handler_t)fsync_status_handler);
    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST,    (fmsg_handler_t)fsync_sync_files_list_handler);
    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)fsync_file_part_request_handler);
    fmsgbus_release(psync->msgbus);
}

static void *fsync_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_sync_active = true;

    while(psync->is_sync_active)
    {
        printf("Wait\n");

        struct timespec tm = { time(0) + FSYNC_BLOCK_REQ_TIMEOUT, 0 };
        while(psync->is_sync_active && sem_timedwait(&psync->sync_sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!psync->is_sync_active)
            break;

        printf("Synchronization...\n");

        fsynchronizer_t *synchronizer = fsynchronizer_create(psync->msgbus, psync->db, &psync->uuid, psync->dir);
        if (synchronizer)
        {
            while(psync->is_sync_active && fsynchronizer_update(synchronizer));
            fsynchronizer_free(synchronizer);
        }
    }

    return 0;
}

static void *fsync_events_queue_processing_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_events_queue_processing_active = true;

    size_t len = strlen(psync->dir);

    while(psync->is_events_queue_processing_active)
    {
        struct timespec tm = { time(0) + FSYNC_TIMEOUT, 0 };
        while(psync->is_events_queue_processing_active && sem_timedwait(&psync->events_queue_sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!psync->is_events_queue_processing_active)
            break;

        fsdir_event_t *event = 0;
        uint32_t event_size = 0;

        // Copy data from queue into the list
        if (fring_queue_front(psync->events_queue, (void **)&event, &event_size) == FSUCCESS)
        {
            time_t const cur_time = time(0);

            switch(event->action)
            {
                case FSDIR_ACTION_ADDED:    // TODO
/*
                {
                    ffile_info_t info = { 0 };
                    info.id = FINVALID_ID;
                    info.mod_time = cur_time;
                    info.status = FFILE_IS_EXIST;

                    if (fsfile_md5sum(event->path, &info.digest))
                    {
                        strncpy(info.path, event->path + len + 1, sizeof info.path - len - 1);
                        fsfile_size(event->path, &info.size);
                        fdb_sync_file_add_unique(&psync->uuid, &info);
                    }

                    break;
                }
*/
                case FSDIR_ACTION_MODIFIED: // TODO: use rsync for differences synchronization
                case FSDIR_ACTION_REMOVED:  // TODO: remove file from other nodes
                case FSDIR_ACTION_RENAMED:  // TODO: rename for other nodes
                case FSDIR_ACTION_REOPENED: // TODO: rescan whole directory
                default:
                    break;
            }

            if (fring_queue_pop_front(psync->events_queue) != FSUCCESS)
                FS_WARN("Unable to pop the file system event from the queue");
        }

        // TODO: Notify other nodes
/*
        time_t const cur_time = time(0);
        if (cur_time - psync->sync_time >= FSYNC_TIMEOUT)
        {
            fuuid_t uuids[FMAX_CONNECTIONS_NUM];
            uint32_t nodes_num = fdb_get_uuids(uuids);

            for(uint32_t i = 0; i < nodes_num; ++i)
            {
                if (memcmp(&psync->uuid, &uuids[i], sizeof uuids[i]) != 0)
                {
                    // TODO: fsync_notify_files_diff(psync, &uuids[i]);
                }
            }

            psync->sync_time = cur_time;
        }
*/
    }   // while(psync->is_active)

    return 0;
}

static void fsync_scan_dir(fsync_t *psync)
{
    fdb_sync_file_del_all(&psync->uuid);

    fdb_transaction_t transaction = { 0 };

    if (fdb_transaction_start(psync->db, &transaction))
    {
        fdb_sync_files_map_t *files_map = fdb_sync_files(&transaction, &psync->uuid);
        if (files_map)
        {
            fsiterator_t *it = fsdir_iterator(psync->dir);

            time_t const cur_time = time(0);

            for(dirent_t entry; fsdir_iterator_next(it, &entry);)
            {
                if (entry.type == FS_REG)
                {
                    fsync_file_info_t info = { 0 };
                    info.id = FINVALID_ID;
                    info.mod_time = cur_time;
                    info.status = FFILE_IS_EXIST;

                    char full_path[FMAX_PATH];
                    size_t full_path_len = fsdir_iterator_full_path(it, &entry, full_path, sizeof full_path);
                    if (full_path_len <= sizeof full_path)
                    {
                        if (fsfile_md5sum(full_path, &info.digest))
                        {
                            info.status |= FFILE_DIGEST_IS_CALCULATED;
                            fsdir_iterator_path(it, &entry, info.path, sizeof info.path);
                            fsfile_size(full_path, &info.size);

                            if (!fdb_sync_file_add(files_map, &transaction, &info))
                            {
                                fdb_transaction_abort(&transaction);
                                fdb_sync_files_release(files_map);
                                files_map = 0;
                                break;
                            }
                        }
                    }
                    else
                        FS_ERR("Full path length of \'%s\' file is too long.", entry.name);
                }
            }
            fsdir_iterator_free(it);

            if (files_map)
            {
                psync->sync_time = time(0);
                fdb_transaction_commit(&transaction);
                fdb_sync_files_release(files_map);
            }
        }
    }
}

fsync_t *fsync_create(fmsgbus_t *pmsgbus, fdb_t *db, char const *dir, fuuid_t const *uuid)
{
    if (!pmsgbus || !db || !dir || !*dir || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fsync_t *psync = malloc(sizeof(fsync_t));
    if (!psync)
    {
        FS_ERR("Unable to allocate memory for directories synchronizer");
        return 0;
    }
    memset(psync, 0, sizeof *psync);

    psync->ref_counter = 1;
    psync->uuid = *uuid;

    char *dst = psync->dir;
    for(; *dir && dst - psync->dir + 1 < sizeof psync->dir; ++dst, ++dir)
        *dst = *dir == '\\' ? '/' : *dir;
    *dst = 0;

    fsync_msgbus_retain(psync, pmsgbus);

    psync->db = fdb_retain(db);

    if (fring_queue_create(psync->events_queue_buf, sizeof psync->events_queue_buf, &psync->events_queue) != FSUCCESS)
    {
        FS_ERR("The file system events queue isn't created");
        fsync_release(psync);
        return 0;
    }

    if (sem_init(&psync->events_queue_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fsync_release(psync);
        return 0;
    }

    if (sem_init(&psync->sync_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fsync_release(psync);
        return 0;
    }

    psync->dir_listener = fsdir_listener_create();
    if (!psync->dir_listener)
    {
        fsync_release(psync);
        return 0;
    }

    if (!fsdir_listener_reg_handler(psync->dir_listener, fsdir_evt_handler, psync))
    {
        fsync_release(psync);
        return 0;
    }

    if (!fsdir_listener_add_path(psync->dir_listener, psync->dir))
    {
        fsync_release(psync);
        return 0;
    }

    int rc = pthread_create(&psync->sync_thread, 0, fsync_thread, (void*)psync);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directories synchronization. Error: %d", rc);
        fsync_release(psync);
        return 0;
    }

    while(!psync->is_sync_active)
        nanosleep(&F1_SEC, NULL);

    rc = pthread_create(&psync->events_queue_processing_thread, 0, fsync_events_queue_processing_thread, (void*)psync);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directories synchronization. Error: %d", rc);
        fsync_release(psync);
        return 0;
    }

    fsync_scan_dir(psync);

    while(!psync->is_events_queue_processing_active)
        nanosleep(&F1_SEC, NULL);

    fmsg_node_status_t const status = { *uuid, FSTATUS_READY4SYNC };
    if (fmsgbus_publish(psync->msgbus, FNODE_STATUS, &status, sizeof status) != FSUCCESS)
        FS_ERR("Node status not published");

    return psync;
}

fsync_t *fsync_retain(fsync_t *psync)
{
    if (psync)
        psync->ref_counter++;
    else
        FS_ERR("Invalid files synchronizer");
    return psync;
}

void fsync_release(fsync_t *psync)
{
    if (psync)
    {
        if (!psync->ref_counter)
            FS_ERR("Invalid files synchronizer");
        else if (!--psync->ref_counter)
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
            fdb_release(psync->db);
            memset(psync, 0, sizeof *psync);
            free(psync);
        }
    }
    else
        FS_ERR("Invalid files synchronizer");
}
