#include "ids.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>

static char FLD_USED[4] = "used";
static char FLD_FREE[4] = "free";

bool fdb_ids_map_open(fdb_transaction_t *transaction, char const *tbl, fdb_map_t *pmap)
{
    if (!transaction || !tbl || !pmap)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    if (!fdb_map_open(transaction, tbl, FDB_MAP_CREATE | FDB_MAP_MULTI | FDB_MAP_FIXED_SIZE_VALUE | FDB_MAP_INTEGERVAL, pmap))
    {
        FS_ERR("Map wasn't created");
        return false;
    }

    return true;
}

bool fdb_id_generate(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t *id)
{
    if (!pmap || !transaction || !id)
        return false;

    fdb_data_t const used_key = { sizeof FLD_USED, FLD_USED };
    fdb_data_t const free_key = { sizeof FLD_FREE, FLD_FREE };
    fdb_data_t value = { 0 };

    if (fdb_map_get(pmap, transaction, &free_key, &value))
    {
        *id = *(uint32_t*)value.data;
        if (fdb_map_del(pmap, transaction, &free_key, &value))
            return fdb_map_put(pmap, transaction, &used_key, &value);
    }
    else
    {
        uint32_t last_id = FINVALID_ID;

        fdb_cursor_t cursor = { 0 };
        if (fdb_cursor_open(pmap, transaction, &cursor))
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

        return fdb_map_put(pmap, transaction, &used_key, &value);
    }

    return false;
}

bool fdb_id_free(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t id)
{
    if (!pmap || !transaction)
        return false;

    fdb_data_t const used_key = { sizeof FLD_USED, FLD_USED };
    fdb_data_t const free_key = { sizeof FLD_FREE, FLD_FREE };
    fdb_data_t value = { sizeof id, &id };

    if (fdb_map_del(pmap, transaction, &used_key, &value))
        return fdb_map_put(pmap, transaction, &free_key, &value);
    return false;
}
