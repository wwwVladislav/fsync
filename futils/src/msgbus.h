#ifndef MSGBUS_H_FUTILS
#define MSGBUS_H_FUTILS
#include <stdint.h>
#include "errno.h"

typedef struct fmsgbus fmsgbus_t;

typedef void(*fmsg_handler_t)(void *, uint32_t, void const *, uint32_t);

ferr_t     fmsgbus_create     (fmsgbus_t **ppmsgbus);
fmsgbus_t *fmsgbus_retain     (fmsgbus_t *pmsgbus);
void       fmsgbus_release    (fmsgbus_t *pmsgbus);
ferr_t     fmsgbus_subscribe  (fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler, void *param);
ferr_t     fmsgbus_unsubscribe(fmsgbus_t *pmsgbus, uint32_t msg_type, fmsg_handler_t handler);
ferr_t     fmsgbus_publish    (fmsgbus_t *pmsgbus, uint32_t msg_type, void const *msg, uint32_t size);

#endif
