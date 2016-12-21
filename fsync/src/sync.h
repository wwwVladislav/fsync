#ifndef SYNC_H_FSYNC
#define SYNC_H_FSYNC
#include <stdbool.h>
#include <futils/uuid.h>

typedef struct fsync fsync_t;

fsync_t *fsync_create(char const *dir, fuuid_t const *uuid);
void     fsync_free(fsync_t *);

#endif
