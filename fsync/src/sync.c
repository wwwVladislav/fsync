#include "sync.h"
#include "fsutils.h"
#include <futils/log.h>
#define RSYNC_NO_STDIO_INTERFACE
#include <librsync.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

static size_t const FSYNC_INBUF_SIZE  = 32 * 1024;
static size_t const FSYNC_SIGBUF_SIZE = 16 * 1024;

struct fsync
{
    volatile bool     is_active;
    pthread_t         thread;
    fsdir_listener_t *dir_listener;
};

#define fsync_push_lock(mutex)                      \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fsync_pop_lock() pthread_cleanup_pop(1);

static void fsdir_evt_handler(fsdir_action_t action, char const *dir, void *arg)
{
    fsync_t *psync = (fsync_t*)arg;
    // TODO
}

static void *fsync_thread(void *param)
{
    fsync_t *psync = (fsync_t*)param;
    psync->is_active = true;

    // TODO
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

    return psync;
}

void fsync_free(fsync_t *psync)
{
    if (psync)
    {
        fsdir_listener_free(psync->dir_listener);
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