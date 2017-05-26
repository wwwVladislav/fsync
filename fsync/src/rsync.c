#include "rsync.h"
#include <futils/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <librsync.h>

static struct timespec const F1_SEC = { 1, 0 };

enum
{
    FRBUF_SIZE = 8 * 1024
};

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Signature calculation
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_sig_calculator
{
    volatile uint32_t   ref_counter;
    rs_job_t           *job;
    uint32_t            in_size;
    char                in_buf[FRBUF_SIZE];
    uint32_t            out_size;
    char                out_buf[FRBUF_SIZE];
};

frsync_sig_calculator_t *frsync_sig_calculator_create()
{
    frsync_sig_calculator_t *psig = malloc(sizeof(frsync_sig_calculator_t));
    if (!psig)
    {
        FS_ERR("Unable to allocate memory for signature calculator");
        return 0;
    }
    memset(psig, 0, sizeof *psig);

    psig->ref_counter = 1;

    psig->job = rs_sig_begin(RS_DEFAULT_BLOCK_LEN, 0, RS_BLAKE2_SIG_MAGIC);

    if (!psig->job)
    {
        FS_ERR("Unable to create job for signature calculation");
        frsync_sig_calculator_release(psig);
        return 0;
    }

    return psig;
}

frsync_sig_calculator_t *frsync_sig_calculator_retain(frsync_sig_calculator_t *psig)
{
    if (psig)
        psig->ref_counter++;
    else
        FS_ERR("Invalid signature calculator");
    return psig;
}

void frsync_sig_calculator_release(frsync_sig_calculator_t *psig)
{
    if (psig)
    {
        if (!psig->ref_counter)
            FS_ERR("Invalid signature calculator");
        else if (!--psig->ref_counter)
        {
            if (psig->job)
                rs_job_free(psig->job);
            memset(psig, 0, sizeof *psig);
            free(psig);
        }
    }
    else
        FS_ERR("Invalid signature calculator");
}

