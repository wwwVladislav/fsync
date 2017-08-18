#ifndef SYNC_ENGINE_H_FSYNC
#define SYNC_ENGINE_H_FSYNC
#include <futils/msgbus.h>
#include <futils/uuid.h>

typedef struct sync_engine fsync_engine_t;

fsync_engine_t *fsync_engine(fmsgbus_t *pmsgbus, fuuid_t const *uuid);
fsync_engine_t *fsync_engine_retain(fsync_engine_t *pengine);
void            fsync_engine_release(fsync_engine_t *pengine);

#endif
