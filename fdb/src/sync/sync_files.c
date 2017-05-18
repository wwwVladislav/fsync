#include "sync_files.h"
#include "statuses.h"
#include <futils/md5.h>
#include <futils/log.h>
#include <string.h>
#include <fcommon/limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <binn.h>

static char const TBL_SYNC_FILE_INFO[]    = "/sfinfo";
static char const TBL_SYNC_FILE_PATH_ID[] = "/sfpath->id";
static char const TBL_SYNC_FILE_ID[]      = "/sfid";
static char const TBL_SYNC_FILE_STATUS[]  = "/sfstatus";

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

struct fdb_sync_files_map
{
    volatile uint32_t   ref_counter;
    fdb_map_t           files_map;
    fdb_map_t           path_ids_map;
    fdb_map_t           ids_map;
};

fdb_sync_files_map_t *fdb_sync_files_ex(fdb_transaction_t *transaction, fuuid_t const *uuid, bool ids_generator)
{
    if (!transaction || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_sync_files_map_t *files_map = malloc(sizeof(fdb_sync_files_map_t));
    if (!files_map)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(files_map, 0, sizeof *files_map);

    files_map->ref_counter = 1;

    // id->file_info
    char files_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_SYNC_FILE_INFO] = { 0 };
    fdb_tbl_name(uuid, files_tbl_name, sizeof files_tbl_name, TBL_SYNC_FILE_INFO);

    if (!fdb_map_open(transaction, files_tbl_name, FDB_MAP_CREATE | FDB_MAP_INTEGERKEY, &files_map->files_map))
    {
        FS_ERR("Map wasn't created");
        fdb_transaction_abort(transaction);
        fdb_sync_files_release(files_map);
        return 0;
    }

    // path->id
    char path_ids_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_SYNC_FILE_PATH_ID] = { 0 };
    fdb_tbl_name(uuid, path_ids_tbl_name, sizeof path_ids_tbl_name, TBL_SYNC_FILE_PATH_ID);

    if (!fdb_map_open(transaction, path_ids_tbl_name, FDB_MAP_CREATE, &files_map->path_ids_map))
    {
        FS_ERR("Map wasn't created");
        fdb_transaction_abort(transaction);
        fdb_sync_files_release(files_map);
        return 0;
    }

    // ids
    if (ids_generator)
    {
        char ids_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_SYNC_FILE_ID] = { 0 };
        fdb_tbl_name(uuid, ids_tbl_name, sizeof ids_tbl_name, TBL_SYNC_FILE_ID);

        if (!fdb_ids_map_open(transaction, ids_tbl_name, &files_map->ids_map))
        {
            FS_ERR("Map wasn't created");
            fdb_transaction_abort(transaction);
            fdb_sync_files_release(files_map);
            return 0;
        }
    }

    return files_map;
}

fdb_sync_files_map_t *fdb_sync_files(fdb_transaction_t *transaction, fuuid_t const *uuid)
{
    return fdb_sync_files_ex(transaction, uuid, true);
}

fdb_sync_files_map_t *fdb_sync_files_retain(fdb_sync_files_map_t *files_map)
{
    if (files_map)
        files_map->ref_counter++;
    else
        FS_ERR("Invalid files map");
    return files_map;
}

void fdb_sync_files_release(fdb_sync_files_map_t *files_map)
{
    if (files_map)
    {
        if (!files_map->ref_counter)
            FS_ERR("Invalid files map");
        else if (!--files_map->ref_counter)
        {
            fdb_map_close(&files_map->files_map);
            fdb_map_close(&files_map->ids_map);
            fdb_map_close(&files_map->path_ids_map);
            free(files_map);
        }
    }
    else
        FS_ERR("Invalid files map");
}

bool fdb_sync_files_statuses(fdb_transaction_t *transaction, fuuid_t const *uuid, fdb_map_t *pmap)
{
    if (!transaction || !uuid || !pmap)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    char file_status_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_SYNC_FILE_STATUS] = { 0 };
    fdb_tbl_name(uuid, file_status_tbl_name, sizeof file_status_tbl_name, TBL_SYNC_FILE_STATUS);

    return fdb_statuses_map_open(transaction, file_status_tbl_name, pmap);
}

