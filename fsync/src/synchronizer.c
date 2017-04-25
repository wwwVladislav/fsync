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

static bool fsynchronizer_prepare(fsynchronizer_t *psynchronizer)
{
    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(psync->db, &transaction))
    {
        fdb_map_t status_map = { 0 };
        if (fdb_files_statuses(&transaction, &psync->uuid, &status_map))
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
                        //fdb_nodes_iterator_t *nodes_it = fdb_nodes_iterator(nodes, &transaction);
                    }
                    fdb_statuses_map_iterator_free(statuses_map_iterator);
                }
                fdb_nodes_release(nodes);
            }
/*
            fdb_data_t file_id = { 0 };
            if (fdb_statuses_map_get(&status_map, &transaction, FFILE_IS_EXIST, &file_id))
            {
                printf("Search nodes\n");
                // I. Find all nodes where the file is exist
                fdb_nodes_t *nodes = fdb_nodes(&transaction);
                if (nodes)
                {
                    fdb_nodes_iterator_t *nodes_it = fdb_nodes_iterator(nodes, &transaction);
                    fuuid_t uuid;
                    fdb_node_info_t node_info;
                    for (bool st = fdb_nodes_first(nodes_it, &uuid, &node_info); st; st = fdb_nodes_next(nodes_it, &uuid, &node_info))
                    {
                        printf("ololo\n");
                        // TODO:
                    }
                    fdb_nodes_iterator_free(nodes_it);
                    fdb_nodes_release(nodes);
                }

                // TODO
            }
*/
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

    if (!fsynchronizer_prepare(psynchronizer))
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
