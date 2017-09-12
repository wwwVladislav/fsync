#ifndef SYNC_ENGINE_H_FSYNC
#define SYNC_ENGINE_H_FSYNC
#include <futils/msgbus.h>
#include <futils/uuid.h>
#include <futils/errno.h>
#include <futils/stream.h>
#include <stdint.h>
#include <binn.h>

typedef struct sync_engine   fsync_engine_t;
typedef struct fsync_agent   fsync_agent_t;

typedef fsync_agent_t* (*fsync_agent_retain_fn_t)      (fsync_agent_t *);
typedef void           (*fsync_agent_release_fn_t)     (fsync_agent_t *);
typedef bool           (*fsync_agent_accept_fn_t)      (fsync_agent_t *, binn *metainf, fistream_t **pistream, fostream_t **postream);
typedef void           (*fsync_error_handler_fn_t)     (fsync_agent_t *, binn *metainf, ferr_t err, char const *err_msg);
typedef void           (*fsync_completion_handler_fn_t)(fsync_agent_t *, binn *metainf);

struct fsync_agent
{
    uint32_t                        id;
    fsync_agent_retain_fn_t         retain;
    fsync_agent_release_fn_t        release;
    fsync_agent_accept_fn_t         accept;
    fsync_error_handler_fn_t        failed;
    fsync_completion_handler_fn_t   comple;
};

fsync_engine_t *fsync_engine(fmsgbus_t *pmsgbus, fuuid_t const *uuid);
fsync_engine_t *fsync_engine_retain(fsync_engine_t *pengine);
void            fsync_engine_release(fsync_engine_t *pengine);
ferr_t          fsync_engine_register_agent(fsync_engine_t *pengine, fsync_agent_t *agent);
ferr_t          fsync_engine_sync(fsync_engine_t *pengine, fuuid_t const *dst, uint32_t agent_id, binn *metainf, fistream_t *pstream);  // data -> dst

#endif
