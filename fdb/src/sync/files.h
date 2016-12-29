#ifndef FSYNC_FILES_H_FDB
#define FSYNC_FILES_H_FDB
#include <futils/md5.h>
#include <futils/uuid.h>
#include <config.h>
#include <time.h>

#define FINVALID_ID (~0u)

typedef struct
{
    uint32_t id;                // Unique for node id
    char     path[FMAX_PATH];   // Path

    time_t   mod_time;          // Modification time
    time_t   sync_time;         // Synchronization time
    fmd5_t   digest;            // MD5 sum
    uint64_t size;              // File size.
    bool     is_exist;          // File is exist. If not exist, it should be downloaded/sent.
} ffile_info_t;

typedef struct fdb_syncfiles_iterator fdb_sync_files_iterator_t;

bool fdb_sync_file_add(fuuid_t const *uuid, ffile_info_t const *info);
bool fdb_sync_file_del(fuuid_t const *uuid, char const *path);
bool fdb_sync_file_get(fuuid_t const *uuid, char const *path, ffile_info_t *info);
bool fdb_sync_file_del_all(fuuid_t const *uuid);
bool fdb_sync_file_update(fuuid_t const *uuid, ffile_info_t const *info);
bool fdb_sync_file_path(fuuid_t const *uuid, uint32_t id, char *path, size_t size);

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

#endif
