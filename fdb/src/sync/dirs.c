#include "dirs.h"
#include "ids.h"
#include <futils/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char const *TBL_DIRS = "id->dir";
static char const *TBL_DIR_IDS = "dir_ids";

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

bool fdb_dirs_add(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, char const *path, uint32_t *id)
{
    if (!pdirs || !transaction || !path)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    uint32_t tmp_id = FINVALID_ID;
    if (!id) id = &tmp_id;

    if (fdb_dirs_get_id(pdirs, transaction, path, id))
        return true;

    if (fdb_id_generate(&pdirs->ids, transaction, id))
    {
        fdb_data_t const dir_path = { strlen(path), (void*)path };
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
    strncpy(info->path, (char const *)dir_path.data, sizeof info->path);

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
    strncpy(info->path, (char const *)dir_path.data, sizeof info->path);

    return true;
}
