/*
 * The Ole32.lib is needed
 */

#include "../../uuid.h"
#include "../../log.h"
#include "../../static_assert.h"
#include <objbase.h>
#include <string.h>
#include <stdio.h>

FSTATIC_ASSERT(sizeof(fuuid_t) == sizeof(GUID));

bool fuuid_gen(fuuid_t *uuid)
{
    GUID guid = { 0 };
    HRESULT hr = CoCreateGuid(&guid);
    if (FAILED(hr))
    {
        FS_ERR("UUID generation is failed");
        return false;
    }
    memcpy(uuid, &guid, sizeof guid);
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
    snprintf(buf, size, "%08llx%08llx", uuid->data.u64[0], uuid->data.u64[1]);
    return buf;
}

