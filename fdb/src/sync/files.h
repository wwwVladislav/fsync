#ifndef FSYNC_FILES_H_FDB
#define FSYNC_FILES_H_FDB
#include <futils/md5.h>
#include <futils/uuid.h>
#include <fcommon/limits.h>
#include <time.h>
#include "../db.h"
#include "ids.h"

typedef enum
{
    FFILE_IS_EXIST              = 1 << 0,
    FFILE_DIGEST_IS_CALCULATED  = 1 << 1,
} ffile_status_t;

typedef struct
{
    uint32_t id;                // Unique file id for node
    char     path[FMAX_PATH];   // Path
    time_t   mod_time;          // Modification time
    time_t   sync_time;         // Synchronization time
    fmd5_t   digest;            // MD5 sum
    uint64_t size;              // File size
    uint32_t status;            // File status.
} ffile_info_t;

typedef struct fdb_files_iterator fdb_files_iterator_t;
typedef struct fdb_files_diff_iterator fdb_files_diff_iterator_t;
typedef struct fdb_files_map fdb_files_map_t;

fdb_files_map_t *fdb_files(fdb_transaction_t *transaction, fuuid_t const *uuid);
fdb_files_map_t *fdb_files_ex(fdb_transaction_t *transaction, fuuid_t const *uuid, bool ids_generator);
fdb_files_map_t *fdb_files_retain(fdb_files_map_t *files_map);
void fdb_files_release(fdb_files_map_t *files_map);

bool fdb_files_statuses(fdb_transaction_t *transaction, fuuid_t const *uuid, fdb_map_t *pmap);

bool fdb_file_add(fdb_files_map_t *files_map, fdb_transaction_t *transaction, ffile_info_t *info);
bool fdb_file_add_unique(fdb_files_map_t *files_map, fdb_transaction_t *transaction, ffile_info_t *info);
bool fdb_file_del(fdb_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id);
bool fdb_file_get(fdb_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, ffile_info_t *info);
bool fdb_file_id(fdb_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, uint32_t *id);
bool fdb_file_get_by_path(fdb_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, ffile_info_t *info);
bool fdb_file_del_all(fuuid_t const *uuid);
bool fdb_file_path(fdb_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, char *path, size_t size);

typedef enum
{
    FDB_FILE_ABSENT = 0,
    FDB_DIFF_CONTENT
} fdb_diff_kind_t;

fdb_files_iterator_t      *fdb_files_iterator(fdb_files_map_t *files_map, fdb_transaction_t *transaction);
void                       fdb_files_iterator_free(fdb_files_iterator_t *);
bool                       fdb_files_iterator_first(fdb_files_iterator_t *, ffile_info_t *);
bool                       fdb_files_iterator_next(fdb_files_iterator_t *, ffile_info_t *);

fdb_files_diff_iterator_t *fdb_files_diff_iterator(fdb_files_map_t *map_1, fdb_files_map_t *map_2, fdb_transaction_t *transaction);
void                       fdb_files_diff_iterator_free(fdb_files_diff_iterator_t *);
bool                       fdb_files_diff_iterator_first(fdb_files_diff_iterator_t *, ffile_info_t *, fdb_diff_kind_t *);
bool                       fdb_files_diff_iterator_next(fdb_files_diff_iterator_t *, ffile_info_t *, fdb_diff_kind_t *);

#endif
