#ifndef FCOMPOSER_H_FSYNC
#define FCOMPOSER_H_FSYNC
#include <config.h>
#include <futils/uuid.h>
#include <futils/msgbus.h>

typedef struct
{
    bool       is_active;
    fmsgbus_t *msgbus;
    uint32_t   nodes_num;
    fuuid_t    nodes[FMAX_CONNECTIONS_NUM];
    uint32_t   file_id;
    uint64_t   file_size;
    char       path[FMAX_PATH];
} fcomposer_t;

#endif
