/*
    Prefix tree implementation.
*/

#ifndef PREFIX_TREE_H_FUTILS
#define PREFIX_TREE_H_FUTILS
#include "static_allocator.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fptree fptree_t;
typedef struct fptree_iterator fptree_iterator_t;

enum fptree_params
{
    FPTREE_MAX_KEY_LEN            = 1024,                                       // maximum allowed key length
    FPTREE_LEAF_HEADER_SIZE       = sizeof(void*) * 4 + sizeof(uint32_t),       // sizeof(fptree_leaf_t) - FPTREE_DEF_KEY_LEN
    FPTREE_HEADER_SIZE            = sizeof(uint32_t) * 2 + sizeof(void*) * 2    // sizeof(fptree_t)
};

typedef struct fptree_node
{
    void    *data;
    uint32_t key_len;
    uint8_t  key[FPTREE_MAX_KEY_LEN];
} fptree_node_t;

#define FPTREE_LEAF_SIZE(key_len) (FPTREE_LEAF_HEADER_SIZE + key_len)
#define FPTREE_MEM_NEED(items, key_len) (FPTREE_HEADER_SIZE + FSTATIC_ALLOCATOR_MEM_NEED(items * 2, FPTREE_LEAF_SIZE(key_len)))

ferr_t fptree_create(void *buf, uint32_t size, uint32_t key_len, fptree_t **);
void   fptree_delete(fptree_t *ptr);
ferr_t fptree_clear(fptree_t *ptr);
ferr_t fptree_node_insert(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void *data, bool *is_unique);
ferr_t fptree_node_unique_insert(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void *data);
ferr_t fptree_node_delete(fptree_t *ptr, uint8_t const *key, uint32_t key_len);
ferr_t fptree_node_find(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void **pdata);
bool   fptree_empty(fptree_t *ptr);

ferr_t fptree_iterator_create(fptree_t *ptr, fptree_iterator_t **);
void   fptree_iterator_delete(fptree_iterator_t *);
ferr_t fptree_first(fptree_iterator_t *, fptree_node_t *);
ferr_t fptree_next(fptree_iterator_t *, fptree_node_t *);

#ifdef __cplusplus
}
#endif

#endif
