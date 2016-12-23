#include "msgbus.h"
#include "queue.h"
#include "log.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct
{
    uint32_t        msg_type;
    fmsg_handler_t  handler;
    void           *param;
} fmsgbus_handler_t;

typedef struct
{
    volatile bool   is_active;
    pthread_t       thread;
} fmsgbus_thread_t;

typedef struct
{
    fmsgbus_t      *msgbus;
    uint32_t        thread_id;
} fmsgbus_thread_param_t;

enum
{
    FMSGBUS_MAX_HANDLERS = 256,
    FMSGBUS_MAX_THREADS  = 4
};

struct fmsgbus
{
    volatile uint32_t ref_counter;
    pthread_mutex_t   handlers_mutex;
    fmsgbus_handler_t handlers[FMSGBUS_MAX_HANDLERS];
    pthread_mutex_t   messages_mutex;
    uint8_t           buf[1024 * 1024];
    fring_queue_t    *messages;
    fmsgbus_thread_t  threads[FMSGBUS_MAX_THREADS];
};

#define fmsgbus_push_lock(mutex)                    \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fmsgbus_pop_lock() pthread_cleanup_pop(1)

static void *fmsgbus_thread(void *param)
{
    fmsgbus_thread_param_t *thread_param = (fmsgbus_thread_param_t *)param;
    fmsgbus_t *msgbus        = thread_param->msgbus;
    fmsgbus_thread_t *thread = &msgbus->threads[thread_param->thread_id];

    thread->is_active = true;

    while(thread->is_active)
    {
        // TODO
    }

    return 0;
}

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
    pmsgbus->handlers_mutex = mutex_initializer;
    pmsgbus->messages_mutex = mutex_initializer;

    ferr_t ret = fring_queue_create(pmsgbus->buf, sizeof pmsgbus->buf, &pmsgbus->messages);
    if (ret != FSUCCESS)
    {
        fmsgbus_release(pmsgbus);
        return ret;
    }

    for(uint32_t i = 0; i < FMSGBUS_MAX_THREADS; ++i)
    {
        fmsgbus_thread_param_t thread_param = { pmsgbus, i };

        int rc = pthread_create(&pmsgbus->threads[i].thread, 0, fmsgbus_thread, &thread_param);
        if (rc)
        {
            FS_ERR("Unable to create the thread. Error: %d", rc);
            fmsgbus_release(pmsgbus);
            return FFAIL;
        }

        static struct timespec const ts = { 1, 0 };
        while(!pmsgbus->threads[i].is_active)
            nanosleep(&ts, NULL);
    }

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
            {
                for(uint32_t i = 0; i < FMSGBUS_MAX_THREADS; ++i)
                {
                    pmsgbus->threads[i].is_active = false;
                    pthread_join(pmsgbus->threads[i].thread, 0);
                }

                fring_queue_free(pmsgbus->messages);
                free(pmsgbus);
            }
        }
    }
    else
        FS_ERR("Invalid message bus");
}

ferr_t fmsgbus_subscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler, void *param)
{
    if (!pmsgbus
        || !handler)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    int i = 0;

    fmsgbus_push_lock(pmsgbus->handlers_mutex);

    for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (!pmsgbus->handlers[i].handler)
        {
            pmsgbus->handlers[i].msg_type = msg_type;
            pmsgbus->handlers[i].handler = handler;
            pmsgbus->handlers[i].param = param;
            break;
        }
    }

    fmsgbus_pop_lock();

    if (i >= FMSGBUS_MAX_HANDLERS)
    {
        FS_ERR("No free space in messages handlers table");
        return FFAIL;
    }

    return FSUCCESS;
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

    fmsgbus_push_lock(pmsgbus->handlers_mutex);

    for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (pmsgbus->handlers[i].handler == handler
            && pmsgbus->handlers[i].msg_type == msg_type)
        {
            pmsgbus->handlers[i].msg_type = 0;
            pmsgbus->handlers[i].handler = 0;
            pmsgbus->handlers[i].param = 0;
            break;
        }
    }

    fmsgbus_pop_lock();

    if (i >= FMSGBUS_MAX_HANDLERS)
    {
        FS_ERR("Message handler not found in handlers table");
        return FFAIL;
    }

    return FSUCCESS;
}

ferr_t fmsgbus_publish(fmsgbus_t *pmsgbus, uint32_t msg_type, void const *msg, uint32_t size)
{
    if (!pmsgbus)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    ferr_t ret;

    fmsgbus_push_lock(pmsgbus->messages_mutex);
    ret = fring_queue_push_back(pmsgbus->messages, msg, size);
    fmsgbus_pop_lock();

    return ret;

/*
    fmsgbus_push_lock(pmsgbus->handlers_mutex);

    for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (pmsgbus->handlers[i].msg_type == msg_type
            && pmsgbus->handlers[i].handler)
            pmsgbus->handlers[i].handler(pmsgbus->handlers[i].param, msg_type, msg, size);
    }

    fmsgbus_pop_lock();
*/
    return FSUCCESS;
}
