#ifndef FSYNC_STATUSES_H_FDB
#define FSYNC_STATUSES_H_FDB
#include "../db.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct fdb_statuses_map_iterator fdb_statuses_map_iterator_t;

bool fdb_statuses_map_open(fdb_transaction_t *transaction, char const *tbl, fdb_map_t *pmap);
bool fdb_statuses_map_put(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t const *value);
bool fdb_statuses_map_get(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t *value);
bool fdb_statuses_map_del(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t const *value);

fdb_statuses_map_iterator_t *fdb_statuses_map_iterator(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status);
void                         fdb_statuses_map_iterator_free(fdb_statuses_map_iterator_t *);
bool                         fdb_statuses_map_iterator_first(fdb_statuses_map_iterator_t *, fdb_data_t *value);
bool                         fdb_statuses_map_iterator_next(fdb_statuses_map_iterator_t *, fdb_data_t *value);

#endif
