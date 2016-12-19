#ifndef TYPES_H_FUTILS
#define TYPES_H_FUTILS
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    union
    {
        uint8_t  u8[16];
        uint64_t u64[2];
    } data;
} fuuid_t;

bool fuuid_gen(fuuid_t *);

#endif
