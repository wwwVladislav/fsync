#include "queue.h"
#include "log.h"
#include <stddef.h>
#include <string.h>

typedef enum
{
    FRING_QUEUE_UNINITIALIZED = 0,
    FRING_QUEUE_INITIALIZED,
    FRING_QUEUE_DESTROYED
} fring_status_t;

struct fring_queue
{
    volatile fring_status_t status;
    uint32_t                capacity;
    volatile uint32_t       start;
    volatile uint32_t       end;
    uint8_t                 buf[1];
};

ferr_t fring_queue_create(void *buf, uint32_t size, fring_queue_t **ppqueue)
{
    if (!buf
        || size < sizeof(fring_queue_t)
        || !ppqueue)
    {
        FS_ERR("Invalid arguments");
        return FINVALID_ARG;
    }

    fring_queue_t *pqueue = (fring_queue_t*)buf;
    pqueue->status = FRING_QUEUE_UNINITIALIZED;

    pqueue->capacity = size - offsetof(fring_queue_t, buf);
    pqueue->start = 0;
    pqueue->end = 0;
    pqueue->status = FRING_QUEUE_INITIALIZED;
    *ppqueue = pqueue;
    return FSUCCESS;
}

void fring_queue_free(fring_queue_t *pqueue)
{
    if (pqueue)
        pqueue->status = FRING_QUEUE_DESTROYED;
}

ferr_t fring_queue_push_back(fring_queue_t *pqueue, void const *data, uint32_t size)
{
    if (!pqueue || pqueue->status != FRING_QUEUE_INITIALIZED)
    {
        FS_ERR("Invalid queue");
        return FINVALID_ARG;
    }

    unsigned const data_size = sizeof(unsigned) + size;

    if (pqueue->end >= pqueue->start)
    {
        if (data_size <= pqueue->capacity - pqueue->end)
        {
            memcpy(pqueue->buf + pqueue->end, &size, sizeof size);
            memcpy(pqueue->buf + pqueue->end + sizeof size, data, size);
            pqueue->end += data_size;
            return FSUCCESS;
        }
        else if (sizeof size <= pqueue->capacity - pqueue->end
                 && size < pqueue->start)
        {
            memcpy(pqueue->buf + pqueue->end, &size, sizeof size);
            memcpy(pqueue->buf, data, size);
            pqueue->end = size;
            return FSUCCESS;
        }
        else if (data_size < pqueue->start)
        {
            memcpy(pqueue->buf, &size, sizeof size);
            memcpy(pqueue->buf + sizeof size, data, size);
            pqueue->end = data_size;
            return FSUCCESS;
        }
        else
        {
            FS_WARN("There is no free space.");
            return FNO_MEM;
        }
    }
    else
    {
        if (data_size + pqueue->end < pqueue->start)
        {
            memcpy(pqueue->buf + pqueue->end, &size, sizeof size);
            memcpy(pqueue->buf + pqueue->end + sizeof size, data, size);
            pqueue->end += data_size;
            return FSUCCESS;
        }
        else
        {
            FS_WARN("There is no free space.");
            return FNO_MEM;
        }
    }

    return FSUCCESS;
}

ferr_t fring_queue_front(fring_queue_t *pqueue, void **pdata, uint32_t *psize)
{
    if (!pqueue || pqueue->status != FRING_QUEUE_INITIALIZED)
    {
        FS_ERR("Invalid queue");
        return FINVALID_ARG;
    }

    if (!pdata || !psize)
    {
        FS_ERR("Invalid arguments");
        return FINVALID_ARG;
    }

    if (pqueue->end == pqueue->start)
    {
        *psize = 0u;
        return FFAIL;    // No data
    }

    if (sizeof *psize <= pqueue->capacity - pqueue->start)
    {
        memcpy(psize, pqueue->buf + pqueue->start, sizeof *psize);
        if (pqueue->start + sizeof *psize + *psize <= pqueue->capacity)
            *pdata = pqueue->buf + pqueue->start + sizeof *psize;
        else
            *pdata = pqueue->buf;
    }
    else
    {
        memcpy(psize, pqueue->buf, sizeof *psize);
        *pdata = pqueue->buf + sizeof *psize;
    }

    return FSUCCESS;
}

ferr_t fring_queue_pop_front(fring_queue_t *pqueue)
{
    if (!pqueue || pqueue->status != FRING_QUEUE_INITIALIZED)
    {
        FS_ERR("Invalid queue");
        return FINVALID_ARG;
    }

    if (pqueue->end == pqueue->start)
    {
        FS_WARN("Queue is empty.");
        return FFAIL;
    }

    unsigned size;

    if (sizeof size <= pqueue->capacity - pqueue->start)
    {
        memcpy(&size, pqueue->buf + pqueue->start, sizeof size);
        if (pqueue->start + sizeof size + size <= pqueue->capacity)
            pqueue->start += sizeof size + size;
        else
            pqueue->start = size;
    }
    else
    {
        memcpy(&size, pqueue->buf, sizeof size);
        pqueue->start = size + sizeof size;
    }

    return FSUCCESS;
}
