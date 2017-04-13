#include "config.h"
#include <string.h>

static char const *TBL_CONFIG = "config";
static char const *CFG_UUID = "uuid";
static char const *CFG_ADDRESS = "address";
static char const *CFG_SYNC_DIR = "dir";

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
            ret = fdb_map_get_value(&map, &transaction, CFG_UUID, &config->uuid, sizeof config->uuid);
            ret &= fdb_map_get_value(&map, &transaction, CFG_ADDRESS, &config->address, sizeof config->address);
            fdb_map_get_value(&map, &transaction, CFG_SYNC_DIR, &config->sync_dir, sizeof config->sync_dir);
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

    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(pdb, &transaction))
    {
        fdb_map_t map = {0};
        if (fdb_map_open(&transaction, TBL_CONFIG, FDB_MAP_CREATE, &map))
        {
            ret = fdb_map_put_value(&map, &transaction, CFG_UUID, &config->uuid, sizeof config->uuid);
            ret &= fdb_map_put_value(&map, &transaction, CFG_ADDRESS, config->address, strlen(config->address));
            ret &= fdb_map_put_value(&map, &transaction, CFG_SYNC_DIR, config->sync_dir, strlen(config->sync_dir));
            fdb_transaction_commit(&transaction);
            fdb_map_close(&map);
        }
    }

    return ret;
}
