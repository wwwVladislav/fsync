#include "md5.h"
#include "static_assert.h"
#include <openssl/md5.h>

FSTATIC_ASSERT(sizeof(fmd5_context_t) >= sizeof(MD5_CTX));

void fmd5_init(fmd5_context_t *ctx)
{
    MD5_Init((MD5_CTX*)ctx);
}

void fmd5_update(fmd5_context_t *ctx, const void *data, uint32_t len)
{
    MD5_Update((MD5_CTX*)ctx, data, len);
}

void fmd5_final(fmd5_context_t *ctx, fmd5_t *sum)
{
    MD5_Final((unsigned char *)sum, (MD5_CTX*)ctx);
}
