#ifndef DIRS_H_FDB
#define DIRS_H_FDB
#include <fcommon/limits.h>
#include <futils/uuid.h>
#include <stdbool.h>
#include "../db.h"

typedef struct fdb_dirs fdb_dirs_t;
typedef struct fdb_dirs_iterator fdb_dirs_iterator_t;

fdb_dirs_t *fdb_dirs(fdb_transaction_t *transaction);
fdb_dirs_t *fdb_dirs_retain(fdb_dirs_t *pdirs);
void        fdb_dirs_release(fdb_dirs_t *pdirs);
bool        fdb_dirs_add(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, char const *path, uint32_t *id);
bool        fdb_dirs_get_id(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, char const *path, uint32_t *id);
bool        fdb_dirs_del(fdb_dirs_t *pdirs, fdb_transaction_t *transaction, uint32_t id);

typedef struct
{
    uint32_t id;                // Unique dir id for node
    char     path[FMAX_PATH];   // Path
} fdir_info_t;

fdb_dirs_iterator_t *fdb_dirs_iterator(fdb_dirs_t *dirs, fdb_transaction_t *transaction);
void                 fdb_dirs_iterator_free(fdb_dirs_iterator_t *);
bool                 fdb_dirs_iterator_first(fdb_dirs_iterator_t *, fdir_info_t *);
bool                 fdb_dirs_iterator_next(fdb_dirs_iterator_t *, fdir_info_t *);

#endif
