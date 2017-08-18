#include "test.h"
#include <fdb/db.h>
#include <fdb/sync/ids.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

FTEST_START(fbd_simple)
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
                    { { 8, "2 Hello!" },  { 6, "World!" } },
                    { { 7, "3 Hello" },   { 5, "World" } },
                    { { 9, "1 Hello!!" }, { 7, "World!!" } }
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
                            int i = 0;
                            for(; i < 3; ++i)
                            {
                                if (memcmp(data[i][0].data, key.data, key.size) == 0
                                    && memcmp(data[i][1].data, value.data, value.size) == 0)
                                    break;
                            }
                            assert(i < 3);
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
FTEST_END()

FTEST_START(fbd_ids)
{
    fdb_t *pdb = fdb_open("test", 4u, 1u, 16 * 1024 * 1024);
    if (pdb)
    {
        fdb_transaction_t transaction = {0};
        if (fdb_transaction_start(pdb, &transaction))
        {
            fdb_map_t map = {0};
            if (fdb_ids_map_open(&transaction, "ids", &map))
            {
                fdb_transaction_commit(&transaction);

                uint32_t ids[5] = { FINVALID_ID, FINVALID_ID, FINVALID_ID, FINVALID_ID, FINVALID_ID};

                if (fdb_transaction_start(pdb, &transaction))
                {
                    if (fdb_id_generate(&map, &transaction, ids + 0)
                        && fdb_id_generate(&map, &transaction, ids + 1)
                        && fdb_id_generate(&map, &transaction, ids + 2)
                        && fdb_id_generate(&map, &transaction, ids + 3))
                        fdb_transaction_commit(&transaction);
                }
                else fdb_transaction_abort(&transaction);

                if (fdb_transaction_start(pdb, &transaction))
                {
                    if (fdb_id_free(&map, &transaction, ids[2])
                        && fdb_id_free(&map, &transaction, ids[1]))
                        fdb_transaction_commit(&transaction);
                }
                else fdb_transaction_abort(&transaction);

                if (fdb_transaction_start(pdb, &transaction))
                {
                    if (fdb_id_generate(&map, &transaction, ids + 1)
                        && fdb_id_generate(&map, &transaction, ids + 2)
                        && fdb_id_generate(&map, &transaction, ids + 4))
                        fdb_transaction_commit(&transaction);
                }
                else fdb_transaction_abort(&transaction);

                if (ids[4] != ids[3] + 1
                    && ids[3] != ids[2] + 1
                    && ids[2] != ids[1] + 1
                    && ids[1] != ids[0] + 1)
                    printf("fbd_ids_test failed!\n");

                fdb_map_close(&map);
            }
            else fdb_transaction_abort(&transaction);
        }
        fdb_release(pdb);
    }
}
FTEST_END()

FUNIT_TEST_START(fbd)
    FTEST(fbd_simple);
    FTEST(fbd_ids);
FUNIT_TEST_END()
