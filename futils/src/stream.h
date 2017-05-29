#ifndef STREAM_H_FUTILS
#define STREAM_H_FUTILS
#include <stddef.h>
#include <stdbool.h>

typedef struct fistream fistream_t;
typedef struct fostream fostream_t;

typedef fistream_t* (*fistream_retain_fn_t) (fistream_t *);
typedef void        (*fistream_release_fn_t)(fistream_t *);
typedef size_t      (*fistream_read_fn_t)   (fistream_t *, char *, size_t);
typedef bool        (*fistream_seek_fn_t)   (fistream_t *, size_t);

typedef fostream_t* (*fostream_retain_fn_t) (fostream_t *);
typedef void        (*fostream_release_fn_t)(fostream_t *);
typedef size_t      (*fostream_write_fn_t)  (fostream_t *, char const *, size_t);
typedef bool        (*fostream_seek_fn_t)   (fostream_t *, size_t);

struct fistream
{
    fistream_retain_fn_t  retain;
    fistream_release_fn_t release;
    fistream_read_fn_t    read;
    fistream_seek_fn_t    seek;
};

struct fostream
{
    fostream_retain_fn_t  retain;
    fostream_release_fn_t release;
    fostream_write_fn_t   write;
    fostream_seek_fn_t    seek;
};

#endif
