#include "msgbus.h"
#include "queue.h"
#include "log.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include <errno.h>

static struct timespec const F1_MSEC = { 0, 1000000 };

enum
{
    FMSGBUS_MSG = 0,
    FMSGBUS_SUBSCRIBE,
    FMSGBUS_UNSUBSCRIBE
};

typedef struct
{
    uint32_t        operation;
    uint32_t        msg_type;
    fmsg_t         *msg;
} fmsgbus_msg_t;

typedef struct
{
    uint32_t        operation;
    uint32_t        msg_type;
    fmsg_handler_t  handler;
    void           *param;
    sem_t          *sem;
    ferr_t         *ret;
} fmsgbus_msg_subscribe_t;

typedef struct
{
    uint32_t        operation;
    uint32_t        msg_type;
    fmsg_handler_t  handler;
    sem_t          *sem;
    ferr_t         *ret;
} fmsgbus_msg_unsubscribe_t;

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
} fmsgbus_ctrl_thread_t;

typedef struct
{
    volatile bool       is_active;
    pthread_t           thread;
    sem_t               sem;
    volatile uint32_t   msg_type;
    fmsg_t * volatile   msg;
} fmsgbus_thread_t;

typedef struct
{
    fmsgbus_t      *msgbus;
    uint32_t        thread_id;
} fmsgbus_thread_param_t;

enum
{
    FMSGBUS_MAX_HANDLERS = 256,
    FMSGBUS_MAX_THREADS  = 8
};

struct fmsgbus
{
    volatile uint32_t     ref_counter;

    fmsgbus_ctrl_thread_t ctrl_thread;
    fmsgbus_handler_t     handlers[FMSGBUS_MAX_HANDLERS];
    pthread_mutex_t       messages_mutex;
    sem_t                 messages_sem;
    uint8_t               buf[1024 * 1024];
    fring_queue_t        *messages;

    fmsgbus_thread_t      threads[FMSGBUS_MAX_THREADS];
};

#define fmsgbus_push_lock(mutex)                    \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fmsgbus_pop_lock() pthread_cleanup_pop(1)

static bool fmsgbus_msg_handle(fmsgbus_t *msgbus, uint32_t msg_type, fmsg_t *msg)
{
    for(uint32_t thread_id = 0; thread_id < FMSGBUS_MAX_THREADS; ++thread_id)
    {
        fmsgbus_thread_t *thread = &msgbus->threads[thread_id];
        if (thread->is_active
            && !thread->msg)
        {
            thread->msg_type = msg_type;
            thread->msg      = msg;
            sem_post(&thread->sem);
            return true;
        }
    }

    return false;
}

static ferr_t fmsgbus_subscribe_impl(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler, void *param)
{
    int i = 0;

    for(; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (!pmsgbus->handlers[i].handler)
        {
            pmsgbus->handlers[i].param = param;
            pmsgbus->handlers[i].handler = handler;
            pmsgbus->handlers[i].msg_type = msg_type;
            break;
        }
    }

    if (i >= FMSGBUS_MAX_HANDLERS)
    {
        FS_ERR("No free space in messages handlers table");
        return FFAIL;
    }

    return FSUCCESS;
}

static ferr_t fmsgbus_unsubscribe_impl(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler)
{
    int i = 0;

    for(; i < FMSGBUS_MAX_HANDLERS; ++i)
    {
        if (pmsgbus->handlers[i].handler == handler
            && pmsgbus->handlers[i].msg_type == msg_type)
        {
            pmsgbus->handlers[i].param = 0;
            pmsgbus->handlers[i].handler = 0;
            pmsgbus->handlers[i].msg_type = 0;
            break;
        }
    }

    if (i >= FMSGBUS_MAX_HANDLERS)
    {
        FS_ERR("Message handler not found in handlers table");
        return FFAIL;
    }

    return FSUCCESS;
}

