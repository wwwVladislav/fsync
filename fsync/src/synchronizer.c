#include "synchronizer.h"
#include <futils/log.h>
#include <string.h>
#include <stdlib.h>

struct fsynchronizer
{
    fuuid_t    uuid;
    fmsgbus_t *msgbus;
    fdb_t     *db;
};

static void fsynchronizer_msgbus_retain(fsynchronizer_t *psynchronizer, fmsgbus_t *pmsgbus)
{
    psynchronizer->msgbus = fmsgbus_retain(pmsgbus);
//    fmsgbus_subscribe(psync->msgbus, FNODE_STATUS,          (fmsg_handler_t)fsync_status_handler,            psync);
//    fmsgbus_subscribe(psync->msgbus, FSYNC_FILES_LIST,      (fmsg_handler_t)fsync_sync_files_list_handler,   psync);
//    fmsgbus_subscribe(psync->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)fsync_file_part_request_handler, psync);
//    fmsgbus_subscribe(psync->msgbus, FFILE_PART,            (fmsg_handler_t)fsync_file_part_handler,         psync);
}

static void fsynchronizer_msgbus_release(fsynchronizer_t *psynchronizer)
{
//    fmsgbus_unsubscribe(psync->msgbus, FNODE_STATUS,        (fmsg_handler_t)fsync_status_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FSYNC_FILES_LIST,    (fmsg_handler_t)fsync_sync_files_list_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)fsync_file_part_request_handler);
//    fmsgbus_unsubscribe(psync->msgbus, FFILE_PART,          (fmsg_handler_t)fsync_file_part_handler);
    fmsgbus_release(psynchronizer->msgbus);
}

fsynchronizer_t *fsynchronizer_create(fmsgbus_t *pmsgbus, fdb_t *db, fuuid_t const *uuid)
{
    if (!pmsgbus || !db || !uuid)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fsynchronizer_t *psynchronizer = malloc(sizeof(fsynchronizer_t));
    if (!psynchronizer)
    {
        FS_ERR("Unable to allocate memory for synchronizer");
        return 0;
    }
    memset(psynchronizer, 0, sizeof *psynchronizer);

    psynchronizer->uuid = *uuid;
    fsynchronizer_msgbus_retain(psynchronizer, pmsgbus);
    psynchronizer->db = fdb_retain(db);

    return 0;
}

void fsynchronizer_free(fsynchronizer_t *psynchronizer)
{
    if (psynchronizer)
    {
        fsynchronizer_msgbus_release(psynchronizer);
        fdb_release(psynchronizer->db);
        free(psynchronizer);
    }
}

bool fsynchronizer_update(fsynchronizer_t *psynchronizer)
{
    return false;
}
