#include "files.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>
#include <binn.h>

static char const TBL_FILE_INFO[] = "/finfo";

struct fdb_files
{
    volatile uint32_t   ref_counter;
    fdb_map_t           files_map;
};

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

static char STR_SIZE[] = "s";

static binn * fdb_file_info_marshal(ffile_info_t const *info)
{
    binn *obj = binn_object();
    if (!binn_object_set_uint64(obj, STR_SIZE, info->size))
    {
        binn_free(obj);
        obj = 0;
    }
    return obj;
}

static bool fdb_file_info_unmarshal(ffile_info_t *info, void const *data)
{
    if (!info || !data)
        return false;
    binn *obj = binn_open((void *)data);
    if (!obj)
        return false;
    info->size = binn_object_uint64(obj, STR_SIZE);
    binn_free(obj);
    return true;
}

fdb_files_t *fdb_files(fdb_transaction_t *transaction, fuuid_t const *uuid)
{
    if (!transaction || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_files_t *files = malloc(sizeof(fdb_files_t));
    if (!files)
    {
        FS_ERR("No free space of memory");
        return 0;
    }

    memset(files, 0, sizeof *files);

    files->ref_counter = 1;

    // path->file_info
    char files_tbl_name[sizeof(fuuid_t) * 2 + sizeof TBL_FILE_INFO] = { 0 };
    fdb_tbl_name(uuid, files_tbl_name, sizeof files_tbl_name, TBL_FILE_INFO);

    if (!fdb_map_open(transaction, files_tbl_name, FDB_MAP_CREATE, &files->files_map))
    {
        FS_ERR("Map wasn't created");
        fdb_transaction_abort(transaction);
        fdb_files_release(files);
        return 0;
    }

    return files;
}

fdb_files_t *fdb_files_retain(fdb_files_t *files)
{
    if (files)
        files->ref_counter++;
    else
        FS_ERR("Invalid files map");
    return files;
}

void fdb_files_release(fdb_files_t *files)
{
    if (files)
    {
        if (!files->ref_counter)
            FS_ERR("Invalid files map");
        else if (!--files->ref_counter)
        {
            fdb_map_close(&files->files_map);
            free(files);
        }
    }
    else
        FS_ERR("Invalid files map");
}

bool fdb_files_add(fdb_files_t *files, fdb_transaction_t *transaction, ffile_info_t const *info)
{
    if (!files || !transaction || !info)
        return false;

    binn *obj = fdb_file_info_marshal(info);
    if (obj)
    {
        fdb_data_t const file_path = { strlen(info->path), (char*)info->path };
        fdb_data_t const file_info = { binn_size(obj), binn_ptr(obj) };
        bool ret = fdb_map_put(&files->files_map, transaction, &file_path, &file_info);
        binn_free(obj);
        return ret;
    }

    return false;
}

bool fdb_files_find(fdb_files_t *files, fdb_transaction_t *transaction, char const *file)
{
    if (!files || !file)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    bool ret = false;

    fdb_cursor_t cursor = { 0 };
    if (fdb_cursor_open(&files->files_map, transaction, &cursor))
    {
        fdb_data_t file_path = { 0 };
        fdb_data_t file_info = { 0 };
        for(bool st = fdb_cursor_get(&cursor, &file_path, &file_info, FDB_FIRST);
            st;
            st = fdb_cursor_get(&cursor, &file_path, &file_info, FDB_NEXT))
        {
            char path[2 * FMAX_PATH] = { 0 };
            strncpy(path, file_path.data, file_path.size);
            ret = strstr(path, file) != 0;
            if (ret) break;
        }
        fdb_cursor_close(&cursor);
    }

    return ret;
}
