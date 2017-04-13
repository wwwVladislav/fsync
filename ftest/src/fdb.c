#include <fdb/db.h>
#include <fdb/sync/ids.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static void fbd_simple_test()
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
                    if (fdb_cursor_open(&map, &transaction, &cursor))
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

static void fbd_ids_test()
{
    fdb_t *pdb = fdb_open("test", 4u, 1u, 16 * 1024 * 1024);
    if (pdb)
    {
        uint32_t ids[5] = { FINVALID_ID, FINVALID_ID, FINVALID_ID, FINVALID_ID, FINVALID_ID};

        fdb_ids_transaction_t *transaction = fdb_ids_transaction_start(pdb, "ids");
        if (transaction)
        {
            if (fdb_id_generate(transaction, ids + 0)
                && fdb_id_generate(transaction, ids + 1)
                && fdb_id_generate(transaction, ids + 2)
                && fdb_id_generate(transaction, ids + 3))
                fdb_ids_transaction_commit(transaction);
        }

        transaction = fdb_ids_transaction_start(pdb, "ids");
        if (transaction)
        {
            if (fdb_id_free(transaction, ids[2])
                && fdb_id_free(transaction, ids[1]))
                fdb_ids_transaction_commit(transaction);
        }

        transaction = fdb_ids_transaction_start(pdb, "ids");
        if (transaction)
        {
            if (fdb_id_generate(transaction, ids + 1)
                && fdb_id_generate(transaction, ids + 2)
                && fdb_id_generate(transaction, ids + 4))
                fdb_ids_transaction_commit(transaction);
        }

        if (ids[4] != ids[3] + 1
            && ids[3] != ids[2] + 1
            && ids[2] != ids[1] + 1
            && ids[1] != ids[0] + 1)
            printf("fbd_ids_test failed!\n");

        fdb_release(pdb);
    }
}

void fbd_test()
{
    fbd_simple_test();
    fbd_ids_test();
}
