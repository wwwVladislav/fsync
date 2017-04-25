#ifndef SYNC_H_FSYNC
#define SYNC_H_FSYNC
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <fdb/db.h>

typedef struct fsync fsync_t;

fsync_t *fsync_create(fmsgbus_t *pmsgbus, fdb_t *db, char const *dir, fuuid_t const *uuid);
void     fsync_free(fsync_t *);

#endif