static void *fmsgbus_ctrl_thread(void *param)
{
    fmsgbus_t *msgbus = (fmsgbus_t *)param;
    fmsgbus_ctrl_thread_t *thread = &msgbus->ctrl_thread;

    thread->is_active = true;

    while(thread->is_active)
    {
        while (sem_wait(&msgbus->messages_sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!thread->is_active)
            break;

        void *data;
        uint32_t size;

        if (fring_queue_front(msgbus->messages, &data, &size) == FSUCCESS)
        {
            switch(*(uint32_t *)data)
            {
                case FMSGBUS_MSG:
                {
                    fmsgbus_msg_t *cmsg = (fmsgbus_msg_t *)data;
                    if (fmsgbus_msg_handle(msgbus, cmsg->msg_type, cmsg->msg))
                        fring_queue_pop_front(msgbus->messages);
                    else
                    {
                        nanosleep(&F1_MSEC, NULL);
                        sem_post(&msgbus->messages_sem);
                    }
                    break;
                }

                case FMSGBUS_SUBSCRIBE:
                {
                    fmsgbus_msg_subscribe_t *msg = (fmsgbus_msg_subscribe_t *)data;
                    *msg->ret = fmsgbus_subscribe_impl(msgbus, msg->msg_type, msg->handler, msg->param);
                    sem_post(msg->sem);
                    fring_queue_pop_front(msgbus->messages);
                    break;
                }

                case FMSGBUS_UNSUBSCRIBE:
                {
                    fmsgbus_msg_unsubscribe_t *msg = (fmsgbus_msg_unsubscribe_t *)data;
                    *msg->ret = fmsgbus_unsubscribe_impl(msgbus, msg->msg_type, msg->handler);
                    sem_post(msg->sem);
                    fring_queue_pop_front(msgbus->messages);
                    break;
                }

                default:
                {
                    FS_ERR("Unknown message type");
                    fring_queue_pop_front(msgbus->messages);
                    break;
                }
            }
        }
    }
    return 0;
}

static void *fmsgbus_thread(void *param)
{
    fmsgbus_thread_param_t *thread_param = (fmsgbus_thread_param_t *)param;
    fmsgbus_t              *msgbus       = thread_param->msgbus;
    fmsgbus_thread_t       *thread       = &msgbus->threads[thread_param->thread_id];

    thread->is_active = true;

    while(thread->is_active)
    {
        while (sem_wait(&thread->sem) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        if (!thread->is_active)
            break;

        for(int i = 0; i < FMSGBUS_MAX_HANDLERS; ++i)
        {
            fmsgbus_handler_t *handler = &msgbus->handlers[i];

            if (handler->msg_type == thread->msg_type
                && handler->handler)
                handler->handler(handler->param, thread->msg);
        }

        free(thread->msg);
        thread->msg_type = 0;
        thread->msg = 0;
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
    pmsgbus->messages_mutex = mutex_initializer;

    if (sem_init(&pmsgbus->messages_sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        fmsgbus_release(pmsgbus);
        return FFAIL;
    }

    ferr_t ret = fring_queue_create(pmsgbus->buf, sizeof pmsgbus->buf, &pmsgbus->messages);
    if (ret != FSUCCESS)
    {
        fmsgbus_release(pmsgbus);
        return ret;
    }

    for(uint32_t i = 0; i < FMSGBUS_MAX_THREADS; ++i)
    {
        if (sem_init(&pmsgbus->threads[i].sem, 0, 0) == -1)
        {
            FS_ERR("The semaphore initialization is failed");
            fmsgbus_release(pmsgbus);
            return FFAIL;
        }

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

    int rc = pthread_create(&pmsgbus->ctrl_thread.thread, 0, fmsgbus_ctrl_thread, pmsgbus);
    if (rc)
    {
        FS_ERR("Unable to create the thread. Error: %d", rc);
        fmsgbus_release(pmsgbus);
        return FFAIL;
    }

    static struct timespec const ts = { 1, 0 };
    while(!pmsgbus->ctrl_thread.is_active)
        nanosleep(&ts, NULL);

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
                pmsgbus->ctrl_thread.is_active = false;
                sem_post(&pmsgbus->messages_sem);
                pthread_join(pmsgbus->ctrl_thread.thread, 0);
                sem_destroy(&pmsgbus->messages_sem);

                for(uint32_t i = 0; i < FMSGBUS_MAX_THREADS; ++i)
                {
                    pmsgbus->threads[i].is_active = false;
                    sem_post(&pmsgbus->threads[i].sem);
                    pthread_join(pmsgbus->threads[i].thread, 0);
                    sem_destroy(&pmsgbus->threads[i].sem);
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

    ferr_t ret;
    sem_t  sem;
    ferr_t result = FSUCCESS;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    fmsgbus_msg_subscribe_t const msg =
    {
        FMSGBUS_SUBSCRIBE,
        msg_type,
        handler,
        param,
        &sem,
        &result
    };

    fmsgbus_push_lock(pmsgbus->messages_mutex);
    ret = fring_queue_push_back(pmsgbus->messages, &msg, sizeof msg);
    fmsgbus_pop_lock();

    if (ret == FSUCCESS)
    {
        sem_post(&pmsgbus->messages_sem);

        // Waiting for completion
        struct timespec tm = { time(0) + 1, 0 };
        while(pmsgbus->ctrl_thread.is_active && sem_timedwait(&sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ret = result;
    }

    sem_destroy(&sem);

    return ret;
}

ferr_t fmsgbus_unsubscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler)
{
    if (!pmsgbus
        || !handler)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    ferr_t ret;
    sem_t  sem;
    ferr_t result = FSUCCESS;

    if (sem_init(&sem, 0, 0) == -1)
    {
        FS_ERR("The semaphore initialization is failed");
        return FFAIL;
    }

    fmsgbus_msg_unsubscribe_t const msg =
    {
        FMSGBUS_UNSUBSCRIBE,
        msg_type,
        handler,
        &sem,
        &result
    };

    fmsgbus_push_lock(pmsgbus->messages_mutex);
    ret = fring_queue_push_back(pmsgbus->messages, &msg, sizeof msg);
    fmsgbus_pop_lock();

    if (ret == FSUCCESS)
    {
        sem_post(&pmsgbus->messages_sem);

        // Waiting for completion
        struct timespec tm = { time(0) + 1, 0 };
        while(pmsgbus->ctrl_thread.is_active && sem_timedwait(&sem, &tm) == -1 && errno == EINTR)
            continue;       // Restart if interrupted by handler

        ret = result;
    }

    sem_destroy(&sem);

    return ret;
}

ferr_t fmsgbus_publish(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_t const *msg)
{
    if (!pmsgbus || !msg)
    {
        FS_ERR("Invalid argument");
        return FERR_INVALID_ARG;
    }

    ferr_t ret;

    fmsgbus_msg_t cmsg =
    {
        FMSGBUS_MSG,
        msg_type,
        malloc(msg->size)
    };

    if (!cmsg.msg)
    {
        FS_ERR("No free space of memory");
        return FERR_NO_MEM;
    }

    memcpy(cmsg.msg, msg, msg->size);

    fmsgbus_push_lock(pmsgbus->messages_mutex);
    ret = fring_queue_push_back(pmsgbus->messages, &cmsg, sizeof cmsg);
    fmsgbus_pop_lock();

    if (ret == FSUCCESS)
        sem_post(&pmsgbus->messages_sem);
    else
        free(cmsg.msg);

    return ret;
}
