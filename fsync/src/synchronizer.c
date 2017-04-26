#include "synchronizer.h"
#include <futils/log.h>
#include <futils/vector.h>
#include <fdb/sync/nodes.h>
#include <fdb/sync/statuses.h>
#include <fdb/sync/files.h>
#include <string.h>
#include <stdlib.h>

enum
{
    FDB_FILES_LIST_SIZE     = 10000,    // Max number of files for sync for all nodes
    FDB_MAX_REQUESTED_PARTS = 128,      // Max number of requested parts
    FDB_MAX_SYNC_FILES      = 64        // Max number of synchronized files
};

typedef struct
{
    uint32_t    file_id;
    fuuid_t     uuid;
    uint32_t    uuid_file_id;
} fsynchronizer_file_dst_info_t;

typedef struct
{
    uint32_t    file_id;
    uint64_t    size;
    uint64_t    received_size;
} fsynchronizer_file_t;

struct fsynchronizer
{
    fuuid_t                 uuid;                               // uuid
    fmsgbus_t              *msgbus;                             // messages bus
    fdb_t                  *db;                                 // db
    fvector_t              *fsynchronizer_file_dst;             // file destinations
    // fsynchronizer_file_t    sync_files[FDB_MAX_SYNC_FILES];     // synchronized files
    // uint32_t                sync_files_size;                    // number of synchronized files
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

static bool fsynchronizer_update_peers_list(fsynchronizer_t *psynchronizer)
{
    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(psynchronizer->db, &transaction))
    {
        fdb_map_t status_map = { 0 };
        if (fdb_files_statuses(&transaction, &psynchronizer->uuid, &status_map))
        {
            fdb_files_map_t *files_map = fdb_files(&transaction, &psynchronizer->uuid);
            if (files_map)
            {
                fdb_nodes_t *nodes = fdb_nodes(&transaction);
                if (nodes)
                {
                    fdb_statuses_map_iterator_t *statuses_map_iterator = fdb_statuses_map_iterator(&status_map, &transaction, FFILE_IS_EXIST);
                    if (statuses_map_iterator)
                    {
                        fdb_data_t file_id = { 0 };
                        for(bool st = fdb_statuses_map_iterator_first(statuses_map_iterator, &file_id); st; st = fdb_statuses_map_iterator_next(statuses_map_iterator, &file_id))
                        {
                            ffile_info_t file_info = { 0 };
                            uint32_t const id = *(uint32_t*)file_id.data;

                            if (fdb_file_get(files_map, &transaction, id, &file_info))
                            {
                                fdb_nodes_iterator_t *nodes_iterator = fdb_nodes_iterator(nodes, &transaction);
                                if (nodes_iterator)
                                {
                                    fuuid_t uuid;
                                    fdb_node_info_t node_info;

                                    for (bool nst = fdb_nodes_first(nodes_iterator, &uuid, &node_info); nst; nst = fdb_nodes_next(nodes_iterator, &uuid, &node_info))
                                    {
                                        fdb_files_map_t *files_map4uuid = fdb_files(&transaction, &uuid);
                                        if (files_map4uuid)
                                        {
                                            ffile_info_t info = { 0 };
                                            if (fdb_file_get_by_path(files_map4uuid, &transaction, file_info.path, strlen(file_info.path), &info))
                                            {
                                                if (info.status & FFILE_IS_EXIST)
                                                {
                                                    // id -> uuid, info.id
                                                    printf("ololo\n");
                                                    // TODO:
                                                    ret = true;
                                                }
                                            }
                                            fdb_files_release(files_map4uuid);
                                        }
                                    }
                                    fdb_nodes_iterator_free(nodes_iterator);
                                }
                            }
                        }
                        fdb_statuses_map_iterator_free(statuses_map_iterator);
                    }
                    fdb_nodes_release(nodes);
                }
                fdb_files_release(files_map);
            }
            fdb_map_close(&status_map);
        }
        fdb_transaction_abort(&transaction);
    }

    return ret;
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

    if (!fsynchronizer_update_peers_list(psynchronizer))
    {
        fsynchronizer_free(psynchronizer);
        return 0;
    }

    return psynchronizer;
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
