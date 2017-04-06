#include "config.h"
#include <string.h>

static char const *TBL_CONFIG = "config";
static char const *CFG_UUID = "uuid";
static char const *CFG_ADDRESS = "address";

static bool fdb_map_put_value(fdb_map_t *pmap, fdb_transaction_t *transaction, char const *key, void const *value, size_t size)
{
    fdb_data_t const k = { strlen(key), (void*)key };
    fdb_data_t const v = { size, (void*)value };
    return fdb_map_put(pmap, transaction, &k, &v);
}

bool fdb_load_config(fdb_t *pdb, fconfig_t *config)
{
    if (!pdb || !config)
        return false;

    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pdb, &transaction))
    {
        fdb_map_t map = {0};
        if (fdb_map_open(&transaction, TBL_CONFIG, FDB_MAP_CREATE, &map))
        {
            fdb_cursor_t cursor = { 0 };
            if (fdb_cursor_open(&transaction, &map, &cursor))
            {
                fdb_data_t key, value;
                bool valid_uuid = false, valid_address = false;
                while (fdb_cursor_get(&cursor, &key, &value, FDB_NEXT))
                {
                    if (strncmp(CFG_UUID, (char const *)key.data, key.size))
                        memcpy(&config->uuid, value.data, sizeof config->uuid), valid_uuid = true;
                    else if (strncmp(CFG_ADDRESS, (char const *)key.data, key.size))
                        memcpy(config->address, value.data, value.size < FMAX_ADDR ? value.size : FMAX_ADDR), valid_address = true;
                }
                ret = valid_uuid && valid_address;
            }
            fdb_map_close(&map);
        }
        fdb_transaction_abort(&transaction);
    }

    return ret;
}

bool fdb_save_config(fdb_t *pdb, fconfig_t const *config)
{
    if (!pdb || !config)
        return false;

    bool ret = true;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pdb, &transaction))
    {
        fdb_map_t map = {0};
        if (fdb_map_open(&transaction, TBL_CONFIG, FDB_MAP_CREATE, &map))
        {
            ret &= fdb_map_put_value(&map, &transaction, CFG_UUID, &config->uuid, sizeof config->uuid);
            ret &= fdb_map_put_value(&map, &transaction, CFG_ADDRESS, config->address, strlen(config->address));
            fdb_transaction_commit(&transaction);
            fdb_map_close(&map);
        }
    }

    return ret;
}
