/*
    Allocator implementation.
*/

#ifndef STATIC_ALLOCATOR_H_FUTILS
#define STATIC_ALLOCATOR_H_FUTILS
#include "errno.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fstatic_allocator fstatic_allocator_t;

enum fstatic_allocator_params
{
    FSTATIC_ALLOCATOR_HEADER_SIZE = 16
};

#define FSTATIC_ALLOCATOR_MEM_NEED(items, item_size) (FSTATIC_ALLOCATOR_HEADER_SIZE + items * sizeof(uint32_t) + items * item_size)

ferr_t   fstatic_allocator_create(void *buf, uint32_t size, uint32_t item_size, fstatic_allocator_t **);
void     fstatic_allocator_delete(fstatic_allocator_t *);
ferr_t   fstatic_allocator_clear(fstatic_allocator_t *);
void *   fstatic_alloc(fstatic_allocator_t *);
void     fstatic_free(fstatic_allocator_t *, void *);
uint32_t fstatic_allocator_available(fstatic_allocator_t const *);
uint32_t fstatic_allocator_allocated(fstatic_allocator_t const *);

#ifdef __cplusplus
}
#endif

#endif
