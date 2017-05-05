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
} fsync_file_status_t;

typedef struct
{
    uint32_t id;                // Unique file id for node
    char     path[FMAX_PATH];   // Path
    time_t   mod_time;          // Modification time
    time_t   sync_time;         // Synchronization time
    fmd5_t   digest;            // MD5 sum
    uint64_t size;              // File size
    uint32_t status;            // File status.
} fsync_file_info_t;

typedef struct fdb_sync_files_iterator fdb_sync_files_iterator_t;
typedef struct fdb_sync_files_diff_iterator fdb_sync_files_diff_iterator_t;
typedef struct fdb_sync_files_map fdb_sync_files_map_t;

fdb_sync_files_map_t *fdb_sync_files(fdb_transaction_t *transaction, fuuid_t const *uuid);
fdb_sync_files_map_t *fdb_sync_files_ex(fdb_transaction_t *transaction, fuuid_t const *uuid, bool ids_generator);
fdb_sync_files_map_t *fdb_sync_files_retain(fdb_sync_files_map_t *files_map);
void                  fdb_sync_files_release(fdb_sync_files_map_t *files_map);

bool fdb_sync_files_statuses(fdb_transaction_t *transaction, fuuid_t const *uuid, fdb_map_t *pmap);

bool fdb_sync_file_add(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, fsync_file_info_t *info);
bool fdb_sync_file_add_unique(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, fsync_file_info_t *info);
bool fdb_sync_file_del(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id);
bool fdb_sync_file_get(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, fsync_file_info_t *info);
bool fdb_sync_file_id(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, uint32_t *id);
bool fdb_sync_file_get_by_path(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, char const *path, size_t const size, fsync_file_info_t *info);
bool fdb_sync_file_del_all(fuuid_t const *uuid);
bool fdb_sync_file_path(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction, uint32_t id, char *path, size_t size);

typedef enum
{
    FDB_FILE_ABSENT = 0,
    FDB_DIFF_CONTENT
} fdb_diff_kind_t;

fdb_sync_files_iterator_t *fdb_sync_files_iterator(fdb_sync_files_map_t *files_map, fdb_transaction_t *transaction);
void                       fdb_sync_files_iterator_free(fdb_sync_files_iterator_t *);
bool                       fdb_sync_files_iterator_first(fdb_sync_files_iterator_t *, fsync_file_info_t *);
bool                       fdb_sync_files_iterator_next(fdb_sync_files_iterator_t *, fsync_file_info_t *);

fdb_sync_files_diff_iterator_t *fdb_sync_files_diff_iterator(fdb_sync_files_map_t *map_1, fdb_sync_files_map_t *map_2, fdb_transaction_t *transaction);
void                            fdb_sync_files_diff_iterator_free(fdb_sync_files_diff_iterator_t *);
bool                            fdb_sync_files_diff_iterator_first(fdb_sync_files_diff_iterator_t *, fsync_file_info_t *, fdb_diff_kind_t *);
bool                            fdb_sync_files_diff_iterator_next(fdb_sync_files_diff_iterator_t *, fsync_file_info_t *, fdb_diff_kind_t *);

#endif
