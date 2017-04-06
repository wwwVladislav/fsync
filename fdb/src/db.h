#ifndef DB_H_FDB
#define DB_H_FDB
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct fdb fdb_t;

typedef struct
{
    fdb_t *pdb;
    void  *ptransaction;
} fdb_transaction_t;

typedef struct
{
    fdb_t             *pdb;
    unsigned int       dbmap;
} fdb_map_t;

typedef struct
{
    fdb_t *pdb;
    void  *pcursor;
} fdb_cursor_t;

typedef struct fdb_field
{
    char const *name;
    size_t      offset;
    size_t      size;
} fdb_field_t;

typedef struct fdb_data
{
    size_t size;
    void  *data;
} fdb_data_t;

fdb_t* fdb_open(char const *path, uint32_t max_dbs, uint32_t readers, uint32_t size);
fdb_t* fdb_retain(fdb_t *pdb);
void fdb_release(fdb_t *pdb);

bool fdb_transaction_start(fdb_t *pdb, fdb_transaction_t *ptransaction);
void fdb_transaction_commit(fdb_transaction_t *transaction);
void fdb_transaction_abort(fdb_transaction_t *transaction);

enum fdb_map_flags
{
    FDB_MAP_CREATE              = 1 << 0,   // Create the map if it doesn't exist
    FDB_MAP_MULTI               = 1 << 1,   // Duplicate keys may be used in the map
    FDB_MAP_INTEGERKEY          = 1 << 2,   // Keys are binary integers in native byte order
    FDB_MAP_FIXED_SIZE_VALUE    = 1 << 3,   // The data items for this map are all the same size. (used only with FDB_MAP_MULTI)
    FDB_MAP_INTEGERVAL          = 1 << 4,   // This option specifies that duplicate data items are binary integers
};

typedef enum fdb_cursor_op
{
    FDB_FIRST,                              // Position at first key/data item
    FDB_CURRENT,                            // Return key/data at current cursor position
    FDB_LAST,                               // Position at last key/data item
    FDB_NEXT,                               // Position at next data item
    FDB_PREV                                // Position at previous data item
} fdb_cursor_op_t;

bool fdb_map_open(fdb_transaction_t *transaction, char const *name, uint32_t flags, fdb_map_t *pmap);
void fdb_map_close(fdb_map_t *pmap);
bool fdb_map_put(fdb_map_t *pmap, fdb_transaction_t *transaction, fdb_data_t const *key, fdb_data_t const *value);
bool fdb_map_put_value(fdb_map_t *pmap, fdb_transaction_t *transaction, char const *key, void const *value, size_t size);
bool fdb_map_put_unique(fdb_map_t *pmap, fdb_transaction_t *transaction, fdb_data_t const *key, fdb_data_t const *value);
bool fdb_map_get(fdb_map_t *pmap, fdb_transaction_t *transaction, fdb_data_t const *key, fdb_data_t *value);
bool fdb_map_get_value(fdb_map_t *pmap, fdb_transaction_t *transaction, char const *key, void *value, size_t size);

bool fdb_cursor_open(fdb_transaction_t *transaction, fdb_map_t *pmap, fdb_cursor_t *pcursor);
void fdb_cursor_close(fdb_cursor_t *pcursor);
bool fdb_cursor_get(fdb_cursor_t *pcursor, fdb_data_t *key, fdb_data_t *value, fdb_cursor_op_t op);

#endif
