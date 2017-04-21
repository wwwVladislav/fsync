#include "statuses.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>

bool fdb_statuses_map_open(fdb_transaction_t *transaction, char const *tbl, fdb_map_t *pmap)
{
    if (!transaction || !tbl || !pmap)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    if (!fdb_map_open(transaction, tbl, FDB_MAP_CREATE | FDB_MAP_INTEGERKEY | FDB_MAP_MULTI, pmap))
    {
        FS_ERR("Map wasn't created");
        return false;
    }

    return true;
}

bool fdb_statuses_map_put(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t const *value)
{
    if (!pmap || !transaction || !value)
        return false;

    for(uint32_t i = 0; i < sizeof status * 8; ++i)
    {
        uint32_t mask = status & (1 << i);
        if (mask)
        {
            fdb_data_t const key = { sizeof mask, &mask };
            if (!fdb_map_put(pmap, transaction, &key, value))
                return false;
        }
    }

    return true;
}

bool fdb_statuses_map_get(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t *value)
{
    if (!pmap || !transaction || !value)
        return false;

    for(uint32_t i = 0; i < sizeof status * 8; ++i)
    {
        uint32_t mask = status & (1 << i);
        if (mask)
        {
            fdb_data_t const key = { sizeof mask, &mask };
            if (fdb_map_get(pmap, transaction, &key, value))
                return true;
        }
    }

    return false;
}

bool fdb_statuses_map_del(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status, fdb_data_t const *value)
{
    if (!pmap || !transaction || !value)
        return false;

    for(uint32_t i = 0; i < sizeof status * 8; ++i)
    {
        uint32_t mask = status & (1 << i);
        if (mask)
        {
            fdb_data_t const key = { sizeof mask, &mask };
            if (!fdb_map_del(pmap, transaction, &key, value))
                return false;
        }
    }

    return true;
}
