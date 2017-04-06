#include <fdb/db.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

void fbd_test()
{
    fdb_t *pdb = fdb_open("test", 4u, 1u, 16 * 1024 * 1024);
    if (pdb)
    {
        fdb_transaction_t transaction = {0};
        if (fdb_transaction_start(pdb, &transaction))
        {
            fdb_map_t map = {0};
            if (fdb_map_open(&transaction, "db1", FDB_MAP_CREATE, &map))
            {
                // Insert data
                fdb_data_t data[][2] = {
                    { { 5, "Hello" },   { 5, "World" } },
                    { { 6, "Hello!" },  { 6, "World!" } },
                    { { 7, "Hello!!" }, { 7, "World!!" } }
                };
                for(int i = 0; i < sizeof data / sizeof data[0]; ++i)
                    fdb_map_put(&map, &transaction, data[i], data[i] + 1);
                fdb_transaction_commit(&transaction);

                // Read data
                if (fdb_transaction_start(pdb, &transaction))
                {
                    fdb_data_t const key = { 5, "Hello" };
                    fdb_data_t value = { 0 };
                    fdb_map_get(&map, &transaction, &key, &value);
                    fdb_transaction_abort(&transaction);
                }

                // Read by cursor
                if (fdb_transaction_start(pdb, &transaction))
                {
                    fdb_cursor_t cursor = { 0 };
                    if (fdb_cursor_open(&transaction, &map, &cursor))
                    {
                        fdb_data_t key, value;
                        while (fdb_cursor_get(&cursor, &key, &value, FDB_NEXT))
                        {
                            char k[16], v[16];
                            memcpy(k, key.data, key.size);
                            k[key.size] = 0;
                            memcpy(v, value.data, value.size);
                            v[value.size] = 0;
                            printf("\'%s\' -> \'%s\'\n", k, v);
                        }
                        fdb_cursor_close(&cursor);
                    }
                    fdb_transaction_abort(&transaction);
                }

                fdb_map_close(&map);
            }
        }
        fdb_release(pdb);
    }
}
