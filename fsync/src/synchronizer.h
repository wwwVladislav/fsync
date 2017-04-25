#ifndef SYNCHRONIZER_H_FSYNC
#define SYNCHRONIZER_H_FSYNC
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <fdb/db.h>
#include <stdbool.h>

typedef struct fsynchronizer fsynchronizer_t;

fsynchronizer_t *fsynchronizer_create(fmsgbus_t *pmsgbus, fdb_t *db, fuuid_t const *uuid);
void             fsynchronizer_free(fsynchronizer_t *);
bool             fsynchronizer_update(fsynchronizer_t *);

#endif
