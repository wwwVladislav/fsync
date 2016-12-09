#ifndef SYNC_H_FSYNC
#define SYNC_H_FSYNC
#include <stdbool.h>

typedef struct fsync fsync_t;

fsync_t *fsync_create(char const *);
void     fsync_free(fsync_t *);

#endif
