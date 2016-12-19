/*
 * The Ole32.lib is needed
 */

#include "../../uuid.h"
#include "../../log.h"
#include "../../static_assert.h"
#include <objbase.h>
#include <string.h>

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
