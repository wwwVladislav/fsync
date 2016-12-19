#ifndef INTERFACE_H_FILINK
#define INTERFACE_H_FILINK
#include <stdbool.h>
#include <futils/uuid.h>

typedef struct filink filink_t;

filink_t *filink_bind(char const *addr, fuuid_t const *uuid);
void filink_unbind(filink_t *ilink);
bool filink_connect(filink_t *ilink, char const *addr);

#endif
