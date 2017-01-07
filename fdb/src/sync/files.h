#ifndef FSYNC_FILES_H_FDB
#define FSYNC_FILES_H_FDB
#include <futils/md5.h>
#include <futils/uuid.h>
#include <config.h>
#include <time.h>

#define FINVALID_ID (~0u)

typedef enum
{
    FFILE_IS_EXIST              = 1 << 0,
    FFILE_DIGEST_IS_CALCULATED  = 1 << 1,
} ffile_status_t;

typedef struct
{
    char     path[FMAX_PATH];   // Path
    uint32_t id;                // Unique file id for node
    time_t   mod_time;          // Modification time
    time_t   sync_time;         // Synchronization time
    fmd5_t   digest;            // MD5 sum
    uint64_t size;              // File size
    uint32_t status;            // File status.
} ffile_info_t;

typedef struct fdb_syncfiles_iterator fdb_sync_files_iterator_t;

bool fdb_sync_file_add(fuuid_t const *uuid, ffile_info_t *info);
bool fdb_sync_file_add_unique(fuuid_t const *uuid, ffile_info_t *info);
bool fdb_sync_file_del(fuuid_t const *uuid, char const *path);
bool fdb_sync_file_get(fuuid_t const *uuid, char const *path, ffile_info_t *info);
bool fdb_sync_file_get_if_not_exist(fuuid_t const *uuid, ffile_info_t *info);
bool fdb_sync_file_del_all(fuuid_t const *uuid);
bool fdb_sync_file_update(fuuid_t const *uuid, ffile_info_t const *info);
bool fdb_sync_file_path(fuuid_t const *uuid, uint32_t id, char *path, size_t size);
bool fdb_sync_start(uint32_t id, uint32_t threshold_delta_time, uint32_t requested_parts_threshold, uint64_t size);
bool fdb_sync_next_part(uint32_t id, uint32_t *part, bool *completed);
void fdb_sync_part_received(fuuid_t const *uuid, uint32_t id, uint32_t part);

typedef enum
{
    FDB_FILE_ABSENT = 0,
    FDB_DIFF_CONTENT
} fdb_diff_kind_t;

fdb_sync_files_iterator_t *fdb_sync_files_iterator(fuuid_t const *uuid);
fdb_sync_files_iterator_t *fdb_sync_files_iterator_diff(fuuid_t const *uuid0, fuuid_t const *uuid1);    // return differences in files list for uuid0 and uuid1
void                       fdb_sync_files_iterator_free(fdb_sync_files_iterator_t *);
bool                       fdb_sync_files_iterator_first(fdb_sync_files_iterator_t *, ffile_info_t *, fdb_diff_kind_t *);
bool                       fdb_sync_files_iterator_next(fdb_sync_files_iterator_t *, ffile_info_t *, fdb_diff_kind_t *);
bool                       fdb_sync_files_iterator_uuid(fdb_sync_files_iterator_t *, ffile_info_t *);
uint32_t                   fdb_get_uuids(fuuid_t uuids[FMAX_CONNECTIONS_NUM]);
uint32_t                   fdb_get_uuids_where_file_is_exist(fuuid_t uuids[FMAX_CONNECTIONS_NUM], uint32_t ids[FMAX_CONNECTIONS_NUM], char const *path);

#endif
