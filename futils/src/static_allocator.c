#include "static_allocator.h"
#include "static_assert.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static const uint32_t FSTATIC_ALLOCATOR_SIGNATURE = 0x636C6173u;  // This signature is used to check the allocator initialisation

struct fstatic_allocator
{
    uint32_t signature;
    uint32_t capacity;
    uint32_t size;
    uint32_t item_size;
    uint32_t free_blocks[1];
};

static int fstatic_allocator_is_valid(fstatic_allocator_t const *ptr)
{
    return ptr && ptr->signature == FSTATIC_ALLOCATOR_SIGNATURE ? 1 : 0;
}

ferr_t fstatic_allocator_create(void *buf, uint32_t size, uint32_t item_size, fstatic_allocator_t **ppallocator)
{
    fstatic_allocator_t *ptr = (fstatic_allocator_t*)buf;
    uint32_t i = 0;

    if (!buf
        || size < sizeof(fstatic_allocator_t)
        || !item_size
        || !ppallocator)
        return FERR_INVALID_ARG;

    memset(buf, 0, size);
    ptr->signature = FSTATIC_ALLOCATOR_SIGNATURE;
    ptr->capacity = (size - FSTATIC_ALLOCATOR_HEADER_SIZE) / (sizeof(uint32_t) + item_size);
    ptr->size = 0;
    ptr->item_size = item_size;

    for(i = 0; i < ptr->capacity; ++i)
        ptr->free_blocks[i] = i;

    *ppallocator = ptr;
    return FSUCCESS;
}

void fstatic_allocator_delete(fstatic_allocator_t *pallocator)
{
    if (pallocator)
        memset(pallocator, 0, sizeof(fstatic_allocator_t));
}

ferr_t fstatic_allocator_clear(fstatic_allocator_t *pallocator)
{
    uint32_t i = 0;
    if (!fstatic_allocator_is_valid(pallocator))
        return FERR_INVALID_ARG;
    pallocator->size = 0;
    for(i = 0; i < pallocator->capacity; ++i)
        pallocator->free_blocks[i] = i;
    return FSUCCESS;
}

void *fstatic_alloc(fstatic_allocator_t *pallocator)
{
    void *ret;
    uint8_t *mem_area;
    if (!fstatic_allocator_is_valid(pallocator))
        return 0;                                       // invalid allocator
    if (pallocator->size >= pallocator->capacity)
        return 0;                                       // no enough memory
    mem_area = (uint8_t*)(pallocator->free_blocks + pallocator->capacity);
    ret = mem_area + pallocator->free_blocks[pallocator->size] * pallocator->item_size;
    pallocator->size++;
    return ret;
}

void fstatic_free(fstatic_allocator_t *pallocator, void *ptr)
{
    uint8_t const *mem_area;
    uint32_t item4free;
    if (!fstatic_allocator_is_valid(pallocator)         // invalid allocator
        || !ptr                                         // invalid pointer
        || !pallocator->size)                           // no allocated blocks
        return;
    mem_area = (uint8_t const*)(pallocator->free_blocks + pallocator->capacity);
    item4free = ((uint8_t const *)ptr - mem_area) / pallocator->item_size;
    pallocator->size--;
    pallocator->free_blocks[pallocator->size] = item4free;
}

uint32_t fstatic_allocator_available(fstatic_allocator_t const *ptr)
{
    return fstatic_allocator_is_valid(ptr) ? ptr->capacity - ptr->size : 0;
}

uint32_t fstatic_allocator_allocated(fstatic_allocator_t const *ptr)
{
    return fstatic_allocator_is_valid(ptr) ? ptr->size : 0;
}
