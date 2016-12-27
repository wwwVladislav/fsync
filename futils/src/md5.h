#ifndef MD5_H_FUTILS
#define MD5_H_FUTILS
#include <stdint.h>

typedef struct
{
    uint8_t data[16];
} fmd5_t;

typedef struct fmd5_context
{
    uint8_t ctx[768];
} fmd5_context_t;

void fmd5_init(fmd5_context_t *ctx);
void fmd5_update(fmd5_context_t *ctx, const void *data, uint32_t len);
void fmd5_final(fmd5_context_t *ctx, fmd5_t *sum);

#endif
