#ifndef VECTOR_H_FUTILS
#define VECTOR_H_FUTILS
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct fvector fvector_t;

fvector_t *fvector(uint32_t item_size, size_t size, size_t capacity);
fvector_t * fvector_retain(fvector_t *);
void fvector_release(fvector_t *);
uint8_t *fvector_ptr(fvector_t *);
bool fvector_push_back(fvector_t **, void const*);
bool fvector_pop_back(fvector_t **, void *);

#endif
