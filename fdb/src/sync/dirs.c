#include "dirs.h"
#include "ids.h"
#include "statuses.h"
#include <futils/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <binn.h>

static char const *TBL_DIRS = "sys/id/dir";
static char const *TBL_DIR_IDS = "sys/dir/id";
static char const *TBL_DIR_STATUSES = "sys/dir/status";

struct fdb_dirs
{
    volatile uint32_t   ref_counter;
    fdb_map_t           dirs;       // id->dir
    fdb_map_t           ids;
};

fdb_dirs_t *fdb_dirs(fdb_transaction_t *transaction)
{
    if (!transaction)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_dirs_t *pdirs = malloc(sizeof(fdb_dirs_t));
    if (!pdirs)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(pdirs, 0, sizeof *pdirs);

    pdirs->ref_counter = 1;

    if (!fdb_map_open(transaction, TBL_DIRS, FDB_MAP_CREATE | FDB_MAP_INTEGERKEY, &pdirs->dirs))
    {
        fdb_dirs_release(pdirs);
        return 0;
    }

    if (!fdb_ids_map_open(transaction, TBL_DIR_IDS, &pdirs->ids))
    {
        fdb_dirs_release(pdirs);
        return 0;
    }

    return pdirs;
}

fdb_dirs_t *fdb_dirs_retain(fdb_dirs_t *pdirs)
{
    if (pdirs)
        pdirs->ref_counter++;
    else
        FS_ERR("Invalid dirs map");
    return pdirs;
}

void fdb_dirs_release(fdb_dirs_t *pdirs)
{
    if (pdirs)
    {
        if (!pdirs->ref_counter)
            FS_ERR("Invalid dirs map");
        else if (!--pdirs->ref_counter)
        {
            fdb_map_close(&pdirs->dirs);
            fdb_map_close(&pdirs->ids);
            free(pdirs);
        }
    }
    else
        FS_ERR("Invalid dirs map");
}

bool fdb_dirs_add_unique(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, char const *path, uint32_t *id)
{
    if (!pdirs || !transaction || !path)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    uint32_t tmp_id;
    if (!id) id = &tmp_id;
    *id = FINVALID_ID;

    size_t const path_len = strlen(path);
    if (path_len >= FMAX_PATH)
    {
        FS_ERR("Path length is too long");
        return false;
    }

    if (fdb_dirs_get_id(pdirs, transaction, path, id))
        return false;   // directory is exist

    if (fdb_id_generate(&pdirs->ids, transaction, id))
    {
        fdb_data_t const dir_path = { path_len, (void*)path };
        fdb_data_t const dir_id = { sizeof *id, id };
        return fdb_map_put(&pdirs->dirs, transaction, &dir_id, &dir_path);
    }

    return false;
}

bool fdb_dirs_get_id(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, char const *path, uint32_t *id)
{
    if (!pdirs || !transaction || !path || !id)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    *id = FINVALID_ID;

    fdb_cursor_t cursor = { 0 };
    if (fdb_cursor_open(&pdirs->dirs, transaction, &cursor))
    {
        fdb_data_t dir_id = { 0 };     // id
        fdb_data_t dir_path = { 0 };   // dir

        size_t const path_len = strlen(path);

        for(bool st = fdb_cursor_get(&cursor, &dir_id, &dir_path, FDB_FIRST);
            st;
            st = fdb_cursor_get(&cursor, &dir_id, &dir_path, FDB_NEXT))
        {
            if (strncmp((char const*)dir_path.data, path, path_len < dir_path.size ? path_len : dir_path.size) == 0)
            {
                *id = *(uint32_t const *)dir_id.data;
                break;
            }
        }

        fdb_cursor_close(&cursor);
    }


    return *id != FINVALID_ID;
}

bool fdb_dirs_get(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, uint32_t id, fdir_info_t *info)
{
    if (!pdirs || !transaction || !info)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    fdb_data_t const dir_id = { sizeof id, &id };
    fdb_data_t dir_path = { 0 };

    if (fdb_map_get(&pdirs->dirs, transaction, &dir_id, &dir_path))
    {
        info->id = id;
        strncpy(info->path, (char const *)dir_path.data, dir_path.size);
        return true;
    }

    return false;
}

bool fdb_dirs_del(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, uint32_t id)
{
    fdb_data_t const dir_id = { sizeof id, &id };
    return fdb_map_del(&pdirs->dirs, transaction, &dir_id, 0);
}

struct fdb_dirs_iterator
{
    fdb_transaction_t       *transaction;
    fdb_dirs_t              *dirs;
    fdb_cursor_t             cursor;
};

