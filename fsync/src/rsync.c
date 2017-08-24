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

enum
{
    FRBUF_SIZE = 8 * 1024
};

typedef struct
{
    rs_job_t           *job;
    uint32_t            in_size;
    char                in_buf[FRBUF_SIZE];
    uint32_t            out_size;
    char                out_buf[FRBUF_SIZE];
} frsync_iojob_t;

static ferr_t frsync_iojob_do(frsync_iojob_t *io_job, fistream_t *pistream, fostream_t *postream)
{
    uint32_t rsize = 0;

    do
    {
        size_t const read_size = sizeof io_job->in_buf - io_job->in_size;
        rsize = pistream->read(pistream, io_job->in_buf + io_job->in_size, read_size);
        io_job->in_size += rsize;

        rs_buffers_t buf = { 0 };
        buf.next_in = io_job->in_buf;
        buf.avail_in = io_job->in_size;
        buf.eof_in = rsize < read_size || rsize == 0;
        buf.next_out = io_job->out_buf;
        buf.avail_out = sizeof io_job->out_buf;

        rs_result result = rs_job_iter(io_job->job, &buf);

        if (result != RS_INPUT_ENDED
            && result != RS_BLOCKED
            && result != RS_DONE)
            return FFAIL;

        size_t sig_size = buf.next_out - io_job->out_buf;
        while(sig_size)
        {
            uint32_t write_size = postream->write(postream, io_job->out_buf, sig_size);
            if (!write_size)
                return FFAIL;
            sig_size -= write_size;
        }

        if (buf.avail_in)
            memmove(io_job->in_buf, io_job->in_buf + io_job->in_size - buf.avail_in, buf.avail_in);
        io_job->in_size = buf.avail_in;
    }
    while (rsize);

    return FSUCCESS;
}

typedef struct
{
    rs_job_t           *job;
    uint32_t            in_size;
    char                in_buf[FRBUF_SIZE];
} frsync_ijob_t;

static ferr_t frsync_ijob_do(frsync_ijob_t *i_job, fistream_t *pistream)
{
    uint32_t rsize = 0;

    do
    {
        size_t const read_size = sizeof i_job->in_buf - i_job->in_size;
        rsize = pistream->read(pistream, i_job->in_buf + i_job->in_size, read_size);
        i_job->in_size += rsize;

        rs_buffers_t buf = { 0 };
        buf.next_in = i_job->in_buf;
        buf.avail_in = i_job->in_size;
        buf.eof_in = rsize < read_size || rsize == 0;

        rs_result result = rs_job_iter(i_job->job, &buf);

        if (result != RS_BLOCKED
            && result != RS_DONE)
            return FFAIL;

        if (buf.avail_in)
            memmove(i_job->in_buf, i_job->in_buf + i_job->in_size - buf.avail_in, buf.avail_in);
        i_job->in_size = buf.avail_in;
    }
    while (rsize);

    return FSUCCESS;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Signature calculation
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_signature_calculator
{
    volatile uint32_t   ref_counter;
    frsync_iojob_t      io_job;
};

frsync_signature_calculator_t *frsync_signature_calculator_create()
{
    frsync_signature_calculator_t *psig = malloc(sizeof(frsync_signature_calculator_t));
    if (!psig)
    {
        FS_ERR("Unable to allocate memory for signature calculator");
        return 0;
    }
    memset(psig, 0, sizeof *psig);

    psig->ref_counter = 1;

    psig->io_job.job = rs_sig_begin(RS_DEFAULT_BLOCK_LEN, 0, RS_BLAKE2_SIG_MAGIC);

    if (!psig->io_job.job)
    {
        FS_ERR("Unable to create job for signature calculation");
        frsync_signature_calculator_release(psig);
        return 0;
    }

    return psig;
}

frsync_signature_calculator_t *frsync_signature_calculator_retain(frsync_signature_calculator_t *psig)
{
    if (psig)
        psig->ref_counter++;
    else
        FS_ERR("Invalid signature calculator");
    return psig;
}

void frsync_signature_calculator_release(frsync_signature_calculator_t *psig)
{
    if (psig)
    {
        if (!psig->ref_counter)
            FS_ERR("Invalid signature calculator");
        else if (!--psig->ref_counter)
        {
            if (psig->io_job.job)
                rs_job_free(psig->io_job.job);
            memset(psig, 0, sizeof *psig);
            free(psig);
        }
    }
    else
        FS_ERR("Invalid signature calculator");
}

ferr_t frsync_signature_calculate(frsync_signature_calculator_t *psig, fistream_t *pbase_stream, fostream_t *psignature_ostream)
{
    if (!psig || !pbase_stream || !psignature_ostream)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    return frsync_iojob_do(&psig->io_job, pbase_stream, psignature_ostream);
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Signature data
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_signature
{
    volatile uint32_t   ref_counter;
    rs_signature_t     *sumset;
    bool                is_ready;
    frsync_ijob_t       ijob;
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

    psig->ijob.job = rs_loadsig_begin(&psig->sumset);

    if (!psig->ijob.job)
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
            if (psig->ijob.job)
                rs_job_free(psig->ijob.job);
            if (psig->sumset)
                rs_free_sumset(psig->sumset);
            memset(psig, 0, sizeof *psig);
            free(psig);
        }
    }
    else
        FS_ERR("Invalid signature");
}

