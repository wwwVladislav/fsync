#ifndef INTERFACE_H_FILINK
#define INTERFACE_H_FILINK
#include <stdbool.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <fdb/db.h>

typedef struct filink filink_t;

filink_t *filink_bind(fmsgbus_t *pmsgbus, fdb_t *db, char const *addr, fuuid_t const *uuid);
void filink_unbind(filink_t *ilink);
filink_t *filink_retain(filink_t *ilink);
void filink_release(filink_t *ilink);
bool filink_connect(filink_t *ilink, char const *addr);
bool filink_is_connected(filink_t *ilink, fuuid_t const *uuid);

#endif
