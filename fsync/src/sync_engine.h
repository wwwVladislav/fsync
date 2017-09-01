#ifndef SYNC_ENGINE_H_FSYNC
#define SYNC_ENGINE_H_FSYNC
#include <futils/msgbus.h>
#include <futils/uuid.h>
#include <futils/errno.h>
#include <futils/stream.h>
#include <stdint.h>
#include <binn.h>

typedef struct sync_engine    fsync_engine_t;
typedef struct fsync_listener fsync_listener_t;

typedef fsync_listener_t* (*fsync_listener_retain_fn_t) (fsync_listener_t *);
typedef void              (*fsync_listener_release_fn_t)(fsync_listener_t *);
typedef bool              (*fsync_listener_accept_fn_t) (fsync_listener_t *, binn *metainf);

struct fsync_listener
{
    uint32_t                    id;
    fsync_listener_retain_fn_t  retain;
    fsync_listener_release_fn_t release;
    fsync_listener_accept_fn_t  accept;
};

fsync_engine_t *fsync_engine(fmsgbus_t *pmsgbus, fuuid_t const *uuid);
fsync_engine_t *fsync_engine_retain(fsync_engine_t *pengine);
void            fsync_engine_release(fsync_engine_t *pengine);
ferr_t          fsync_engine_register_listener(fsync_engine_t *pengine, fsync_listener_t *listener);
ferr_t          fsync_engine_sync(fsync_engine_t *pengine, fuuid_t const *dst, uint32_t listener_id, binn *metainf, fistream_t *pstream);  // pdata -> dst

#endif
