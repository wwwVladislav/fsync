#ifndef SYNC_H_FSYNC
#define SYNC_H_FSYNC
#include <stdbool.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>

typedef struct fsync fsync_t;

fsync_t *fsync_create(fmsgbus_t *pmsgbus, char const *dir, fuuid_t const *uuid);
void     fsync_free(fsync_t *);

#endif
