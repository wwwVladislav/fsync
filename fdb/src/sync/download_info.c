#include "download_info.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>

static char const TBL_DOWNLOAD_INFO[] = "/fdinf";

static char const *fdb_tbl_name(fuuid_t const *uuid, char *buf, size_t size, char const *tbl)
{
    if (size < sizeof(fuuid_t) * 2 + strlen(tbl) + 1)
        return 0;
    char *ret = buf;
    fuuid2str(uuid, buf, size);
    size_t uuid_len = strlen(buf);
    buf += uuid_len;
    size -= uuid_len;
    strncpy(buf, tbl, size);
    return ret;
}

struct fdinf_map
{
    volatile uint32_t   ref_counter;
    fdb_map_t           dinf_map;
};

fdinf_map_t *fdinf(fdb_transaction_t *transaction, fuuid_t const *uuid)
{
    if (!transaction || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdinf_map_t *pmap = malloc(sizeof(fdinf_map_t));
    if (!pmap)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(pmap, 0, sizeof *pmap);

    pmap->ref_counter = 1;

    char dinf_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_DOWNLOAD_INFO] = { 0 };
    fdb_tbl_name(uuid, dinf_tbl_name, sizeof dinf_tbl_name, TBL_DOWNLOAD_INFO);

    if (!fdb_map_open(transaction, dinf_tbl_name, FDB_MAP_CREATE | FDB_MAP_INTEGERKEY, &pmap->dinf_map))
    {
        FS_ERR("Map wasn't created");
        fdb_transaction_abort(transaction);
        fdinf_release(pmap);
        return 0;
    }

    return pmap;
}

fdinf_map_t *fdinf_retain(fdinf_map_t *pmap)
{
    if (pmap)
        pmap->ref_counter++;
    else
        FS_ERR("Invalid files map");
    return pmap;
}

void fdinf_release(fdinf_map_t *pmap)
{
    if (pmap)
    {
        if (!pmap->ref_counter)
            FS_ERR("Invalid files map");
        else if (!--pmap->ref_counter)
        {
            fdb_map_close(&pmap->dinf_map);
            free(pmap);
        }
    }
    else
        FS_ERR("Invalid files map");
}
bool fdinf_received_size(fdinf_map_t *pmap, fdb_transaction_t *transaction, uint32_t id, uint64_t *size)
{
    if (!pmap || !transaction || !size)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    fdb_data_t const file_id = { sizeof id, &id };
    fdb_data_t dinf = { 0 };

    if (!fdb_map_get(&pmap->dinf_map, transaction, &file_id, &dinf))
        return false;
    *size = *(uint64_t*)dinf.data;
    return true;
}

bool fdinf_received_size_update(fdinf_map_t *pmap, fdb_transaction_t *transaction, uint32_t id, uint64_t size)
{
    if (!pmap || !transaction)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    fdb_data_t const file_id = { sizeof id, &id };
    fdb_data_t dinf = { sizeof size, &size };

    return fdb_map_put(&pmap->dinf_map, transaction, &file_id, &dinf);
}
