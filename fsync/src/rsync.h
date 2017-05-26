#ifndef RSYNC_H_FSYNC
#define RSYNC_H_FSYNC
#include <futils/msgbus.h>
#include <futils/errno.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t (*frsync_read_fn_t)(void *, char *, uint32_t);
typedef uint32_t (*frsync_write_fn_t)(void *, char const *, uint32_t);

typedef struct frsync_sig_calculator frsync_sig_calculator_t;

frsync_sig_calculator_t *frsync_sig_calculator_create();
frsync_sig_calculator_t *frsync_sig_calculator_retain(frsync_sig_calculator_t *psig);
void                     frsync_sig_calculator_release(frsync_sig_calculator_t *psig);
ferr_t                   frsync_sig_calculate(frsync_sig_calculator_t *psig, void *pstream, frsync_read_fn_t read, frsync_write_fn_t write);

typedef struct frsync_signature frsync_signature_t;

frsync_signature_t *frsync_signature_create();
frsync_signature_t *frsync_signature_retain(frsync_signature_t *psig);
void                frsync_signature_release(frsync_signature_t *psig);
ferr_t              frsync_signature_load(frsync_signature_t *psig, void *pstream, frsync_read_fn_t read);

typedef struct frsync_delta frsync_delta_t;

frsync_delta_t *frsync_delta_create();
frsync_delta_t *frsync_delta_retain(frsync_delta_t *psig);
void            frsync_delta_release(frsync_delta_t *psig);
ferr_t          frsync_delta_calc(frsync_delta_t *psig, void *pstream, frsync_read_fn_t read, frsync_write_fn_t write);

typedef struct frsync frsync_t;

frsync_t *frsync_create(fmsgbus_t *pmsgbus);
frsync_t *frsync_retain(frsync_t *psync);
void      frsync_release(frsync_t *psync);

#endif
