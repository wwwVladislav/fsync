#ifndef SEARCH_ENGINE_H_FSYNC
#define SEARCH_ENGINE_H_FSYNC
#include <futils/msgbus.h>
#include <futils/uuid.h>
#include <fdb/db.h>
#include <stdbool.h>

typedef struct search_engine fsearch_engine_t;

fsearch_engine_t *fsearch_engine(fmsgbus_t *pmsgbus, fdb_t *db, fuuid_t const *uuid);
fsearch_engine_t *fsearch_engine_retain(fsearch_engine_t *pengine);
void              fsearch_engine_release(fsearch_engine_t *pengine);
bool              fsearch_engine_add_dir(fsearch_engine_t *pengine, char const *dir);

#endif
