#ifndef INTERFACE_H_FILINK
#define INTERFACE_H_FILINK
#include <stdbool.h>

typedef struct filink filink_t;

filink_t *filink_bind(char const *addr);
void filink_unbind(filink_t *ilink);
bool filink_connect(filink_t *ilink, char const *addr);

#endif
