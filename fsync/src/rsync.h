#ifndef RSYNC_H_FSYNC
#define RSYNC_H_FSYNC
#include <futils/errno.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <futils/stream.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * base  ------signature--------> data
 *       <-------delta-----------
 * data  <======================> data
 */

typedef struct frsync_signature_calculator frsync_signature_calculator_t;

frsync_signature_calculator_t *frsync_signature_calculator_create();
frsync_signature_calculator_t *frsync_signature_calculator_retain(frsync_signature_calculator_t *psig);
void                           frsync_signature_calculator_release(frsync_signature_calculator_t *psig);
ferr_t                         frsync_signature_calculate(frsync_signature_calculator_t *psig, fistream_t *pbase_stream, fostream_t *psignature_ostream);

typedef struct frsync_signature frsync_signature_t;

frsync_signature_t            *frsync_signature_create();
frsync_signature_t            *frsync_signature_retain(frsync_signature_t *psig);
void                           frsync_signature_release(frsync_signature_t *psig);
ferr_t                         frsync_signature_load(frsync_signature_t *psig, fistream_t *psignature_istream);

typedef struct frsync_delta_calculator frsync_delta_calculator_t;

frsync_delta_calculator_t     *frsync_delta_calculator_create(frsync_signature_t *psig);
frsync_delta_calculator_t     *frsync_delta_calculator_retain(frsync_delta_calculator_t *pdelta);
void                           frsync_delta_calculator_release(frsync_delta_calculator_t *pdelta);
ferr_t                         frsync_delta_calculate(frsync_delta_calculator_t *pdelta, fistream_t *pistream, fostream_t *pdelta_ostream);

typedef struct frsync_delta frsync_delta_t;

frsync_delta_t                *frsync_delta_create(fistream_t *pbase_stream);
frsync_delta_t                *frsync_delta_retain(frsync_delta_t *pdelta);
void                           frsync_delta_release(frsync_delta_t *pdelta);
ferr_t                         frsync_delta_apply(frsync_delta_t *pdelta, fistream_t *pdelta_istream, fostream_t *pnew_ostream);

typedef struct frsync_client frsync_client_t;
typedef struct frsync_server frsync_server_t;

typedef struct
{
    fuuid_t     uuid;               // current UUID
    fistream_t *src;                // local src data stream (file.txt)
    fostream_t *dst;                // local dst data stream (file.txt.tmp)
} frsync_dst_t;

typedef struct
{
    fuuid_t     uuid;               // current UUID
    fistream_t *src;                // local src data stream (file.txt)
} frsync_src_t;

frsync_client_t *frsync_client_snd(fmsgbus_t *pmsgbus, frsync_src_t *src, fuuid_t const *dst);
frsync_client_t *frsync_client_rcv(fmsgbus_t *pmsgbus, frsync_dst_t *dst, fuuid_t const *src);
frsync_client_t *frsync_client_retain(frsync_client_t *psync);
void             frsync_client_release(frsync_client_t *psync);

frsync_server_t *frsync_server(fmsgbus_t *pmsgbus);
frsync_server_t *frsync_server_retain(frsync_server_t *psync);
void             frsync_server_release(frsync_server_t *psync);

#endif
