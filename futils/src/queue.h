#ifndef QUEUE_H_FUTILS
#define QUEUE_H_FUTILS
#include <stdbool.h>

typedef struct fring_queue fring_queue_t;
typedef void (*fring_queue_notify_t)();

fring_queue_t *fring_queue_create(char *buf, unsigned size, fring_queue_notify_t fn);   // Thread 1
void           fring_queue_free(fring_queue_t *);                                       // Thread 1
bool           fring_queue_push_back(fring_queue_t *, char const *, unsigned);          // Thread 1
bool           fring_queue_front(fring_queue_t *, char **, unsigned *);                 // Thread 2
bool           fring_queue_pop_front(fring_queue_t *);                                  // Thread 2

#endif
