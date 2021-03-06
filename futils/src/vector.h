#ifndef VECTOR_H_FUTILS
#define VECTOR_H_FUTILS
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct fvector fvector_t;

typedef int (*fvector_comparer_t)(const void *, const void*);

fvector_t *fvector(uint32_t item_size, size_t size, size_t capacity);
fvector_t *fvector_retain(fvector_t *);
void       fvector_release(fvector_t *);
void      *fvector_ptr(fvector_t *);
void      *fvector_at(fvector_t *, size_t);
size_t     fvector_idx(fvector_t const*, void const*);
size_t     fvector_size(fvector_t const *);
size_t     fvector_capacity(fvector_t const *);
bool       fvector_push_back(fvector_t **, void const*);
bool       fvector_pop_back(fvector_t **);
bool       fvector_erase(fvector_t **, size_t);
bool       fvector_clear(fvector_t **pvector);
void       fvector_qsort(fvector_t *, fvector_comparer_t pred);
void      *fvector_bsearch(fvector_t *, void const* key, fvector_comparer_t pred);

#endif
