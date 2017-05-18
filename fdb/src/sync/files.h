#ifndef FILES_H_FDB
#define FILES_H_FDB
#include <futils/uuid.h>
#include <fcommon/limits.h>
#include "../db.h"
#include <stdbool.h>

typedef struct fdb_files fdb_files_t;

typedef struct
{
    char     path[FMAX_PATH];   // Path
    uint64_t size;              // File size
} ffile_info_t;

fdb_files_t *fdb_files(fdb_transaction_t *transaction, fuuid_t const *uuid);
fdb_files_t *fdb_files_retain(fdb_files_t *files);
void         fdb_files_release(fdb_files_t *files);
bool         fdb_files_add(fdb_files_t *files, fdb_transaction_t *transaction, ffile_info_t const *info);
bool         fdb_files_find(fdb_files_t *files, fdb_transaction_t *transaction, char const *file);

#endif