static char STR_PATH[] = "path";
static char STR_MTIME[] = "mtime";
static char STR_STIME[] = "stime";
static char STR_DIGEST[] = "digest";
static char STR_SIZE[] = "size";
static char STR_STATUS[] = "status";

static binn * fdb_file_info_marshal(fsync_file_info_t const *info)
{
    binn *obj = binn_object();
    if (!binn_object_set_str(obj, STR_PATH, (char *)info->path)
         || !binn_object_set_uint64(obj, STR_MTIME, (uint64_t)info->mod_time)
         || !binn_object_set_uint64(obj, STR_STIME, (uint64_t)info->sync_time)
         || !binn_object_set_blob(obj, STR_DIGEST, (void *)info->digest.data, sizeof info->digest.data)
         || !binn_object_set_uint64(obj, STR_SIZE, info->size)
         || !binn_object_set_uint32(obj, STR_STATUS, info->status))
    {
        binn_free(obj);
        obj = 0;
    }
    return obj;
}

static bool fdb_file_info_unmarshal(fsync_file_info_t *info, void const *data)
{
    if (!info || !data)
        return false;
    int digest_size = 0;
    binn *obj = binn_open((void *)data);
    if (!obj)
        return false;
    char const *path = binn_object_str(obj, STR_PATH);
    if (path) strncpy(info->path, path, sizeof info->path);
    info->mod_time = (time_t)binn_object_uint64(obj, STR_MTIME);
    info->sync_time = (time_t)binn_object_uint64(obj, STR_STIME);
    memcpy(info->digest.data, binn_object_blob(obj, STR_DIGEST, &digest_size), sizeof info->digest.data);
    info->size = binn_object_uint64(obj, STR_SIZE);
    info->status = binn_object_uint32(obj, STR_STATUS);
    binn_free(obj);
    return true;
}

bool fdb_sync_file_add(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, fsync_file_info_t *info)
{
    if (!files_map || !transaction || !info)
        return false;

    fsync_file_info_t old_info = { 0 };

    if (!fdb_sync_file_get_by_path(files_map, transaction, info->path, strlen(info->path), &old_info))
    {
        if (info->id == FINVALID_ID
            && !fdb_id_generate(&files_map->ids_map, transaction, &info->id))
            return false;
    }
    else
        info->id = old_info.id;

    binn *binfo = fdb_file_info_marshal(info);
    if (!binfo)
        return false;

    fdb_data_t const file_id = { sizeof info->id, &info->id };
    fdb_data_t const file_info = { binn_size(binfo), binn_ptr(binfo) };
    fdb_data_t const file_path = { strlen(info->path), info->path };

    bool ret = fdb_map_put(&files_map->files_map, transaction, &file_id, &file_info)
                && fdb_map_put(&files_map->path_ids_map, transaction, &file_path, &file_id);

    binn_free(binfo);

    return ret;
}

bool fdb_sync_file_add_unique(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, fsync_file_info_t *info)
{
    if (!files_map || !transaction || !info || info->id != FINVALID_ID)
        return false;

    uint32_t id = FINVALID_ID;

    if (!fdb_sync_file_id(files_map, transaction, info->path, strlen(info->path), &id))
    {
        if (!fdb_id_generate(&files_map->ids_map, transaction, &info->id))
            return false;

        binn *binfo = fdb_file_info_marshal(info);
        if (!binfo)
            return false;

        fdb_data_t const file_id = { sizeof info->id, &info->id };
        fdb_data_t const file_info = { binn_size(binfo), binn_ptr(binfo) };
        fdb_data_t const file_path = { strlen(info->path), info->path };

        bool ret = fdb_map_put(&files_map->files_map, transaction, &file_id, &file_info)
                    && fdb_map_put(&files_map->path_ids_map, transaction, &file_path, &file_id);

        binn_free(binfo);

        return ret;
    }

    return false;
}

