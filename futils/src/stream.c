#include "stream.h"
#include "vector.h"
#include "log.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// mem_iostream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

enum
{
    FMEM_IOSTREAM_MIN_CAPACITY = 64
};

struct fmem_iostream
{
    volatile uint32_t   ref_counter;
    fvector_t          *blocks;
    size_t              block_size;
    size_t              offset;
    size_t              data_size;
};

fmem_iostream_t *fmem_iostream(size_t block_size)
{
    if (!block_size)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fmem_iostream_t *piostream = malloc(sizeof(fmem_iostream_t));
    if (!piostream)
    {
        FS_ERR("Unable to allocate memory for iostream");
        return 0;
    }
    memset(piostream, 0, sizeof(fmem_iostream_t));

    piostream->ref_counter = 1;
    piostream->block_size = block_size;
    piostream->blocks = fvector(sizeof(char*), 0, FMEM_IOSTREAM_MIN_CAPACITY);
    if (!piostream->blocks)
    {
        FS_ERR("Unable to allocate blocks table for iostream");
        free(piostream);
        return 0;
    }

    return piostream;
}

fmem_iostream_t *fmem_iostream_retain(fmem_iostream_t *piostream)
{
    if (piostream)
        piostream->ref_counter++;
    else
        FS_ERR("Invalid iostream");
    return piostream;
}

void fmem_iostream_release(fmem_iostream_t *piostream)
{
    if (piostream)
    {
        if (!piostream->ref_counter)
            FS_ERR("Invalid iostream");
        else if (!--piostream->ref_counter)
        {
            for(size_t i = 0; i < fvector_size(piostream->blocks); ++i)
                free(*(char**)fvector_at(piostream->blocks, i));
            fvector_release(piostream->blocks);
            memset(piostream, 0, sizeof *piostream);
            free(piostream);
        }
    }
    else
        FS_ERR("Invalid iostream");
}

static size_t fmem_iostream_write(fmem_iostream_t *piostream, char const *data, size_t size)
{
    if (!data || !size)
        return 0u;

    size_t last_block_idx = (piostream->offset + piostream->data_size) / piostream->block_size;
    size_t last_block_offset = (piostream->offset + piostream->data_size) % piostream->block_size;
    size_t written_size = 0;

    if (last_block_idx == fvector_size(piostream->blocks))
    {
        char *block = malloc(piostream->block_size);
        if (!block)
        {
            FS_ERR("Memory block allocation failed");
            return 0;
        }

        if (!fvector_push_back(&piostream->blocks, &block))
        {
            FS_ERR("Unable to push memory block to vector");
            free(block);
            return 0;
        }

        last_block_offset = 0;
    }

    while(written_size < size)
    {
        assert(fvector_size(piostream->blocks) > last_block_idx);

        char *block = *(char**)fvector_at(piostream->blocks, last_block_idx);
        if (!block)
        {
            FS_ERR("Invalid memory blocks vector");
            return 0;
        }

        size_t available_space = piostream->block_size - last_block_offset;
        size_t available_size = size - written_size;
        size_t write_size = available_size >= available_space ? available_space : available_size;

        memcpy(block + last_block_offset, data + written_size, write_size);
        written_size += write_size;
        last_block_offset += write_size;
        piostream->data_size += write_size;

        if (last_block_offset >= piostream->block_size)
        {
            char *block = malloc(piostream->block_size);
            if (!block)
            {
                FS_ERR("Memory block allocation failed");
                return write_size;
            }

            if (!fvector_push_back(&piostream->blocks, &block))
            {
                FS_ERR("Unable to push memory block to vector");
                free(block);
                return write_size;
            }

            last_block_offset = 0;
            last_block_idx++;
        }
    }

    return written_size;
}

