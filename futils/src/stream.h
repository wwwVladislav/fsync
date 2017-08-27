#ifndef STREAM_H_FUTILS
#define STREAM_H_FUTILS
#include <stddef.h>
#include <stdbool.h>

typedef enum
{
    FSTREAM_STATUS_OK = 0,
    FSTREAM_STATUS_INIT,
    FSTREAM_STATUS_CLOSED,
    FSTREAM_STATUS_INVALID
} fstream_status_t;

typedef struct fistream fistream_t;
typedef struct fostream fostream_t;

typedef fistream_t*         (*fistream_retain_fn_t) (fistream_t *);
typedef void                (*fistream_release_fn_t)(fistream_t *);
typedef size_t              (*fistream_read_fn_t)   (fistream_t *, char *, size_t);
typedef bool                (*fistream_seek_fn_t)   (fistream_t *, size_t);
typedef fstream_status_t    (*fistream_status_fn_t) (fistream_t *);

typedef fostream_t*         (*fostream_retain_fn_t) (fostream_t *);
typedef void                (*fostream_release_fn_t)(fostream_t *);
typedef size_t              (*fostream_write_fn_t)  (fostream_t *, char const *, size_t);
typedef bool                (*fostream_seek_fn_t)   (fostream_t *, size_t);
typedef fstream_status_t    (*fostream_status_fn_t) (fostream_t *);

struct fistream
{
    fistream_retain_fn_t  retain;
    fistream_release_fn_t release;
    fistream_read_fn_t    read;
    fistream_seek_fn_t    seek;
    fistream_status_fn_t  status;
};

struct fostream
{
    fostream_retain_fn_t  retain;
    fostream_release_fn_t release;
    fostream_write_fn_t   write;
    fostream_seek_fn_t    seek;
    fostream_status_fn_t  status;
};

typedef struct fmem_iostream fmem_iostream_t;

enum
{
    FMEM_BLOCK_SIZE = 65536u
};

fmem_iostream_t *fmem_iostream          (size_t block_size);
fmem_iostream_t *fmem_iostream_retain   (fmem_iostream_t *piostream);
void             fmem_iostream_release  (fmem_iostream_t *piostream);
fistream_t      *fmem_istream           (fmem_iostream_t *piostream);
fostream_t      *fmem_ostream           (fmem_iostream_t *piostream);

#endif
