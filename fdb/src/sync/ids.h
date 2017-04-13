#ifndef FSYNC_IDS_H_FDB
#define FSYNC_IDS_H_FDB
#include "../db.h"
#include <stdint.h>
#include <stdbool.h>

#define FINVALID_ID (~0u)

typedef struct fdb_ids_transaction fdb_ids_transaction_t;

fdb_ids_transaction_t *fdb_ids_transaction_start(fdb_t *pdb, char const *tbl);
void fdb_ids_transaction_commit(fdb_ids_transaction_t *transaction);
void fdb_ids_transaction_abort(fdb_ids_transaction_t *transaction);
bool fdb_id_generate(fdb_ids_transaction_t *transaction, uint32_t *id);
bool fdb_id_free(fdb_ids_transaction_t *transaction, uint32_t id);

#endif
