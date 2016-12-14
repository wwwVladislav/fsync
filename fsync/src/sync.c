#include "sync.h"
#include "fsutils.h"
#include "config.h"
#include <futils/log.h>
#include <futils/queue.h>
#include <futils/prefix_tree.h>
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
    FSYNC_FILES_TREE_SIZE   = 256,
    FSYNC_QUEUEBUF_SIZE     = 256 * 1024
};

struct fsync
{
    volatile bool     is_active;
    char              tree_buf[FPTREE_MEM_NEED(FSYNC_FILES_TREE_SIZE, FSMAX_PATH)]; // buffer for files tree
    char              queue_buf[FSYNC_QUEUEBUF_SIZE];                               // buffer for file events queue
    fptree_t         *files_tree;                                                   // files tree
    fring_queue_t    *events_queue;                                                 // events queue
    sem_t             events_sem;                                                   // semaphore for events waiting
    pthread_t         thread;
    fsdir_listener_t *dir_listener;
};

#define fsync_push_lock(mutex)                      \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fsync_pop_lock() pthread_cleanup_pop(1);

/*
static int fsdir_compare(const void *pa, const void *pb)
{
#   ifdef _WIN32
    // TODO: windows
    return 0;
#   else
    return strcmp((const char *)pa, (const char *)pb);
#   endif
}
*/

static void fsdir_evt_handler(fsdir_event_t const *event, void *arg)
{
    fsync_t *psync = (fsync_t*)arg;
    if (fring_queue_push_back(psync->events_queue, event, offsetof(fsdir_event_t, path) + strlen(event->path) + 1) == FSUCCESS)
        sem_post(&psync->events_sem);
    else
        FS_WARN("Unable to push the file system event into the queue");
}

static void *fsync_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_active = true;

    while(psync->is_active)
    {
        struct timespec tm = { time(0) + FSYNC_TIMEOUT, 0 };
        while (sem_timedwait(&psync->events_sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!psync->is_active)
            break;

        fsdir_event_t *event = 0;
        uint32_t event_size = 0;
        if (fring_queue_front(psync->events_queue, (void **)&event, &event_size) == FSUCCESS)
        {
            time_t const cur_time = time(0);
            uint32_t const path_len = (uint32_t)strlen(event->path);
            ferr_t const err = fptree_node_insert(psync->files_tree, (uint8_t const *)event->path, path_len, (void *)cur_time, 0);
            if (err != FSUCCESS)
                FS_WARN("Unable to push the file name into the files tree. Error: %d", err);

            printf("[%d] %s\n", event->action, event->path);

            if (fring_queue_pop_front(psync->events_queue) != FSUCCESS)
                FS_WARN("Unable to pop the file system event from the queue");
        }

/*
        bool is_empty;

        fsync_push_lock(psync->files_tree_mutex);
        is_empty = fptree_empty(psync->files_tree);
        fsync_pop_lock();

        if (!is_empty)
        {
            static struct timespec const ts = { 1, 0 };
            nanosleep(&ts, NULL);
        }
        else
        {
            printf("Empty!\n");
            continue;
        }

        fsync_push_lock(psync->files_tree_mutex);

        fptree_iterator_t *it;
        if (fptree_iterator_create(psync->files_tree, &it) == FSUCCESS)
        {
            fptree_node_t node;
            for(ferr_t err = fptree_first(it, &node);
                err == FSUCCESS;
                err = fptree_next(it, &node))
            {
                if (node.data)
                    printf("[%d] %s\n", *(uint32_t*)&node.data, node.key);
            }
            fptree_iterator_delete(it);
        }
        else
            FS_WARN("Unable to create the files tree iteartor");
        fptree_clear(psync->files_tree);

        fsync_pop_lock();
*/
    }

    return 0;
}

fsync_t *fsync_create(char const *dir)
{
    if (!dir || !*dir)
    {
        FS_ERR("Invalid directory path");
        return 0;
    }

    fsync_t *psync = malloc(sizeof(fsync_t));
    if (!psync)
    {
        FS_ERR("Unable to allocate memory for directories synchronizer");
        return 0;
    }
    memset(psync, 0, sizeof *psync);

    if (fptree_create(psync->tree_buf, sizeof psync->tree_buf, FSMAX_PATH, &psync->files_tree) != FSUCCESS)
    {
        FS_ERR("The prefix tree for file paths isn't created");
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
        fptree_delete(psync->files_tree);
        sem_destroy(&psync->events_sem);
        free(psync);
    }
}

/*
void fsync_test()
{
    char in[] =
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";

    char out[16] = { 0 };

    rs_job_t *job = rs_sig_begin(RS_DEFAULT_BLOCK_LEN, 0, RS_BLAKE2_SIG_MAGIC);

    rs_buffers_t buf = { 0 };
    buf.next_in = in;
    buf.avail_in = sizeof in;
    buf.eof_in = 1;
    buf.next_out = out;
    buf.avail_out = sizeof out;
    rs_result result = rs_job_iter(job, &buf);
    while(result == RS_BLOCKED)
    {
        buf.next_out = out;
        buf.avail_out = sizeof out;
        result = rs_job_iter(job, &buf);
    }

    rs_job_free(job);
}
*/
