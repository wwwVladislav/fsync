#include "msgbus.h"
#include "queue.h"
#include "log.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>

typedef struct
{
    uint32_t        msg_type;
    fmsg_handler_t  handler;
    void           *user_data;
} fmsgbus_handler_t;

enum
{
    FMSGBUS_MAX_HANDLERS = 64
};

struct fmsgbus
{
    volatile uint32_t ref_counter;
    pthread_mutex_t   mutex;
    fmsgbus_handler_t handlers[FMSGBUS_MAX_HANDLERS];
};

#define fmsgbus_push_lock(mutex)                    \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fmsgbus_pop_lock() pthread_cleanup_pop(1)

ferr_t fmsgbus_create(fmsgbus_t **ppmsgbus)
{
    if (!ppmsgbus)
    {
        FS_ERR("Invalid arguments");
        return FERR_INVALID_ARG;
    }

    fmsgbus_t *pmsgbus = malloc(sizeof(fmsgbus_t));
    if (!pmsgbus)
    {
        FS_ERR("Unable to allocate memory for messages bus");
        return FERR_NO_MEM;
    }
    memset(pmsgbus, 0, sizeof(fmsgbus_t));

    pmsgbus->ref_counter = 1;

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    pmsgbus->mutex = mutex_initializer;

    *ppmsgbus = pmsgbus;

    return FSUCCESS;
}

fmsgbus_t *fmsgbus_retain(fmsgbus_t *pmsgbus)
{
    if (pmsgbus)
        pmsgbus->ref_counter++;
    else
        FS_ERR("Invalid message bus");
    return pmsgbus;
}

void fmsgbus_release(fmsgbus_t *pmsgbus)
{
    if (pmsgbus)
    {
        if (!pmsgbus->ref_counter)
            FS_ERR("Invalid message bus");
        else
        {
            if (!--pmsgbus->ref_counter)
                free(pmsgbus);
        }
    }
    else
        FS_ERR("Invalid message bus");
}

ferr_t fmsgbus_subscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler, void *user_data)
{
    if (!pmsgbus
        || !handler)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    int i = 0;

    fmsgbus_push_lock(pmsgbus->mutex);

    for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (!pmsgbus->handlers[i].handler)
        {
            pmsgbus->handlers[i].msg_type = msg_type;
            pmsgbus->handlers[i].handler = handler;
            pmsgbus->handlers[i].user_data = user_data;
            break;
        }
    }

    fmsgbus_pop_lock();

    return i >= FMSGBUS_MAX_HANDLERS ? FFAIL : FSUCCESS;
}

ferr_t fmsgbus_unsubscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler)
{
    if (!pmsgbus
        || !handler)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    int i = 0;

    fmsgbus_push_lock(pmsgbus->mutex);

    for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (pmsgbus->handlers[i].handler == handler
            && pmsgbus->handlers[i].msg_type == msg_type)
        {
            pmsgbus->handlers[i].msg_type = 0;
            pmsgbus->handlers[i].handler = 0;
            pmsgbus->handlers[i].user_data = 0;
            break;
        }
    }

    fmsgbus_pop_lock();

    return i >= FMSGBUS_MAX_HANDLERS ? FFAIL : FSUCCESS;
}

ferr_t fmsgbus_publish(fmsgbus_t *pmsgbus, uint32_t msg_type, void const *msg, uint32_t size)
{
    if (!pmsgbus)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    fmsgbus_push_lock(pmsgbus->mutex);

    for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (pmsgbus->handlers[i].msg_type == msg_type
            && pmsgbus->handlers[i].handler)
            pmsgbus->handlers[i].handler(pmsgbus->handlers[i].user_data, msg_type, msg, size);
    }

    fmsgbus_pop_lock();

    return FSUCCESS;
}
