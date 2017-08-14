#ifndef MSGBUS_H_FUTILS
#define MSGBUS_H_FUTILS
#include <stdint.h>
#include "uuid.h"
#include "errno.h"

typedef struct fmsgbus fmsgbus_t;

typedef struct fmsg
{
    uint32_t size;  // message size
    fuuid_t  src;   // source address
    fuuid_t  dst;   // destination address
} fmsg_t;

#define FMSG_TYPE(name) fmsg_##name##_t

#define FMSG_DEF(name, ...)                         \
    typedef struct fmsg_##name                      \
    {                                               \
        fmsg_t hdr;                                 \
        __VA_ARGS__                                 \
    } FMSG_TYPE(name);

#define FMSG(name, msg, src, dst, ...)              \
    fmsg_##name##_t msg =                           \
    {                                               \
        { sizeof(fmsg_##name##_t), src, dst },      \
        __VA_ARGS__                                 \
    }

typedef void(*fmsg_handler_t)(void *, fmsg_t const *);

ferr_t     fmsgbus_create     (fmsgbus_t **ppmsgbus);
fmsgbus_t *fmsgbus_retain     (fmsgbus_t *pmsgbus);
void       fmsgbus_release    (fmsgbus_t *pmsgbus);
ferr_t     fmsgbus_subscribe  (fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler, void *param);
ferr_t     fmsgbus_unsubscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler);
ferr_t     fmsgbus_publish    (fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_t const *msg);

#endif
