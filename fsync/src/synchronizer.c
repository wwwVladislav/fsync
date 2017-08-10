#include "synchronizer.h"
#include "file_assembler.h"
#include <futils/log.h>
#include <futils/vector.h>
#include <fdb/sync/nodes.h>
#include <fdb/sync/statuses.h>
#include <fdb/sync/sync_files.h>
#include <fcommon/limits.h>
#include <fcommon/messages.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define fsync_push_lock(mutex)                      \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define fsync_pop_lock() pthread_cleanup_pop(1);

enum
{
    FDB_MAX_REQUESTED_PARTS = 128,      // Max number of requested parts
};

typedef struct
{
    time_t      requested_time;
    uint32_t    file_id;
    uint32_t    part;
    fuuid_t     uuid;
} fsynchronizer_requested_part_t;

typedef struct
{
    uint32_t    file_id;
    fuuid_t     uuid;
} fsynchronizer_file_src_t;

typedef struct
{
    uint32_t          file_id;                                  // file id
    uint64_t          size;                                     // file size
    fvector_t        *src;                                      // file sources (Type: fsynchronizer_file_src_t)
    fvector_t        *requested_parts;                          // requested parts (Type: fsynchronizer_requested_part_t)
    file_assembler_t *fassembler;                               // files assembler
} fsynchronizer_file_t;

static int fsynchronizer_file_cmp(const void *lhs, const void *rhs)
{
    return ((fsynchronizer_file_t const *)lhs)->file_id - ((fsynchronizer_file_t const *)rhs)->file_id;
}

struct fsynchronizer
{
    char                    dir[FMAX_PATH];
    fuuid_t                 uuid;                               // uuid
    fmsgbus_t              *msgbus;                             // messages bus
    fdb_t                  *db;                                 // db
    pthread_mutex_t         mutex;
    fvector_t              *sync_files;                         // synchronized files (Type: fsynchronizer_file_t)
};

static void fsynchronizer_file_part_handler(fsynchronizer_t *psynchronizer, FMSG_TYPE(file_part) const *msg)
{
    if (memcmp(&msg->hdr.dst, &psynchronizer->uuid, sizeof psynchronizer->uuid) == 0)
    {
        char str[2 * sizeof(fuuid_t) + 1] = { 0 };
        FS_INFO("File part received from UUID %s. id=%u, block=%u, size=%u", fuuid2str(&msg->hdr.src, str, sizeof str), msg->id, msg->block_number, msg->size);

        char path[FMAX_PATH] = { 0 };
        uint32_t id = FINVALID_ID;

        fdb_transaction_t transaction = { 0 };
        if (fdb_transaction_start(psynchronizer->db, &transaction))
        {
            fdb_sync_files_map_t *uuid_files_map = fdb_sync_files(&transaction, &msg->hdr.src);
            if (uuid_files_map)
            {
                fdb_sync_file_path(uuid_files_map, &transaction, msg->id, path, sizeof path);
                fdb_sync_files_release(uuid_files_map);
            }

            if (path[0])
            {
                fdb_sync_files_map_t *files_map = fdb_sync_files(&transaction, &psynchronizer->uuid);
                if (files_map)
                {
                    fdb_sync_file_id(files_map, &transaction, path, strlen(path), &id);
                    fdb_sync_files_release(files_map);
                }
            }

            fdb_transaction_abort(&transaction);
        }

        if (id == FINVALID_ID)
            FS_ERR("Unknown file part was received");
        else
        {
            fsync_push_lock(psynchronizer->mutex);

            fsynchronizer_file_t const file_id = { id };
            fsynchronizer_file_t *sync_file = (fsynchronizer_file_t *)fvector_bsearch(psynchronizer->sync_files, &file_id, fsynchronizer_file_cmp);

            if (sync_file)
            {
                if (!file_assembler_add_block(sync_file->fassembler, msg->block_number, msg->data, msg->size))
                    FS_ERR("Unable to write file part");
            }
            else FS_ERR("Unknown file part was received");

            fsync_pop_lock();
        }
    }
}

static void fsynchronizer_msgbus_retain(fsynchronizer_t *psynchronizer, fmsgbus_t *pmsgbus)
{
    psynchronizer->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(psynchronizer->msgbus, FFILE_PART,            (fmsg_handler_t)fsynchronizer_file_part_handler,         psynchronizer);
}

static void fsynchronizer_msgbus_release(fsynchronizer_t *psynchronizer)
{
    fmsgbus_unsubscribe(psynchronizer->msgbus, FFILE_PART,          (fmsg_handler_t)fsynchronizer_file_part_handler);
    fmsgbus_release(psynchronizer->msgbus);
}

