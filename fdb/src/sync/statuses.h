#ifndef FSYNC_STATUSES_H_FDB
#define FSYNC_STATUSES_H_FDB
#include "../db.h"
#include <stdint.h>
#include <stdbool.h>

bool fdb_statuses_map_open(fdb_transaction_t *transaction, char const *tbl, fdb_map_t *pmap);
bool fdb_statuses_map_put(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t const *value);
bool fdb_statuses_map_get(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t *value);
bool fdb_statuses_map_del(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t const *value);

#endif
