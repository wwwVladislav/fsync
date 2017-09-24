#include "dbtool.h"
#include <stdlib.h>
#include <string.h>
#include <futils/log.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <fdb/sync/config.h>
#include <fdb/sync/nodes.h>
#include <fdb/sync/dirs.h>
#include <fdb/sync/files.h>
#include <stdio.h>

enum
{
    FDB_MAX_READERS = 1,
    FDB_MAP_SIZE    = 64 * 1024 * 1024,
    FDB_MAX_DBS     = 8     // config, nodes
};

struct fdbtool
{
    fdb_t            *db;
};

fdbtool_t *fdbtool(char const *dir)
{
    fdbtool_t *ptool = malloc(sizeof(fdbtool_t));
    if (!ptool)
    {
        FS_ERR("Unable to allocate memory for dbtool");
        return 0;
    }
    memset(ptool, 0, sizeof *ptool);

    ptool->db = fdb_open(dir, FDB_MAX_DBS, FDB_MAX_READERS, FDB_MAP_SIZE);
    if (!ptool->db)
    {
        FS_ERR("Unable to open the DB");
        fdbtool_close(ptool);
        return 0;
    }

    return ptool;
}

void fdbtool_close(fdbtool_t *dbtool)
{
    if (dbtool)
    {
        fdb_release(dbtool->db);
        memset(dbtool, 0, sizeof *dbtool);
        free(dbtool);
    }
}

void fdbtool_tables(fdbtool_t *dbtool)
{
    char str[1024];

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(dbtool->db, &transaction))
    {
        fdb_map_t db_map;
        if (fdb_map_open(&transaction, 0, 0, &db_map))
        {
            fdb_cursor_t cursor;
            if (fdb_cursor_open(&db_map, &transaction, &cursor))
            {
                fdb_data_t key = { 0 };
                fdb_data_t value = { 0 };
                for (bool ret = fdb_cursor_get(&cursor, &key, &value, FDB_FIRST);
                     ret;
                     ret = fdb_cursor_get(&cursor, &key, &value, FDB_NEXT))
                {
                    size_t const len = key.size < sizeof str ? key.size : sizeof str;
                    strncpy(str, key.data, len);
                    str[len < sizeof str ? len : sizeof str - 1] = 0;
                    printf("%s\n", str);
                }
                fdb_cursor_close(&cursor);
            }
            fdb_map_close(&db_map);
        }
        fdb_transaction_abort(&transaction);
    }
}
