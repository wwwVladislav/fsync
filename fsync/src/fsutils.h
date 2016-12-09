#ifndef FSUTILS_H_FSYNC
#define FSUTILS_H_FSYNC
#include "config.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct fsdir fsdir_t;
typedef struct fsfile fsfile_t;
typedef struct fsiterator fsiterator_t;
typedef struct fsdir_listener fsdir_listener_t;

typedef enum
{
    FS_BLK = 0,
    FS_CHR,
    FS_DIR,
    FS_FIFO,
    FS_LNK,
    FS_REG,
    FS_SOCK,
    FS_UNKNOWN
} fsdir_entry_t;

typedef struct
{
    fsdir_entry_t   type;                   // type
    unsigned short  namlen;                 // Length of name in name
    char            name[FSMAX_FILENAME];   // File name
} dirent_t;

typedef enum fsdir_action
{
    FSDIR_ACTION_ADDED = 0,
    FSDIR_ACTION_REMOVED,
    FSDIR_ACTION_MODIFIED,
    FSDIR_ACTION_RENAMED,
    FSDIR_ACTION_UNKNOWN
} fsdir_action_t;

typedef void (*fsdir_evt_handler_t)(fsdir_action_t, char const *, void *);

fsdir_t          *fsdir_open(char const *);
void              fsdir_close(fsdir_t *);
bool              fsdir_read(fsdir_t *, dirent_t *);
fsiterator_t     *fsdir_iterator(char const *);
void              fsdir_iterator_free(fsiterator_t *);
bool              fsdir_iterator_next(fsiterator_t *, dirent_t *);
size_t            fsdir_iterator_directory(fsiterator_t *, char *, size_t);

fsdir_listener_t *fsdir_listener_create();
void              fsdir_listener_free(fsdir_listener_t *listener);
bool              fsdir_listener_add_path(fsdir_listener_t *listener, char const *path);
bool              fsdir_listener_reg_handler(fsdir_listener_t *listener, fsdir_evt_handler_t handler, void *arg);

#endif
