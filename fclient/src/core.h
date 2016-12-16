#ifndef CORE_H_FCLIENT
#define CORE_H_FCLIENT
#include <stdbool.h>

typedef struct fcore fcore_t;

fcore_t *fcore_start(char const *addr);
void fcore_stop(fcore_t *pcore);
bool fcore_connect(fcore_t *pcore, char const *addr);
bool fcore_sync(fcore_t *pcore, char const *dir);

#endif