static size_t fmem_iostream_read(fmem_iostream_t *piostream, char *data, size_t size)
{
    // TODO
    return 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// mem_ostream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef struct
{
    fostream_t          ostream;
    volatile uint32_t   ref_counter;
    fmem_iostream_t    *piostream;
} fmem_ostream_t;

static fostream_t* fmem_ostream_retain(fostream_t *postream)
{
    if (postream)
    {
        fmem_ostream_t *pmem_ostream = (fmem_ostream_t *)postream;
        pmem_ostream->ref_counter++;
    }
    else
        FS_ERR("Invalid ostream");
    return postream;
}

static void fmem_ostream_release(fostream_t *postream)
{
    if (postream)
    {
        fmem_ostream_t *pmem_ostream = (fmem_ostream_t *)postream;
        if (!pmem_ostream->ref_counter)
            FS_ERR("Invalid ostream");
        else if (!--pmem_ostream->ref_counter)
        {
            fmem_iostream_release(pmem_ostream->piostream);
            memset(postream, 0, sizeof *postream);
            free(postream);
        }
    }
    else
        FS_ERR("Invalid ostream");
}

static size_t fmem_ostream_write(fostream_t *postream, char const *data, size_t size)
{
    if (!postream)
    {
        FS_ERR("Invalid ostream");
        return 0;
    }
    fmem_ostream_t *pmem_ostream = (fmem_ostream_t *)postream;
    return fmem_iostream_write(pmem_ostream->piostream, data, size);
}

fostream_t *fmem_ostream(fmem_iostream_t *piostream)
{
    if (!piostream)
    {
        FS_ERR("Invalid iostream");
        return 0;
    }

    fmem_ostream_t *postream = malloc(sizeof(fmem_ostream_t));
    if (!postream)
    {
        FS_ERR("Unable to allocate memory for ostream");
        return 0;
    }

    postream->ostream.retain = fmem_ostream_retain;
    postream->ostream.release = fmem_ostream_release;
    postream->ostream.write = fmem_ostream_write;
    postream->ostream.seek = 0; // Not supported
    postream->ref_counter = 1;
    postream->piostream = fmem_iostream_retain(piostream);

    return (fostream_t *)postream;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// mem_istream
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef struct
{
    fistream_t          istream;
    volatile uint32_t   ref_counter;
    fmem_iostream_t    *piostream;
} fmem_istream_t;

static fistream_t* fmem_istream_retain(fistream_t *pistream)
{
    if (pistream)
    {
        fmem_istream_t *pmem_istream = (fmem_istream_t *)pistream;
        pmem_istream->ref_counter++;
    }
    else
        FS_ERR("Invalid istream");
    return pistream;
}

static void fmem_istream_release(fistream_t *pistream)
{
    if (pistream)
    {
        fmem_istream_t *pmem_istream = (fmem_istream_t *)pistream;
        if (!pmem_istream->ref_counter)
            FS_ERR("Invalid istream");
        else if (!--pmem_istream->ref_counter)
        {
            fmem_iostream_release(pmem_istream->piostream);
            memset(pistream, 0, sizeof *pistream);
            free(pistream);
        }
    }
    else
        FS_ERR("Invalid istream");
}

static size_t fmem_istream_read(fistream_t *pistream, char *data, size_t size)
{
    if (!pistream)
    {
        FS_ERR("Invalid istream");
        return 0;
    }
    fmem_istream_t *pmem_istream = (fmem_istream_t *)pistream;
    return fmem_iostream_read(pmem_istream->piostream, data, size);
}

fistream_t *fmem_istream(fmem_iostream_t *piostream)
{
    if (!piostream)
    {
        FS_ERR("Invalid iostream");
        return 0;
    }

    fmem_istream_t *pistream = malloc(sizeof(fmem_istream_t));
    if (!pistream)
    {
        FS_ERR("Unable to allocate memory for istream");
        return 0;
    }

    pistream->istream.retain = fmem_istream_retain;
    pistream->istream.release = fmem_istream_release;
    pistream->istream.read = fmem_istream_read;
    pistream->istream.seek = 0; // Not supported
    pistream->ref_counter = 1;
    pistream->piostream = fmem_iostream_retain(piostream);

    return (fistream_t *)pistream;
}