static bool fsynchronizer_update_sync_files_list(fsynchronizer_t *psynchronizer)
{
    bool ret = false;

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(psynchronizer->db, &transaction))
    {
        fdb_map_t status_map = { 0 };
        if (fdb_sync_files_statuses(&transaction, &psynchronizer->uuid, &status_map))
        {
            fdb_sync_files_map_t *files_map = fdb_sync_files(&transaction, &psynchronizer->uuid);
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
                        for(bool st = fdb_statuses_map_iterator_first(statuses_map_iterator, &file_id); ret && st; st = fdb_statuses_map_iterator_next(statuses_map_iterator, &file_id))
                        {
                            fsync_file_info_t file_info = { 0 };
                            uint32_t const id = *(uint32_t*)file_id.data;

                            if (fdb_sync_file_get(files_map, &transaction, id, &file_info))
                            {
                                char path[2 * FMAX_PATH];
                                size_t dir_path_len = strlen(psynchronizer->dir);
                                memcpy(path, psynchronizer->dir, dir_path_len);
                                strncpy(path + dir_path_len, file_info.path, sizeof path - dir_path_len);

                                file_assembler_t *fassembler = file_assembler_open(path, file_info.size);
                                if (!fassembler)
                                {
                                    ret = false;
                                    FS_ERR("File assembler wasn't opened");
                                    break;
                                }

                                fvector_t *file_srcs = fvector(sizeof(fsynchronizer_file_src_t), 0, 0);
                                if (!file_srcs)
                                {
                                    file_assembler_close(fassembler);
                                    ret = false;
                                    FS_ERR("File sources vector wasn't created");
                                    break;
                                }

                                fvector_t *requested_parts = fvector(sizeof(fsynchronizer_requested_part_t), 0, 0);
                                if (!requested_parts)
                                {
                                    file_assembler_close(fassembler);
                                    fvector_release(file_srcs);
                                    ret = false;
                                    FS_ERR("Requested parts vector wasn't created");
                                    break;
                                }

                                fsync_push_lock(psynchronizer->mutex);

                                fsynchronizer_file_t const sync_file = { id, file_info.size, file_srcs, requested_parts, fassembler };
                                if (!fvector_push_back(&psynchronizer->sync_files, &sync_file))
                                {
                                    file_assembler_close(fassembler);
                                    fvector_release(file_srcs);
                                    fvector_release(requested_parts);
                                    FS_ERR("Sync file info wasn't remembered");
                                    ret = false;
                                }

                                fsync_pop_lock();

                                if (!ret)
                                    break;

                                fdb_nodes_iterator_t *nodes_iterator = fdb_nodes_iterator(nodes, &transaction);
                                if (nodes_iterator)
                                {
                                    fuuid_t uuid;
                                    fdb_node_info_t node_info;

                                    for (bool nst = fdb_nodes_first(nodes_iterator, &uuid, &node_info); ret && nst; nst = fdb_nodes_next(nodes_iterator, &uuid, &node_info))
                                    {
                                        fdb_sync_files_map_t *files_map4uuid = fdb_sync_files(&transaction, &uuid);
                                        if (files_map4uuid)
                                        {
                                            fsync_file_info_t info = { 0 };
                                            if (fdb_sync_file_get_by_path(files_map4uuid, &transaction, file_info.path, strlen(file_info.path), &info))
                                            {
                                                if (info.status & FFILE_IS_EXIST)
                                                {
                                                    fsynchronizer_file_src_t const file_src = { info.id, uuid };
                                                    if (!fvector_push_back(&file_srcs, &file_src))
                                                    {
                                                        FS_ERR("File info wasn't remembered");
                                                        ret = false;
                                                    }
                                                }
                                            }
                                            else
                                            {
                                                ret = false;
                                                FS_ERR("Unable to find file information by path");
                                            }
                                            fdb_sync_files_release(files_map4uuid);
                                        }
                                        else
                                        {
                                            ret = false;
                                            FS_ERR("Unable to retrieve files for uuid");
                                        }
                                    }
                                    fdb_nodes_iterator_free(nodes_iterator);
                                }
                                else
                                {
                                    ret = false;
                                    FS_ERR("Unable to create  the nodes iterator");
                                }
                            }
                            else
                            {
                                ret = false;
                                FS_ERR("Unable to get file information by id");
                            }
                        }
                        fdb_statuses_map_iterator_free(statuses_map_iterator);
                    }
                    fdb_nodes_release(nodes);
                }
                fdb_sync_files_release(files_map);
            }
            fdb_map_close(&status_map);
        }
        fdb_transaction_abort(&transaction);
    }

    if (ret)
    {
        fsync_push_lock(psynchronizer->mutex);
        fvector_qsort(psynchronizer->sync_files, fsynchronizer_file_cmp);
        fsync_pop_lock();
    }

    return ret;
}

