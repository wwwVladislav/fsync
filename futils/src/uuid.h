#ifndef TYPES_H_FUTILS
#define TYPES_H_FUTILS
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    union
    {
        uint8_t  u8[16];
        uint64_t u64[2];
    } data;
} fuuid_t;

#define FUUID(...) { { { __VA_ARGS__ } } }

bool fuuid_gen(fuuid_t *);
char const * fuuid2str(fuuid_t const *, char *buf, size_t size);

#endif
