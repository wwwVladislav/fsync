#include "ids.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>

static char FLD_USED[4] = "used";
static char FLD_FREE[4] = "free";

struct fdb_ids_transaction
{
    fdb_t             *pdb;
    fdb_transaction_t  transaction;
    fdb_map_t          map;
};

fdb_ids_transaction_t *fdb_ids_transaction_start(fdb_t *pdb, char const *tbl)
{
    if (!pdb || !tbl)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_ids_transaction_t *transaction = malloc(sizeof(fdb_ids_transaction_t));
    if (!transaction)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(transaction, 0, sizeof *transaction);
    transaction->pdb = fdb_retain(pdb);
    if (!fdb_transaction_start(pdb, &transaction->transaction))
    {
        FS_ERR("Transaction wasn't started");
        fdb_ids_transaction_abort(transaction);
        return 0;
    }

    if (!fdb_map_open(&transaction->transaction, tbl, FDB_MAP_CREATE | FDB_MAP_MULTI | FDB_MAP_FIXED_SIZE_VALUE | FDB_MAP_INTEGERVAL, &transaction->map))
    {
        FS_ERR("Map wasn't created");
        fdb_ids_transaction_abort(transaction);
        return 0;
    }

    return transaction;
}

void fdb_ids_transaction_commit(fdb_ids_transaction_t *transaction)
{
    if (transaction)
    {
        fdb_transaction_commit(&transaction->transaction);
        fdb_map_close(&transaction->map);
        fdb_release(transaction->pdb);
        free(transaction);
    }
}

void fdb_ids_transaction_abort(fdb_ids_transaction_t *transaction)
{
    if (transaction)
    {
        fdb_transaction_abort(&transaction->transaction);
        fdb_map_close(&transaction->map);
        fdb_release(transaction->pdb);
        free(transaction);
    }
}

bool fdb_id_generate(fdb_ids_transaction_t *transaction, uint32_t *id)
{
    if (!transaction || !id)
        return false;

    fdb_data_t const used_key = { sizeof FLD_USED, FLD_USED };
    fdb_data_t const free_key = { sizeof FLD_FREE, FLD_FREE };
    fdb_data_t value = { 0 };

    if (fdb_map_get(&transaction->map, &transaction->transaction, &free_key, &value))
    {
        *id = *(uint32_t*)value.data;
        if (fdb_map_del(&transaction->map, &transaction->transaction, &free_key, &value))
            return fdb_map_put(&transaction->map, &transaction->transaction, &used_key, &value);
    }
    else
    {
        uint32_t last_id = FINVALID_ID;

        fdb_cursor_t cursor = { 0 };
        if (fdb_cursor_open(&transaction->map, &transaction->transaction, &cursor))
        {
            fdb_data_t key = { sizeof FLD_USED, FLD_USED };
            if (fdb_cursor_get(&cursor, &key, &value, FDB_SET))
            {
                if (fdb_cursor_get(&cursor, &key, &value, FDB_LAST_DUP))
                    last_id = *(uint32_t*)value.data;
            }
            fdb_cursor_close(&cursor);
        }

        *id = last_id == FINVALID_ID ? 0 : ++last_id;

        value.data = id;
        value.size = sizeof(*id);

        return fdb_map_put(&transaction->map, &transaction->transaction, &used_key, &value);
    }

    return false;
}

bool fdb_id_free(fdb_ids_transaction_t *transaction, uint32_t id)
{
    if (!transaction)
        return false;

    fdb_data_t const used_key = { sizeof FLD_USED, FLD_USED };
    fdb_data_t const free_key = { sizeof FLD_FREE, FLD_FREE };
    fdb_data_t value = { sizeof id, &id };

    if (fdb_map_del(&transaction->map, &transaction->transaction, &used_key, &value))
        return fdb_map_put(&transaction->map, &transaction->transaction, &free_key, &value);
    return false;
}
