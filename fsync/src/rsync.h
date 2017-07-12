#ifndef RSYNC_H_FSYNC
#define RSYNC_H_FSYNC
#include <futils/errno.h>
#include <futils/stream.h>
#include <stdint.h>
#include <stdbool.h>

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

typedef struct frsync frsync_t;

frsync_t *frsync_create(fistream_t *src, fostream_t *dst);
frsync_t *frsync_retain(frsync_t *psync);
void      frsync_release(frsync_t *psync);
bool      frsync_update(frsync_t *psync);

#endif