fsynchronizer_t *fsynchronizer_create(fmsgbus_t *pmsgbus, fdb_t *db, fuuid_t const *uuid, char const *dir)
{
    if (!pmsgbus || !db || !uuid || !dir)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    size_t dir_name_len = strlen(dir);
    if (dir_name_len > FMAX_PATH)
    {
        FS_ERR("Directory path is too long");
        return 0;
    }

    fsynchronizer_t *psynchronizer = malloc(sizeof(fsynchronizer_t));
    if (!psynchronizer)
    {
        FS_ERR("Unable to allocate memory for synchronizer");
        return 0;
    }
    memset(psynchronizer, 0, sizeof *psynchronizer);

    strncpy(psynchronizer->dir, dir, sizeof psynchronizer->dir);
    psynchronizer->dir[dir_name_len++] = '/';

    psynchronizer->uuid = *uuid;
    fsynchronizer_msgbus_retain(psynchronizer, pmsgbus);
    psynchronizer->db = fdb_retain(db);

    psynchronizer->sync_files = fvector(sizeof(fsynchronizer_file_t), 0, 0);
    if (!psynchronizer->sync_files)
    {
        fsynchronizer_free(psynchronizer);
        return 0;
    }

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    psynchronizer->mutex = mutex_initializer;

    if (!fsynchronizer_update_sync_files_list(psynchronizer))
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
        fsync_push_lock(psynchronizer->mutex);

        fsynchronizer_file_t *sync_files = (fsynchronizer_file_t *)fvector_ptr(psynchronizer->sync_files);
        for(size_t i = 0; i < fvector_size(psynchronizer->sync_files); ++i)
        {
            fvector_release(sync_files[i].src);
            fvector_release(sync_files[i].requested_parts);
            file_assembler_close(sync_files[i].fassembler);
        }

        fvector_release(psynchronizer->sync_files);
        psynchronizer->sync_files = 0;

        fsync_pop_lock();

        fsynchronizer_msgbus_release(psynchronizer);
        fdb_release(psynchronizer->db);
        memset(psynchronizer, 0, sizeof *psynchronizer);
        free(psynchronizer);
    }
}

static void fsynchronizer_cleanup_ready_files_info(fsynchronizer_t *psynchronizer)
{
    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(psynchronizer->db, &transaction))
    {
        fdb_map_t status_map = { 0 };
        if (fdb_sync_files_statuses(&transaction, &psynchronizer->uuid, &status_map))
        {
            fsynchronizer_file_t *sync_files = (fsynchronizer_file_t *)fvector_ptr(psynchronizer->sync_files);
            size_t sync_files_size = fvector_size(psynchronizer->sync_files);

            for(size_t i = 0; i < sync_files_size;)
            {
                bool const file_is_ready = file_assembler_is_ready(sync_files[i].fassembler);

                if (file_is_ready)
                {
                    fdb_data_t const file_id = { sizeof sync_files[i].file_id, &sync_files[i].file_id };
                    fdb_statuses_map_del(&status_map, &transaction, FFILE_IS_EXIST, &file_id);

                    if (fvector_erase(&psynchronizer->sync_files, i))
                    {
                        sync_files = (fsynchronizer_file_t *)fvector_ptr(psynchronizer->sync_files);
                        sync_files_size = fvector_size(psynchronizer->sync_files);
                        continue;
                    }
                }
                ++i;
            }

            fdb_transaction_commit(&transaction);
            fdb_map_close(&status_map);
        }
        fdb_transaction_abort(&transaction);
    }
}

bool fsynchronizer_update(fsynchronizer_t *psynchronizer)
{
    if (!psynchronizer)
        return false;

    bool ret = false;
    uint32_t ready_files_num = 0;

    time_t const now = time(0);

    fsync_push_lock(psynchronizer->mutex);

    fsynchronizer_file_t *sync_files = (fsynchronizer_file_t *)fvector_ptr(psynchronizer->sync_files);
    size_t const sync_files_size = fvector_size(psynchronizer->sync_files);

    for(size_t i = 0; i < sync_files_size; ++i)
    {
        // request new part
        uint32_t block_id = 0;
        if (file_assembler_request_block(sync_files[i].fassembler, &block_id))
        {
            fsynchronizer_file_src_t const *file_srcs = (fsynchronizer_file_src_t*)fvector_ptr(sync_files[i].src);
            size_t const file_srcs_size = fvector_size(sync_files[i].src);

            if (file_srcs_size)
            {
                fsynchronizer_requested_part_t const requested_part = { now, file_srcs[0].file_id, block_id, file_srcs[0].uuid };
                if (!fvector_push_back(&sync_files[i].requested_parts, &requested_part))
                {
                    FS_ERR("File part not requested");
                    break;
                }

                FMSG(file_part_request, req, psynchronizer->uuid, file_srcs[0].uuid,
                    file_srcs[0].file_id,
                    block_id
                );

                FS_INFO("Requesting file content. Id=%u, part=%u", req.id, req.block_number);

                if (fmsgbus_publish(psynchronizer->msgbus, FFILE_PART_REQUEST, (fmsg_t const *)&req) != FSUCCESS)
                    FS_ERR("File part not requested");
            }
        }

        bool const file_is_ready = file_assembler_is_ready(sync_files[i].fassembler);
        if (file_is_ready)
            ready_files_num++;

        ret |= !file_is_ready;
    }

    if (ready_files_num)
        fsynchronizer_cleanup_ready_files_info(psynchronizer);

    fsync_pop_lock();

    return ret;
}
