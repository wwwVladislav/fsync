#ifndef FSYNC_DOWNLOAD_INFO_H_FDB
#define FSYNC_DOWNLOAD_INFO_H_FDB
#include <futils/uuid.h>
#include "../db.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct fdinf_map fdinf_map_t;

fdinf_map_t *fdinf(fdb_transaction_t *transaction, fuuid_t const *uuid);
fdinf_map_t *fdinf_retain(fdinf_map_t *pmap);
void fdinf_release(fdinf_map_t *pmap);
bool fdinf_received_size(fdinf_map_t *pmap, fdb_transaction_t *transaction, uint32_t id, uint64_t *size);
bool fdinf_received_size_update(fdinf_map_t *pmap, fdb_transaction_t *transaction, uint32_t id, uint64_t size);

#endif
