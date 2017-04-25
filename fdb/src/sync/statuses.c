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

struct fdb_statuses_map_iterator
{
    fdb_transaction_t *transaction;
    fdb_cursor_t       cursor;
};

fdb_statuses_map_iterator_t *fdb_statuses_map_iterator(fdb_map_t *pmap, fdb_transaction_t *transaction, uint32_t status)
{
    if (!pmap || !transaction || !status)
        return 0;

    fdb_statuses_map_iterator_t *piterator = malloc(sizeof(fdb_statuses_map_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory for iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->transaction = transaction;

    if (!fdb_cursor_open(pmap, transaction, &piterator->cursor))
    {
        fdb_statuses_map_iterator_free(piterator);
        return 0;
    }

    return piterator;
}

void fdb_statuses_map_iterator_free(fdb_statuses_map_iterator_t *piterator)
{
    if (piterator)
    {
        fdb_cursor_close(&piterator->cursor);
        free(piterator);
    }
}

bool fdb_statuses_map_iterator_first(fdb_statuses_map_iterator_t *piterator, fdb_data_t *value)
{
    if (!piterator || !value)
        return false;

    fdb_data_t key = { 0 };

    bool first = true;

    while (fdb_cursor_get(&piterator->cursor, &key, value, first ? FDB_FIRST : FDB_NEXT))
    {
        for(uint32_t i = 0; i < sizeof(uint32_t) * 8; ++i)
        {
            uint32_t status = *(uint32_t const*)key.data;
            uint32_t mask = status & (1 << i);
            if (mask)
                return true;
        }

        first = false;
    }

    return false;
}

bool fdb_statuses_map_iterator_next(fdb_statuses_map_iterator_t *piterator, fdb_data_t *value)
{
    if (!piterator || !value)
        return false;

    fdb_data_t key = { 0 };

    while (fdb_cursor_get(&piterator->cursor, &key, value, FDB_NEXT))
    {
        for(uint32_t i = 0; i < sizeof(uint32_t) * 8; ++i)
        {
            uint32_t status = *(uint32_t const*)key.data;
            uint32_t mask = status & (1 << i);
            if (mask)
                return true;
        }
    }

    return false;
}
