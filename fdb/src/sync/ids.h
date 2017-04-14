#ifndef FSYNC_IDS_H_FDB
#define FSYNC_IDS_H_FDB
#include "../db.h"
#include <stdint.h>
#include <stdbool.h>

#define FINVALID_ID (~0u)

bool fdb_ids_map_open(fdb_transaction_t *transaction, char const *tbl, fdb_map_t *pmap);
bool fdb_id_generate(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t *id);
bool fdb_id_free(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t id);

#endif