bool fdb_sync_file_del(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id)
{
    if (!files_map || !transaction)
        return false;

    fsync_file_info_t info;
    if (fdb_sync_file_get(files_map, transaction, id, &info))
    {
        fdb_data_t const file_id = { sizeof id, &id };
        fdb_data_t const file_path = { strlen(info.path), info.path };

        return fdb_map_del(&files_map->files_map, transaction, &file_id, 0)
                && fdb_map_del(&files_map->path_ids_map, transaction, &file_path, 0)
                && fdb_id_free(&files_map->ids_map, transaction, info.id);
    }
    return false;
}

bool fdb_sync_file_get(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, fsync_file_info_t *info)
{
    if (!files_map || !transaction || !info)
        return false;
    info->id = id;
    fdb_data_t const file_id = { sizeof id, &id };
    fdb_data_t file_info = { 0 };
    return fdb_map_get(&files_map->files_map, transaction, &file_id, &file_info)
            && fdb_file_info_unmarshal(info, file_info.data);
}

bool fdb_sync_file_id(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, uint32_t *id)
{
    if (!files_map || !transaction || !path || !id)
        return false;
    fdb_data_t const file_path = { size, (void*)path };
    fdb_data_t file_id = { 0 };
    if (fdb_map_get(&files_map->path_ids_map, transaction, &file_path, &file_id))
    {
        *id = *(uint32_t*)file_id.data;
        return true;
    }
    return false;
}

bool fdb_sync_file_get_by_path(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, fsync_file_info_t *info)
{
    uint32_t id = FINVALID_ID;
    return fdb_sync_file_id(files_map, transaction, path, size, &id)
            && fdb_sync_file_get(files_map, transaction, id, info);
}

bool fdb_sync_file_del_all(fuuid_t const *uuid)
{
    // TODO
    return true;
}

bool fdb_sync_file_path(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, char *path, size_t size)
{
    if (!transaction || !path)
        return false;

    fsync_file_info_t info = { 0 };

    if (fdb_sync_file_get(files_map, transaction, id, &info))
    {
        strncpy(path, info.path, size);
        return true;
    }

    return false;
}

struct fdb_sync_files_iterator
{
    fdb_transaction_t       *transaction;
    fdb_sync_files_map_t    *files_map;
    fdb_cursor_t             cursor;
};

struct fdb_sync_files_diff_iterator
{
    fdb_transaction_t       *transaction;
    fdb_sync_files_map_t    *files_map_1;
    fdb_sync_files_map_t    *files_map_2;
    fdb_cursor_t             cursor;
};

fdb_sync_files_iterator_t *fdb_sync_files_iterator(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction)
{
    if (!transaction)
        return 0;

    fdb_sync_files_iterator_t *piterator = malloc(sizeof(fdb_sync_files_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory for sync files iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->transaction = transaction;
    piterator->files_map = fdb_sync_files_retain(files_map);

    if (!fdb_cursor_open(&files_map->path_ids_map, transaction, &piterator->cursor))
    {
        fdb_sync_files_iterator_free(piterator);
        return 0;
    }

    return piterator;
}

void fdb_sync_files_iterator_free(fdb_sync_files_iterator_t *piterator)
{
    if (piterator)
    {
        fdb_sync_files_release(piterator->files_map);
        fdb_cursor_close(&piterator->cursor);
        free(piterator);
    }
}

bool fdb_sync_files_iterator_first(fdb_sync_files_iterator_t *piterator, fsync_file_info_t *info)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path = { 0 };
    fdb_data_t file_id = { 0 };

    return fdb_cursor_get(&piterator->cursor, &file_path, &file_id, FDB_FIRST)
            && fdb_sync_file_get(piterator->files_map, piterator->transaction, *(uint32_t*)file_id.data, info);
}

bool fdb_sync_files_iterator_next(fdb_sync_files_iterator_t *piterator, fsync_file_info_t *info)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path = { 0 };
    fdb_data_t file_id = { 0 };

    return fdb_cursor_get(&piterator->cursor, &file_path, &file_id, FDB_NEXT)
            && fdb_sync_file_get(piterator->files_map, piterator->transaction, *(uint32_t*)file_id.data, info);
}

