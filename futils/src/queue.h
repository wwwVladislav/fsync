#ifndef QUEUE_H_FUTILS
#define QUEUE_H_FUTILS
#include <stdint.h>
#include "errno.h"

typedef struct fring_queue fring_queue_t;

ferr_t fring_queue_create(void *buf, uint32_t size, fring_queue_t **ppqueue); // Thread 1
void   fring_queue_free(fring_queue_t *pqueue);                               // Thread 1
ferr_t fring_queue_push_back(fring_queue_t *pqueue, void const *, uint32_t);  // Thread 1
ferr_t fring_queue_front(fring_queue_t *pqueue, void **, uint32_t *);         // Thread 2
ferr_t fring_queue_pop_front(fring_queue_t *pqueue);                          // Thread 2

#endif