ferr_t frsync_signature_load(frsync_signature_t *psig, fistream_t *psignature_istream)
{
    if (!psig || !psignature_istream)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    if (psig->is_ready)
        return FSUCCESS;

    if (frsync_ijob_do(&psig->ijob, psignature_istream) == FSUCCESS)
        psig->is_ready = rs_build_hash_table(psig->sumset) == RS_DONE;

    return psig->is_ready ? FSUCCESS : FFAIL;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Delta calculation
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_delta_calculator
{
    volatile uint32_t   ref_counter;
    frsync_signature_t *psig;
    frsync_iojob_t      io_job;
};

frsync_delta_calculator_t *frsync_delta_calculator_create(frsync_signature_t *psig)
{
    frsync_delta_calculator_t *pdelta = malloc(sizeof(frsync_delta_calculator_t));
    if (!pdelta)
    {
        FS_ERR("Unable to allocate memory for delta calculator");
        return 0;
    }
    memset(pdelta, 0, sizeof *pdelta);

    pdelta->ref_counter = 1;
    pdelta->psig = frsync_signature_retain(psig);

    pdelta->io_job.job = rs_delta_begin(psig->sumset);

    if (!pdelta->io_job.job)
    {
        FS_ERR("Unable to create job for delta calculation");
        frsync_delta_calculator_release(pdelta);
        return 0;
    }

    return pdelta;
}

frsync_delta_calculator_t *frsync_delta_calculator_retain(frsync_delta_calculator_t *pdelta)
{
    if (pdelta)
        pdelta->ref_counter++;
    else
        FS_ERR("Invalid delta calculator");
    return pdelta;
}

void frsync_delta_calculator_release(frsync_delta_calculator_t *pdelta)
{
    if (pdelta)
    {
        if (!pdelta->ref_counter)
            FS_ERR("Invalid delta calculator");
        else if (!--pdelta->ref_counter)
        {
            if (pdelta->io_job.job)
                rs_job_free(pdelta->io_job.job);
            frsync_signature_release(pdelta->psig);
            memset(pdelta, 0, sizeof *pdelta);
            free(pdelta);
        }
    }
    else
        FS_ERR("Invalid delta calculator");
}

ferr_t frsync_delta_calculate(frsync_delta_calculator_t *pdelta, fistream_t *pistream, fostream_t *pdelta_ostream)
{
    if (!pdelta || !pistream || !pdelta_ostream)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    return frsync_iojob_do(&pdelta->io_job, pistream, pdelta_ostream);
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Delta apply
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_delta
{
    volatile uint32_t   ref_counter;
    fistream_t         *pbase_stream;
    frsync_iojob_t      io_job;
};

static rs_result frsync_copy_cb(void *arg, rs_long_t pos, size_t *len, void **buf)
{
    fistream_t *pbase_stream = (fistream_t *)arg;

    if (!pbase_stream->seek(pbase_stream, pos))
        return RS_IO_ERROR;

    size_t const read_size = pbase_stream->read(pbase_stream, *buf, *len);
    if (!read_size)
    {
        *len = 0;
        return RS_INPUT_ENDED;
    }

    *len = read_size;

    return RS_DONE;
}

frsync_delta_t *frsync_delta_create(fistream_t *pbase_stream)
{
    frsync_delta_t *pdelta = malloc(sizeof(frsync_delta_t));
    if (!pdelta)
    {
        FS_ERR("Unable to allocate memory for delta");
        return 0;
    }
    memset(pdelta, 0, sizeof *pdelta);

    pdelta->ref_counter = 1;
    pdelta->pbase_stream = pbase_stream->retain(pbase_stream);

    pdelta->io_job.job = rs_patch_begin(frsync_copy_cb, pbase_stream);

    if (!pdelta->io_job.job)
    {
        FS_ERR("Unable to create job for delta");
        frsync_delta_release(pdelta);
        return 0;
    }

    return pdelta;
}

frsync_delta_t *frsync_delta_retain(frsync_delta_t *pdelta)
{
    if (pdelta)
        pdelta->ref_counter++;
    else
        FS_ERR("Invalid delta");
    return pdelta;
}

void frsync_delta_release(frsync_delta_t *pdelta)
{
    if (pdelta)
    {
        if (!pdelta->ref_counter)
            FS_ERR("Invalid delta");
        else if (!--pdelta->ref_counter)
        {
            if (pdelta->io_job.job)
                rs_job_free(pdelta->io_job.job);
            pdelta->pbase_stream->release(pdelta->pbase_stream);
            memset(pdelta, 0, sizeof *pdelta);
            free(pdelta);
        }
    }
    else
        FS_ERR("Invalid delta");
}

ferr_t frsync_delta_apply(frsync_delta_t *pdelta, fistream_t *pdelta_istream, fostream_t *pnew_ostream)
{
    if (!pdelta || !pdelta_istream || !pnew_ostream)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    return frsync_iojob_do(&pdelta->io_job, pdelta_istream, pnew_ostream);
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Synchronization algorithm
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct frsync_client
{
    volatile uint32_t   ref_counter;
    fmsgbus_t          *msgbus;
    fuuid_t             dst_uuid;
    fuuid_t             src_uuid;
    fistream_t         *src;
};

struct frsync_server
{
    volatile uint32_t   ref_counter;
};

static void frsync_client_msgbus_retain(frsync_client_t *psync, fmsgbus_t *pmsgbus)
{
    psync->msgbus = fmsgbus_retain(pmsgbus);
//    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,          (fmsg_handler_t)fsync_status_handler,            psync);
//    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST,      (fmsg_handler_t)fsync_sync_files_list_handler,   psync);
//    fmsgbus_subscribe(psync->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)fsync_file_part_request_handler, psync);
}

static void frsync_client_msgbus_release(frsync_client_t *psync)
{
//    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,        (fmsg_handler_t)fsync_status_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST,    (fmsg_handler_t)fsync_sync_files_list_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)fsync_file_part_request_handler);
    fmsgbus_release(psync->msgbus);
}

frsync_client_t *frsync_client_snd(fmsgbus_t *pmsgbus, frsync_src_t *src, fuuid_t const *dst)
{
    if (!pmsgbus || !src || !dst)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    frsync_client_t *psync = malloc(sizeof(frsync_client_t));
    if (!psync)
    {
        FS_ERR("Unable to allocate memory for rsync");
        return 0;
    }
    memset(psync, 0, sizeof *psync);

    psync->ref_counter = 1;
    psync->dst_uuid = *dst;
    psync->src_uuid = src->uuid;
    psync->src = src->src->retain(src->src);

    frsync_client_msgbus_retain(psync, pmsgbus);

    return psync;
}

frsync_client_t *frsync_client_rcv(fmsgbus_t *pmsgbus, frsync_dst_t *dst, fuuid_t const *src)
{
    FS_ERR("TODO: Not implemented");
    return 0;
}

frsync_client_t *frsync_client_retain(frsync_client_t *psync)
{
    if (psync)
        psync->ref_counter++;
    else
        FS_ERR("Invalid rsync client");
    return psync;
}

void frsync_client_release(frsync_client_t *psync)
{
    if (psync)
    {
        if (!psync->ref_counter)
            FS_ERR("Invalid rsync client");
        else if (!--psync->ref_counter)
        {
            frsync_client_msgbus_release(psync);
            psync->src->release(psync->src);
            memset(psync, 0, sizeof *psync);
            free(psync);
        }
    }
    else
        FS_ERR("Invalid rsync client");
}
