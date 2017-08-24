#include "vector.h"
#include <string.h>
#include <stdlib.h>

static const uint32_t FVECTOR_MIN_CAPACITY = 32u;

struct fvector
{
    volatile uint32_t   ref_counter;
    uint32_t            item_size;
    size_t              size;
    size_t              capacity;
};

fvector_t *fvector(uint32_t item_size, size_t size, size_t capacity)
{
    if (capacity < size)
        capacity = size;
    if (capacity < FVECTOR_MIN_CAPACITY)
        capacity = FVECTOR_MIN_CAPACITY;

    size_t const mem_size = sizeof(fvector_t) + item_size * capacity;
    fvector_t *pvector = (fvector_t *)malloc(mem_size);
    if (!pvector)
        return 0;

    memset(pvector, 0, mem_size);
    pvector->ref_counter = 1;
    pvector->item_size = item_size;
    pvector->size = size;
    pvector->capacity = capacity;

    return pvector;
}

fvector_t * fvector_retain(fvector_t *pvector)
{
    if (pvector)
        pvector->ref_counter++;
    return pvector;
}

void fvector_release(fvector_t *pvector)
{
    if (pvector)
        if (pvector->ref_counter)
            if (!--pvector->ref_counter)
                free(pvector);
}

void *fvector_ptr(fvector_t *pvector)
{
    return pvector ? (void*)(pvector + 1) : 0;
}

void *fvector_at(fvector_t *pvector, size_t idx)
{
    if (!pvector
        || idx >= pvector->size)
        return 0;
    uint8_t *begin = (uint8_t*)(pvector + 1);
    return begin + pvector->item_size * idx;
}

size_t fvector_idx(fvector_t const *pvector, void const *item)
{
    uint8_t const *begin = (uint8_t*)(pvector + 1);
    return ((uint8_t const *)item - begin) / pvector->item_size;
}

size_t fvector_size(fvector_t const *pvector)
{
    return pvector->size;
}

size_t fvector_capacity(fvector_t const *pvector)
{
    return pvector->capacity;
}

bool fvector_push_back(fvector_t **pvector, void const *data)
{
    if (!pvector || !*pvector || !data)
        return false;

    if ((*pvector)->size >= (*pvector)->capacity)
    {
        size_t const new_capacity = !(*pvector)->capacity ? FVECTOR_MIN_CAPACITY :
                                    (*pvector)->capacity * (*pvector)->item_size < 10 * 1024 * 1024 ? (*pvector)->capacity * 2 :
                                    (*pvector)->capacity + 256;
        size_t const mem_size = sizeof(fvector_t) + (*pvector)->item_size * new_capacity;
        fvector_t *new_pvector = (fvector_t *)realloc((*pvector), mem_size);
        if (!new_pvector)
            return false;
        (*pvector) = new_pvector;
        (*pvector)->capacity = new_capacity;
    }

    uint8_t *ptr = fvector_ptr(*pvector);
    memcpy(ptr + (*pvector)->item_size * (*pvector)->size, data, (*pvector)->item_size);
    (*pvector)->size++;
    return true;
}

bool fvector_pop_back(fvector_t **pvector)
{
    if (!pvector || !*pvector)
        return false;
    if (!(*pvector)->size)
        return false;

    if ((*pvector)->size * 2 <= (*pvector)->capacity)
    {
        size_t new_capacity = !(*pvector)->size ? FVECTOR_MIN_CAPACITY : ((*pvector)->capacity / 2);
        if (new_capacity < FVECTOR_MIN_CAPACITY)
            new_capacity = FVECTOR_MIN_CAPACITY;
        if (new_capacity < (*pvector)->capacity)
        {
            size_t const mem_size = sizeof(fvector_t) + (*pvector)->item_size * new_capacity;
            fvector_t *new_pvector = (fvector_t *)malloc(mem_size);
            if (!new_pvector)
                return false;
            size_t const data_size = sizeof(fvector_t) + (*pvector)->item_size * (*pvector)->size - (*pvector)->item_size;
            memcpy(new_pvector, *pvector, data_size);
            free(*pvector);
            (*pvector) = new_pvector;
            (*pvector)->capacity = new_capacity;
        }
    }
    (*pvector)->size--;
    return true;
}

bool fvector_erase(fvector_t **pvector, size_t idx)
{
    if (!pvector || !*pvector)
        return false;
    if (idx >= (*pvector)->size)
        return false;

    if (idx + 1 < (*pvector)->size)
    {
        uint8_t *data = fvector_ptr(*pvector);
        memmove(data + idx * (*pvector)->item_size, data + (idx + 1) * (*pvector)->item_size, ((*pvector)->size - idx - 1) * (*pvector)->item_size);
    }

    return fvector_pop_back(pvector);
}

bool fvector_clear(fvector_t **pvector)
{
    if (!pvector || !*pvector)
        return false;
    fvector_t *new_vector = fvector((*pvector)->item_size, 0, 0);
    if (!new_vector)
        return false;
    free(*pvector);
    *pvector = new_vector;
    return true;
}

void fvector_qsort(fvector_t *pvector, fvector_comparer_t pred)
{
    if (pvector)
        qsort(fvector_ptr(pvector), pvector->size, pvector->item_size, pred);
}

void *fvector_bsearch(fvector_t *pvector, void const* key, fvector_comparer_t pred)
{
    if (!pvector)
        return 0;
    return bsearch(key, fvector_ptr(pvector), pvector->size, pvector->item_size, pred);
}