fdb_dirs_iterator_t *fdb_dirs_iterator(fdb_dirs_t *dirs, fdb_transaction_t *transaction)
{
    if (!dirs || !transaction)
        return 0;

    fdb_dirs_iterator_t *piterator = malloc(sizeof(fdb_dirs_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory for dirs iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->transaction = transaction;
    piterator->dirs = fdb_dirs_retain(dirs);

    if (!fdb_cursor_open(&dirs->dirs, transaction, &piterator->cursor))
    {
        fdb_dirs_iterator_free(piterator);
        return 0;
    }

    return piterator;
}

void fdb_dirs_iterator_free(fdb_dirs_iterator_t *piterator)
{
    if (piterator)
    {
        fdb_dirs_release(piterator->dirs);
        fdb_cursor_close(&piterator->cursor);
        free(piterator);
    }
}

bool fdb_dirs_iterator_first(fdb_dirs_iterator_t *piterator, fdir_info_t *info)
{
    if (!piterator || !info)
        return false;

    fdb_data_t dir_id = { 0 };
    fdb_data_t dir_path = { 0 };

    if (!fdb_cursor_get(&piterator->cursor, &dir_id, &dir_path, FDB_FIRST))
        return false;

    info->id = *(uint32_t*)dir_id.data;
    strncpy(info->path, (char const *)dir_path.data, sizeof info->path < dir_path.size ? sizeof info->path : dir_path.size);

    return true;
}

bool fdb_dirs_iterator_next(fdb_dirs_iterator_t *piterator, fdir_info_t *info)
{
    if (!piterator || !info)
        return false;

    fdb_data_t dir_id = { 0 };
    fdb_data_t dir_path = { 0 };

    if (!fdb_cursor_get(&piterator->cursor, &dir_id, &dir_path, FDB_NEXT))
        return false;

    info->id = *(uint32_t*)dir_id.data;
    strncpy(info->path, (char const *)dir_path.data, sizeof info->path < dir_path.size ? sizeof info->path : dir_path.size);

    return true;
}

struct fdb_dirs_scan_status
{
    volatile uint32_t   ref_counter;
    fdb_map_t           statuses;
};

typedef enum
{
    FDIR_IS_NOT_EXIST = 1 << 0
} fdb_dirs_status_t;

static binn * fdb_dir_scan_status_marshal(fdir_scan_status_t const *info)
{
    binn *obj = binn_object();
    if (obj)
    {
        binn_object_set_uint32(obj, "id", info->id);
        binn_object_set_str(obj, "spos", (char*)info->path);
    }
    return obj;
}

static bool fdb_dir_scan_status_unmarshal(fdir_scan_status_t *info, void const *data)
{
    if (!info || !data)
        return false;
    binn *obj = binn_open((void *)data);
    if (!obj)
        return false;
    info->id = binn_object_uint32(obj, "id");
    strncpy(info->path, binn_object_str(obj, "spos"), sizeof info->path);
    binn_free(obj);
    return true;
}

fdb_dirs_scan_status_t *fdb_dirs_scan_status(fdb_transaction_t *transaction)
{
    if (!transaction)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_dirs_scan_status_t *pdirs = malloc(sizeof(fdb_dirs_scan_status_t));
    if (!pdirs)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(pdirs, 0, sizeof *pdirs);

    pdirs->ref_counter = 1;

    if (!fdb_statuses_map_open(transaction, TBL_DIR_STATUSES, &pdirs->statuses))
    {
        fdb_dirs_scan_status_release(pdirs);
        return 0;
    }

    return pdirs;
}

fdb_dirs_scan_status_t *fdb_dirs_scan_status_retain(fdb_dirs_scan_status_t *pdirs)
{
    if (pdirs)
        pdirs->ref_counter++;
    else
        FS_ERR("Invalid dirs scan statuses map");
    return pdirs;
}

void fdb_dirs_scan_status_release(fdb_dirs_scan_status_t *pdirs)
{
    if (pdirs)
    {
        if (!pdirs->ref_counter)
            FS_ERR("Invalid dirs scan statuses map");
        else if (!--pdirs->ref_counter)
        {
            fdb_map_close(&pdirs->statuses);
            free(pdirs);
        }
    }
    else
        FS_ERR("Invalid dirs scan statuses map");
}

bool fdb_dirs_scan_status_add(fdb_dirs_scan_status_t *pdirs, fdb_transaction_t *transaction, fdir_scan_status_t const *scan_status)
{
    if (!pdirs || !transaction || !scan_status)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    bool ret = false;

    binn *obj = fdb_dir_scan_status_marshal(scan_status);
    if(obj)
    {
        fdb_data_t const status_data = { binn_size(obj), binn_ptr(obj) };
        ret = fdb_statuses_map_put(&pdirs->statuses, transaction, FDIR_IS_NOT_EXIST, &status_data);
        binn_free(obj);
    }

    return ret;
}

bool fdb_dirs_scan_status_get(fdb_dirs_scan_status_t *pdirs, fdb_transaction_t *transaction, fdir_scan_status_t *scan_status)
{
    if (!pdirs || !transaction || !scan_status)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    fdb_data_t status_data = { 0 };
    if (fdb_statuses_map_get(&pdirs->statuses, transaction, FDIR_IS_NOT_EXIST, &status_data))
        return fdb_dir_scan_status_unmarshal(scan_status, status_data.data);

    return false;
}

bool fdb_dirs_scan_status_del(fdb_dirs_scan_status_t *pdirs, fdb_transaction_t *transaction, fdir_scan_status_t const *scan_status)
{
    if (!pdirs || !transaction || !scan_status)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    bool ret = false;

    binn *obj = fdb_dir_scan_status_marshal(scan_status);
    if(obj)
    {
        fdb_data_t const status_data = { binn_size(obj), binn_ptr(obj) };
        ret = fdb_statuses_map_del(&pdirs->statuses, transaction, FDIR_IS_NOT_EXIST, &status_data);
        binn_free(obj);
    }

    return ret;
}

bool fdb_dirs_scan_status_update(fdb_dirs_scan_status_t *pdirs, fdb_transaction_t *transaction, fdir_scan_status_t const *scan_status, fdir_scan_status_t const *new_scan_status)
{
    return fdb_dirs_scan_status_del(pdirs, transaction, scan_status)
           && fdb_dirs_scan_status_add(pdirs, transaction, new_scan_status);
}
