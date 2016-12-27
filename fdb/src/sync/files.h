#ifndef FSYNC_FILES_H_FDB
#define FSYNC_FILES_H_FDB
#include <futils/md5.h>
#include <futils/uuid.h>
#include <time.h>

enum
{
    FDB_MAX_PATH = 1024 // Max file path length
};

typedef struct
{
    time_t  mod_time;
    time_t  sync_time;
    fmd5_t  digest;
    char    path[FDB_MAX_PATH];
} ffile_info_t;

typedef struct fdb_syncfiles_iterator fdb_sync_files_iterator_t;

bool fdb_sync_file_add(fuuid_t const *uuid, ffile_info_t const *info);
bool fdb_sync_file_del(fuuid_t const *uuid, char const *path);
bool fdb_sync_file_del_all(fuuid_t const *uuid);
bool fdb_sync_file_update(fuuid_t const *uuid, ffile_info_t const *info);

fdb_sync_files_iterator_t *fdb_sync_files_iterator(fuuid_t const *uuid);
void                       fdb_sync_files_iterator_free(fdb_sync_files_iterator_t *);
bool                       fdb_sync_files_iterator_first(fdb_sync_files_iterator_t *, ffile_info_t *);
bool                       fdb_sync_files_iterator_next(fdb_sync_files_iterator_t *, ffile_info_t *);

#endif
