#ifndef DBTOOL_H_FDBTOOL
#define DBTOOL_H_FDBTOOL
#include <stdbool.h>
#include <futils/uuid.h>
#include <fcommon/limits.h>

typedef struct fdbtool fdbtool_t;

fdbtool_t *fdbtool(char const *dir);
void       fdbtool_close(fdbtool_t *dbtool);
void       fdbtool_tables(fdbtool_t *dbtool);

#endif
