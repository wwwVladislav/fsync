#include "db.h"
#include <stdint.h>
#include <futils/log.h>
#include <futils/static_assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <lmdb.h>

struct fdb
{
    volatile uint32_t ref_counter;
    MDB_env *env;
};

#define FDB_CALL(pdb, expr)                                                 \
    if((rc = (expr)) != MDB_SUCCESS)                                        \
    {                                                                       \
        FS_ERR("The LMDB initialization failed: \'%s\'", mdb_strerror(rc)); \
        fdb_release(pdb);                                                   \
        return 0;                                                           \
    }

fdb_t* fdb_open(char const *path, uint32_t max_dbs, uint32_t readers, uint32_t size)
{
    int rc;

    if (!path)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fdb_t *pdb = malloc(sizeof(fdb_t));
    if (!pdb)
    {
        FS_ERR("Unable to allocate memory for DB handler");
        return 0;
    }
    memset(pdb, 0, sizeof(fdb_t));

    pdb->ref_counter = 1;

    FDB_CALL(pdb, mdb_env_create(&pdb->env));
    FDB_CALL(pdb, mdb_env_set_maxreaders(pdb->env, readers));
    FDB_CALL(pdb, mdb_env_set_mapsize(pdb->env, size));
    FDB_CALL(pdb, mdb_env_set_maxdbs(pdb->env, max_dbs));
    FDB_CALL(pdb, mdb_env_open(pdb->env, path, 0, 0664));

    return pdb;
}

fdb_t* fdb_retain(fdb_t *pdb)
{
    if (pdb)
        pdb->ref_counter++;
    else
        FS_ERR("Invalid DB handler");
    return pdb;
}

void fdb_release(fdb_t *pdb)
{
    if (pdb)
    {
        if (!pdb->ref_counter)
            FS_ERR("Invalid message bus");
        else if (!--pdb->ref_counter)
        {
            mdb_env_close(pdb->env);
            free(pdb);
        }
    }
    else
        FS_ERR("Invalid message bus");
}

bool fdb_transaction_start(fdb_t *pdb, fdb_transaction_t *ptransaction)
{
    if (!pdb || !ptransaction)
        return false;
    MDB_txn *txn;
    int rc = mdb_txn_begin(pdb->env, 0, 0, &txn);
    if(rc != MDB_SUCCESS)
    {
        FS_ERR("The LMDB transaction wasn't started: \'%s\'", mdb_strerror(rc));
        return false;
    }
    ptransaction->pdb = fdb_retain(pdb);
    ptransaction->ptransaction = txn;

    return true;
}

void fdb_transaction_commit(fdb_transaction_t *transaction)
{
    MDB_txn *txn = (MDB_txn*)transaction->ptransaction;
    int rc = mdb_txn_commit(txn);
    if(rc != MDB_SUCCESS)
        FS_ERR("The LMDB transaction wasn't committed: \'%s\'", mdb_strerror(rc));
    fdb_release(transaction->pdb);
    memset(transaction, 0, sizeof *transaction);
}

void fdb_transaction_abort(fdb_transaction_t *transaction)
{
    MDB_txn *txn = (MDB_txn*)transaction->ptransaction;
    mdb_txn_abort(txn);
    fdb_release(transaction->pdb);
    memset(transaction, 0, sizeof *transaction);
}

bool fdb_map_open(fdb_transaction_t *transaction, char const *name, uint32_t flags, fdb_map_t *pmap)
{
    if (!transaction || !name || !pmap)
        return false;

    uint32_t mdb_flags = 0;
    if (flags & FDB_MAP_CREATE)           mdb_flags |= MDB_CREATE;
    if (flags & FDB_MAP_MULTI)            mdb_flags |= MDB_DUPSORT;
    if (flags & FDB_MAP_INTEGERKEY)       mdb_flags |= MDB_INTEGERKEY;
    if (flags & FDB_MAP_FIXED_SIZE_VALUE) mdb_flags |= MDB_DUPFIXED;
    if (flags & FDB_MAP_INTEGERVAL)       mdb_flags |= MDB_INTEGERDUP;

    MDB_txn *txn = (MDB_txn*)transaction->ptransaction;
    MDB_dbi dbi;
    int rc = mdb_dbi_open(txn, name, mdb_flags, &dbi);
    if(rc != MDB_SUCCESS)
    {
        FS_ERR("Unable to open the LMDB database: \'%s\'", mdb_strerror(rc));
        return false;
    }

    pmap->transaction = transaction;
    pmap->dbmap = (unsigned int)dbi;

    return true;
}

void fdb_map_close(fdb_map_t *pmap)
{
    if (pmap)
    {
        MDB_dbi dbi = (MDB_dbi)pmap->dbmap;
        mdb_dbi_close(pmap->transaction->pdb->env, dbi);
    }
}

FSTATIC_ASSERT(sizeof(fdb_data_t) == sizeof(MDB_val));
FSTATIC_ASSERT(offsetof(fdb_data_t, size) == offsetof(MDB_val, mv_size));
FSTATIC_ASSERT(offsetof(fdb_data_t, data) == offsetof(MDB_val, mv_data));

bool fdb_map_put(fdb_map_t *pmap, fdb_data_t const *key, fdb_data_t const *value)
{
    if (!pmap || !key || !value)
        return false;
    MDB_txn *txn = (MDB_txn*)pmap->transaction->ptransaction;
    MDB_dbi dbi = (MDB_dbi)pmap->dbmap;

    int rc = mdb_put(txn, dbi, (MDB_val*)key, (MDB_val*)value, 0);
    if(rc != MDB_SUCCESS)
    {
        FS_ERR("Unable to put data into the LMDB database: \'%s\'", mdb_strerror(rc));
        return false;
    }

    return true;
}

bool fdb_map_put_unique(fdb_map_t *pmap, fdb_data_t const *key, fdb_data_t const *value)
{
    if (!pmap || !key || !value)
        return false;
    MDB_txn *txn = (MDB_txn*)pmap->transaction->ptransaction;
    MDB_dbi dbi = (MDB_dbi)pmap->dbmap;

    int rc = mdb_put(txn, dbi, (MDB_val*)key, (MDB_val*)value, MDB_NOOVERWRITE);

    if (rc == MDB_KEYEXIST)
        return false;

    if(rc != MDB_SUCCESS)
    {
        FS_ERR("Unable to put data into the LMDB database: \'%s\'", mdb_strerror(rc));
        return false;
    }

    return true;
}

bool fdb_map_get(fdb_map_t *pmap, fdb_data_t const *key, fdb_data_t *value)
{
    if (!pmap || !key || !value)
        return false;
    MDB_txn *txn = (MDB_txn*)pmap->transaction->ptransaction;
    MDB_dbi dbi = (MDB_dbi)pmap->dbmap;

    int rc = mdb_get(txn, dbi, (MDB_val*)key, (MDB_val*)value);

    if (rc == MDB_NOTFOUND)
        return false;

    if(rc != MDB_SUCCESS)
    {
        FS_ERR("Unable to put data into the LMDB database: \'%s\'", mdb_strerror(rc));
        return false;
    }

    return true;
}
