#ifndef CORE_H_FCLIENT
#define CORE_H_FCLIENT
#include <stdbool.h>
#include <futils/uuid.h>
#include <fcommon/limits.h>

typedef struct fcore fcore_t;
typedef struct fcore_nodes_iterator fcore_nodes_iterator_t;

typedef struct
{
    fuuid_t uuid;
    char    address[FMAX_ADDR];
    bool    connected;
} fcore_node_info_t;

fcore_t *fcore_start(char const *addr);
void    fcore_stop(fcore_t *pcore);
bool    fcore_connect(fcore_t *pcore, char const *addr);
bool    fcore_sync(fcore_t *pcore, char const *dir);

bool    fcore_nodes_iterator(fcore_t *pcore, fcore_nodes_iterator_t **it);
void    fcore_nodes_iterator_delete(fcore_nodes_iterator_t *it);
bool    fcore_nodes_next(fcore_nodes_iterator_t *it, fcore_node_info_t *info);

#endif
