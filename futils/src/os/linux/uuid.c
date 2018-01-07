#include "../../uuid.h"
#include "../../log.h"
#include "../../static_assert.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

bool fuuid_gen(fuuid_t *uuid)
{
    struct
    {
        int32_t a;
        int16_t b;
        int16_t c;
        int16_t d;
        int16_t e;
        int16_t f;
        int16_t g;
    }
    const tmp_uuid =
    {
        rand(),
        (int16_t)rand(),
        (rand() & 0x0fff) | 0x4000,
        rand() % 0x3fff + 0x8000,
        (int16_t)rand(),
        (int16_t)rand(),
        (int16_t)rand()
    };

    memcpy(uuid, &tmp_uuid, sizeof(fuuid_t));

    return true;
}

int fuuid_cmp(fuuid_t const *lhs, fuuid_t const *rhs)
{
    if (lhs->data.u64[0] < rhs->data.u64[0]) return -1;
    if (lhs->data.u64[0] > rhs->data.u64[0]) return 1;
    if (lhs->data.u64[1] < rhs->data.u64[1]) return -1;
    if (lhs->data.u64[1] > rhs->data.u64[1]) return 1;
    return 0;
}

char const * fuuid2str(fuuid_t const *uuid, char *buf, size_t size)
{
    static char const hex[] = "0123456789ABCDEF";
    size = size < 2 * sizeof uuid->data.u8 ? size : 2 * sizeof uuid->data.u8;
    for(int i = 0; i < size; ++i)
    {
        uint8_t const d = (uuid->data.u8[i / 2] >> ((1 - i % 2) * 4)) & 0xF;
        buf[i] = hex[d];
    }
    return buf;
}