fdb_sync_files_diff_iterator_t *fdb_sync_files_diff_iterator(fdb_sync_files_map_t *map_1, fdb_sync_files_map_t *map_2, fdb_transaction_t *transaction)
{
    if (!transaction || !map_1 || !map_2)
        return 0;

    fdb_sync_files_diff_iterator_t *piterator = malloc(sizeof(fdb_sync_files_diff_iterator_t));
    if (!piterator)
    {
        FS_ERR("Unable to allocate memory sync files iterator");
        return 0;
    }
    memset(piterator, 0, sizeof *piterator);

    piterator->transaction = transaction;
    piterator->files_map_1 = fdb_sync_files_retain(map_1);
    piterator->files_map_2 = fdb_sync_files_retain(map_2);

    if (!fdb_cursor_open(&map_1->path_ids_map, transaction, &piterator->cursor))
    {
        fdb_sync_files_diff_iterator_free(piterator);
        return 0;
    }

    return piterator;
}

void fdb_sync_files_diff_iterator_free(fdb_sync_files_diff_iterator_t *piterator)
{
    if (piterator)
    {
        fdb_sync_files_release(piterator->files_map_1);
        fdb_sync_files_release(piterator->files_map_2);
        fdb_cursor_close(&piterator->cursor);
        free(piterator);
    }
}

bool fdb_sync_files_diff_iterator_first(fdb_sync_files_diff_iterator_t *piterator, fsync_file_info_t *info, fdb_diff_kind_t *diff_kind)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path_1 = { 0 };
    fdb_data_t file_id_1 = { 0 };

    bool first = true;

    while (fdb_cursor_get(&piterator->cursor, &file_path_1, &file_id_1, first ? FDB_FIRST : FDB_NEXT))
    {
        uint32_t id_2 = FINVALID_ID;

        if (!fdb_sync_file_id(piterator->files_map_2, piterator->transaction, (char const*)file_path_1.data, file_path_1.size, &id_2))
        {
            if (!fdb_sync_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (diff_kind)
                *diff_kind = FDB_FILE_ABSENT;
            return true;
        }
        else
        {
            fsync_file_info_t info_2 = { 0 };

            if (!fdb_sync_file_get(piterator->files_map_2, piterator->transaction, id_2, &info_2)
                || !fdb_sync_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (memcmp(&info->digest, &info_2.digest, sizeof info->digest) != 0)
            {
                if (diff_kind)
                    *diff_kind = FDB_DIFF_CONTENT;
                return true;
            }
        }

        first = false;
    }

    return false;

}

bool fdb_sync_files_diff_iterator_next(fdb_sync_files_diff_iterator_t *piterator, fsync_file_info_t *info, fdb_diff_kind_t *diff_kind)
{
    if (!piterator || !info)
        return false;

    fdb_data_t file_path_1 = { 0 };
    fdb_data_t file_id_1 = { 0 };

    while (fdb_cursor_get(&piterator->cursor, &file_path_1, &file_id_1, FDB_NEXT))
    {
        uint32_t id_2 = FINVALID_ID;

        if (!fdb_sync_file_id(piterator->files_map_2, piterator->transaction, (char const*)file_path_1.data, file_path_1.size, &id_2))
        {
            if (!fdb_sync_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (diff_kind)
                *diff_kind = FDB_FILE_ABSENT;
            return true;
        }
        else
        {
            fsync_file_info_t info_2 = { 0 };

            if (!fdb_sync_file_get(piterator->files_map_2, piterator->transaction, id_2, &info_2)
                || !fdb_sync_file_get(piterator->files_map_1, piterator->transaction, *(uint32_t*)file_id_1.data, info))
            {
                FS_ERR("DB consistency is broken");
                return false;
            }
            if (memcmp(&info->digest, &info_2.digest, sizeof info->digest) != 0)
            {
                if (diff_kind)
                    *diff_kind = FDB_DIFF_CONTENT;
                return true;
            }
        }
    }

    return false;
}
