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
    fring_queue_notify_t    notify;
    unsigned                capacity;
    volatile unsigned       start;
    volatile unsigned       end;
    char                    buf[1];
};

fring_queue_t *fring_queue_create(char *buf, unsigned size, fring_queue_notify_t fn)
{
    if (!buf || !fn)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    if (size < sizeof(fring_queue_t))
    {
        FS_ERR("The queue buffer size is too small");
        return 0;
    }

    fring_queue_t *pqueue = (fring_queue_t*)buf;
    pqueue->status = FRING_QUEUE_UNINITIALIZED;
    pqueue->notify = fn;

    pqueue->capacity = size - offsetof(fring_queue_t, buf);
    pqueue->start = 0;
    pqueue->end = 0;
    pqueue->status = FRING_QUEUE_INITIALIZED;
    return pqueue;
}

void fring_queue_free(fring_queue_t *pqueue)
{
    if (pqueue)
        pqueue->status = FRING_QUEUE_DESTROYED;
}

bool fring_queue_push_back(fring_queue_t *pqueue, char const *data, unsigned size)
{
    if (!pqueue || pqueue->status != FRING_QUEUE_INITIALIZED)
    {
        FS_ERR("Invalid queue");
        return false;
    }

    unsigned const data_size = sizeof(unsigned) + size;

    if (pqueue->end >= pqueue->start)
    {
        if (data_size <= pqueue->capacity - pqueue->end)
        {
            memcpy(pqueue->buf + pqueue->end, &size, sizeof size);
            memcpy(pqueue->buf + pqueue->end + sizeof size, data, size);
            pqueue->end += data_size;
            pqueue->notify();
            return true;
        }
        else if (sizeof size <= pqueue->capacity - pqueue->end
                 && size < pqueue->start)
        {
            memcpy(pqueue->buf + pqueue->end, &size, sizeof size);
            memcpy(pqueue->buf, data, size);
            pqueue->end = size;
            pqueue->notify();
            return true;
        }
        else if (data_size < pqueue->start)
        {
            memcpy(pqueue->buf, &size, sizeof size);
            memcpy(pqueue->buf + sizeof size, data, size);
            pqueue->end = data_size;
            pqueue->notify();
            return true;
        }
        else
        {
            FS_WARN("There is no free space.");
            return false;
        }
    }
    else
    {
        if (data_size + pqueue->end < pqueue->start)
        {
            memcpy(pqueue->buf + pqueue->end, &size, sizeof size);
            memcpy(pqueue->buf + pqueue->end + sizeof size, data, size);
            pqueue->end += data_size;
            pqueue->notify();
            return true;
        }
        else
        {
            FS_WARN("There is no free space.");
            return false;
        }
    }

    return true;
}

bool fring_queue_front(fring_queue_t *pqueue, char **pdata, unsigned *psize)
{
    if (!pqueue || pqueue->status != FRING_QUEUE_INITIALIZED)
    {
        FS_ERR("Invalid queue");
        return false;
    }

    if (!pdata || !psize)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    if (pqueue->end == pqueue->start)
    {
        *psize = 0u;
        return true;    // No data
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

    return true;
}

bool fring_queue_pop_front(fring_queue_t *pqueue)
{
    if (!pqueue || pqueue->status != FRING_QUEUE_INITIALIZED)
    {
        FS_ERR("Invalid queue");
        return false;
    }

    if (pqueue->end == pqueue->start)
    {
        FS_WARN("Queue is empty.");
        return false;
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

    return true;
}
