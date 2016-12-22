#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include <futils/uuid.h>

typedef enum
{
    FNODE_STATUS = 0
} fmessage_t;

enum
{
    FSTATUS_READY4SYNC  = 1 << 0    // Node is ready for files synchronization
};

typedef struct
{
    fuuid_t  uuid;
    uint32_t status;
} fmsg_node_status_t;

#endif
