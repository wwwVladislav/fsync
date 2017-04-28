#include "synchronizer.h"
#include <futils/log.h>
#include <futils/vector.h>
#include <fdb/sync/nodes.h>
#include <fdb/sync/statuses.h>
#include <fdb/sync/files.h>
#include <fcommon/limits.h>
#include <fcommon/messages.h>
#include <string.h>
#include <stdlib.h>

enum
{
    FDB_MAX_REQUESTED_PARTS = 128,      // Max number of requested parts
    FDB_MAX_SYNC_FILES      = 64        // Max number of synchronized files
};

typedef struct
{
    uint32_t    file_id;
    fuuid_t     uuid;
    uint32_t    uuid_file_id;
} fsynchronizer_file_src_info_t;

static int fsynchronizer_file_src_info_cmp(const void *lhs, const void *rhs)
{
    return ((fsynchronizer_file_src_info_t const *)lhs)->file_id - ((fsynchronizer_file_src_info_t const *)rhs)->file_id;
}

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
    fvector_t              *file_src;                           // file source
    fvector_t              *sync_files;                         // synchronized files (FDB_MAX_SYNC_FILES)
};

static void fsynchronizer_file_part_request_handler(fsynchronizer_t *psynchronizer, uint32_t msg_type, fmsg_file_part_request_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
    if (memcmp(&msg->uuid, &psynchronizer->uuid, sizeof psynchronizer->uuid) != 0)
    {
        FS_INFO("UUID %llx%llx requests file content. Id=%u, part=%u", msg->uuid.data.u64[0], msg->uuid.data.u64[1], msg->id, msg->block_number);
#if 0
        char path[FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        // TODO: fsync_file_part_request_handler

        if (fdb_file_path(&psync->uuid, msg->id, path + len, sizeof path - len))
        {
            int fd = open(path, O_BINARY | O_RDONLY);
            if (fd != -1)
            {
                fmsg_file_part_t part;
                part.uuid         = psync->uuid;
                part.destination  = msg->uuid;
                part.id           = msg->id;
                part.block_number = msg->block_number;

                if (lseek(fd, part.block_number * sizeof part.data, SEEK_SET) >= 0)
                {
                    ssize_t size = read(fd, part.data, sizeof part.data);
                    if (size > 0)
                    {
                        part.size = size;
                        if (fmsgbus_publish(psync->msgbus, FFILE_PART, &part, sizeof part) != FSUCCESS)
                            FS_ERR("File part message not published");
                    }
                    else FS_ERR("File reading failed");
                }
                else FS_ERR("lseek failed");

                close(fd);
            }
            else FS_ERR("Unable to open the file: \'%s\'", path);
        }
#endif
    }
}

static void fsynchronizer_file_part_handler(fsynchronizer_t *psynchronizer, uint32_t msg_type, fmsg_file_part_t const *msg, uint32_t size)
{
    (void)size;
    (void)msg_type;
    if (memcmp(&msg->uuid, &psynchronizer->uuid, sizeof psynchronizer->uuid) != 0)
    {
        FS_INFO("File part received from UUID %llx%llx. id=%u, block=%u, size=%u", msg->uuid.data.u64[0], msg->uuid.data.u64[1], msg->id, msg->block_number, msg->size);
#if 0
        char path[FMAX_PATH];
        size_t len = strlen(psync->dir);
        strncpy(path, psync->dir, sizeof path);
        path[len++] = '/';

        // TODO: fsync_file_part_handler

        if (fdb_file_path(&msg->uuid, msg->id, path + len, sizeof path - len))
        {
            int fd = open(path, O_CREAT | O_BINARY | O_WRONLY, 0777);
            if (fd != -1)
            {
                if (lseek(fd, msg->block_number * sizeof msg->data, SEEK_SET) >= 0)
                {
                    if (write(fd, msg->data, msg->size) < 0)
                        FS_ERR("Unable to write data into the file: \'%s\'", path);
                    else
                    {
                        ffile_info_t info;
                        // TODO: if(fdb_file_get(&psync->uuid, path + len, &info))
                        // TODO:   fdb_sync_part_received(&psync->uuid, info.id, msg->block_number);
                    }
                }
                else FS_ERR("lseek failed");

                close(fd);
            }
            else FS_ERR("Unable to open the file: \'%s\'. Error: %d", path, errno);
        }
#endif
    }
}

static void fsynchronizer_msgbus_retain(fsynchronizer_t *psynchronizer, fmsgbus_t *pmsgbus)
{
    psynchronizer->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psynchronizer->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)fsynchronizer_file_part_request_handler, psynchronizer);
    fmsgbus_subscribe(psynchronizer->msgbus, FFILE_PART,            (fmsg_handler_t)fsynchronizer_file_part_handler,         psynchronizer);
}

static void fsynchronizer_msgbus_release(fsynchronizer_t *psynchronizer)
{
    fmsgbus_unsubscribe(psynchronizer->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)fsynchronizer_file_part_request_handler);
    fmsgbus_unsubscribe(psynchronizer->msgbus, FFILE_PART,          (fmsg_handler_t)fsynchronizer_file_part_handler);
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
                        ret = true;

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
                                                    fsynchronizer_file_src_info_t const file_src = { id, uuid, info.id };
                                                    if (!fvector_push_back(&psynchronizer->file_src, &file_src))
                                                        FS_ERR("File info wasn't remembered");
                                                    else ret = false;
                                                }
                                            } else ret = false;
                                            fdb_files_release(files_map4uuid);
                                        } else ret = false;
                                    }
                                    fdb_nodes_iterator_free(nodes_iterator);
                                } else ret = false;
                            } else ret = false;
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

    fvector_qsort(psynchronizer->file_src, fsynchronizer_file_src_info_cmp);

    return ret;
}

static bool fsynchronizer_update_sync_files_list(fsynchronizer_t *psynchronizer)
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
                        ret = true;

                        fdb_data_t file_id = { 0 };
                        for(bool st = fdb_statuses_map_iterator_first(statuses_map_iterator, &file_id); st; st = fdb_statuses_map_iterator_next(statuses_map_iterator, &file_id))
                        {
                            ffile_info_t file_info = { 0 };
                            uint32_t const id = *(uint32_t*)file_id.data;

                            if (fdb_file_get(files_map, &transaction, id, &file_info))
                            {
                                fsynchronizer_file_t const sync_file = { id, file_info.size, 0 };
                                if (!fvector_push_back(&psynchronizer->sync_files, &sync_file))
                                    FS_ERR("Sync file info wasn't remembered");
                                else ret = false;
                            } else ret = false;
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

    fvector_qsort(psynchronizer->file_src, fsynchronizer_file_src_info_cmp);

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

    psynchronizer->file_src = fvector(sizeof(fsynchronizer_file_src_info_t), 0, 256);
    if (!psynchronizer->file_src)
    {
        fsynchronizer_free(psynchronizer);
        return 0;
    }

    psynchronizer->sync_files = fvector(sizeof(fsynchronizer_file_t), 0, FDB_MAX_SYNC_FILES);
    if (!psynchronizer->sync_files)
    {
        fsynchronizer_free(psynchronizer);
        return 0;
    }

    if (!fsynchronizer_update_peers_list(psynchronizer) ||
        !fsynchronizer_update_sync_files_list(psynchronizer))
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
        fvector_release(psynchronizer->sync_files);
        fvector_release(psynchronizer->file_src);
        fsynchronizer_msgbus_release(psynchronizer);
        fdb_release(psynchronizer->db);
        free(psynchronizer);
    }
}

bool fsynchronizer_update(fsynchronizer_t *psynchronizer)
{
    return false;
}