ferr_t frsync_sig_calculate(frsync_sig_calculator_t *psig, void *pstream, frsync_read_fn_t read, frsync_write_fn_t write)
{
    if (!psig || !read || !write)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    uint32_t rsize = 0;

    do
    {
        rsize = read(psig, psig->in_buf + psig->in_size, sizeof psig->in_buf - psig->in_size);
        psig->in_size += rsize;

        rs_buffers_t buf = { 0 };
        buf.next_in = psig->in_buf;
        buf.avail_in = psig->in_size;
        buf.eof_in = rsize == 0;
        buf.next_out = psig->out_buf;
        buf.avail_out = sizeof psig->out_buf;

        rs_result result = rs_job_iter(psig->job, &buf);

        if (result != RS_BLOCKED
            && result != RS_DONE)
            return FFAIL;

        size_t sig_size = buf.next_out - psig->out_buf;
        while(sig_size)
        {
            uint32_t write_size = write(pstream, psig->out_buf, sig_size);
            if (!write_size)
                return FFAIL;
            sig_size -= write_size;
        }

        if (buf.avail_in)
            memmove(psig->in_buf, psig->in_buf + psig->in_size - buf.avail_in, buf.avail_in);
        psig->in_size = buf.avail_in;
    }
    while (rsize);

    return FSUCCESS;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Signature data
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_signature
{
    volatile uint32_t   ref_counter;
    rs_job_t           *job;
    rs_signature_t     *sumset;
    bool                is_ready;
    uint32_t            in_size;
    char                in_buf[FRBUF_SIZE];
};

frsync_signature_t *frsync_signature_create()
{
    frsync_signature_t *psig = malloc(sizeof(frsync_signature_t));
    if (!psig)
    {
        FS_ERR("Unable to allocate memory for signature");
        return 0;
    }
    memset(psig, 0, sizeof *psig);

    psig->ref_counter = 1;

    psig->job = rs_loadsig_begin(&psig->sumset);

    if (!psig->job)
    {
        FS_ERR("Unable to create job for signature calculation");
        frsync_signature_release(psig);
        return 0;
    }

    return psig;
}

frsync_signature_t *frsync_signature_retain(frsync_signature_t *psig)
{
    if (psig)
        psig->ref_counter++;
    else
        FS_ERR("Invalid signature");
    return psig;
}

void frsync_signature_release(frsync_signature_t *psig)
{
    if (psig)
    {
        if (!psig->ref_counter)
            FS_ERR("Invalid signature");
        else if (!--psig->ref_counter)
        {
            if (psig->job)
                rs_job_free(psig->job);
            if (psig->sumset)
                rs_free_sumset(psig->sumset);
            memset(psig, 0, sizeof *psig);
            free(psig);
        }
    }
    else
        FS_ERR("Invalid signature");
}

ferr_t frsync_signature_load(frsync_signature_t *psig, void *pstream, frsync_read_fn_t read)
{
    if (!psig || !read)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    if (psig->is_ready)
        return FSUCCESS;

    uint32_t rsize = 0;

    do
    {
        rsize = read(psig, psig->in_buf + psig->in_size, sizeof psig->in_buf - psig->in_size);
        psig->in_size += rsize;

        rs_buffers_t buf = { 0 };
        buf.next_in = psig->in_buf;
        buf.avail_in = psig->in_size;
        buf.eof_in = rsize == 0;

        rs_result result = rs_job_iter(psig->job, &buf);

        if (result != RS_BLOCKED
            && result != RS_DONE)
            return FFAIL;

        if (buf.avail_in)
            memmove(psig->in_buf, psig->in_buf + psig->in_size - buf.avail_in, buf.avail_in);
        psig->in_size = buf.avail_in;
    }
    while (rsize);

    return rs_build_hash_table(psig->sumset) == RS_DONE ? FSUCCESS : FFAIL;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Delta creation
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_delta
{
    volatile uint32_t   ref_counter;
    rs_job_t           *job;
    uint32_t            in_size;
    char                in_buf[FRBUF_SIZE];
    uint32_t            out_size;
    char                out_buf[FRBUF_SIZE];
};

frsync_delta_t *frsync_delta_create()
{
/*
    frsync_sig_t *psig = malloc(sizeof(frsync_sig_t));
    if (!psig)
    {
        FS_ERR("Unable to allocate memory for signature calculator");
        return 0;
    }
    memset(psig, 0, sizeof *psig);

    psig->ref_counter = 1;

    psig->job = rs_delta_begin(...);

    if (!psig->job)
    {
        FS_ERR("Unable to create job for signature calculation");
        frsync_sig_release(psig);
        return 0;
    }

    return psig;
*/
}

frsync_delta_t *frsync_delta_retain(frsync_delta_t *psig)
{
    
}

void frsync_delta_release(frsync_delta_t *psig)
{
    
}

ferr_t frsync_delta_calc(frsync_delta_t *psig, void *pstream, frsync_read_fn_t read, frsync_write_fn_t write)
{
    
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Synchronization algorithm
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync
{
    volatile uint32_t   ref_counter;
    fmsgbus_t          *msgbus;

    volatile bool       is_sync_active;
    pthread_t           sync_thread;
    sem_t               sync_sem;
};

static void *frsync_thread(void *param)
{
    frsync_t *psync = (frsync_t*)param;
    psync->is_sync_active = true;

    while(psync->is_sync_active)
    {
    }

    return 0;
}

static void frsync_msgbus_retain(frsync_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
//    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,          (fmsg_handler_t)fsync_status_handler,            psync);
//    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST,      (fmsg_handler_t)fsync_sync_files_list_handler,   psync);
//    fmsgbus_subscribe(psync->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)fsync_file_part_request_handler, psync);
}

static void frsync_msgbus_release(frsync_t *psync)
{
//    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,        (fmsg_handler_t)fsync_status_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST,    (fmsg_handler_t)fsync_sync_files_list_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)fsync_file_part_request_handler);
    fmsgbus_release(psync->msgbus);
}

frsync_t *frsync_create(fmsgbus_t *pmsgbus)
{
    if (!pmsgbus)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    frsync_t *psync = malloc(sizeof(frsync_t));
    if (!psync)
    {
        FS_ERR("Unable to allocate memory for rsync");
        return 0;
    }
    memset(psync, 0, sizeof *psync);

    psync->ref_counter = 1;

    frsync_msgbus_retain(psync, pmsgbus);

    if (sem_init(&psync->sync_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        frsync_release(psync);
        return 0;
    }

    int rc = pthread_create(&psync->sync_thread, 0, frsync_thread, (void*)psync);
    if (rc)
    {
        FS_ERR("Unable to create the thread for rsync. Error: %d", rc);
        frsync_release(psync);
        return 0;
    }

    while(!psync->is_sync_active)
        nanosleep(&F1_SEC, NULL);

    return psync;
}

frsync_t *frsync_retain(frsync_t *psync)
{
    if (psync)
        psync->ref_counter++;
    else
        FS_ERR("Invalid rsync");
    return psync;
}

void frsync_release(frsync_t *psync)
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

            sem_destroy(&psync->sync_sem);
            frsync_msgbus_release(psync);
            memset(psync, 0, sizeof *psync);
            free(psync);
        }
    }
    else
        FS_ERR("Invalid rsync");
}
