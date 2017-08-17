#ifndef MUTEX_H_FUTILS
#define MUTEX_H_FUTILS
#include <pthread.h>
#include "log.h"

#define fpush_lock(mutex)                           \
    if (pthread_mutex_lock(&mutex))	                \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fpop_lock() pthread_cleanup_pop(1);

#endif
