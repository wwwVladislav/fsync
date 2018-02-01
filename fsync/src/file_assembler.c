#include "file_assembler.h"
#include <futils/fs.h>
#include <futils/log.h>
#include <fcommon/limits.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static char const TMP_FILE_EXT[] = ".tmp";

// ||||||||||||||||||||||||||||||||||||||   |    ||||    |||
//          available_size              | requested_blocks |

static uint8_t REQ_BLOCK_EMPTY = 0;
static uint8_t REQ_BLOCK_REQUESTED = 1;
static uint8_t REQ_BLOCK_AVAILABLE = 2;

struct file_assembler
{
    char       *path;
    int         fd;
    uint64_t    size;
    uint64_t    available_size;
    uint8_t     requested_blocks[FSYNC_MAX_REQ_PARTS_NUM];
};

file_assembler_t *file_assembler_open(char const *path, uint64_t size)
{
    if (!path)
    {
        FS_ERR("Invalid path");
        return 0;
    }

    file_assembler_t *passembler = malloc(sizeof(file_assembler_t));
    if (!passembler)
    {
        FS_ERR("Unable to allocate memory for file assembler");
        return 0;
    }
    memset(passembler, 0, sizeof *passembler);

    passembler->path = strdup(path);
    if (!passembler->path)
    {
        file_assembler_close(passembler);
        return 0;
    }

    passembler->size = size;

    char tmp_path[strlen(path) + sizeof TMP_FILE_EXT];
    snprintf(tmp_path, sizeof tmp_path, "%s%s", path, TMP_FILE_EXT);

    uint64_t file_size = 0;
    fsfile_size(tmp_path, &file_size);

    passembler->fd = open(tmp_path, O_CREAT | /*O_BINARY |*/ O_RDWR, 0777);
    if (passembler->fd == -1)
    {
        file_assembler_close(passembler);
        return 0;
    }

    if (lseek(passembler->fd, size, SEEK_SET) < 0)
    {
        file_assembler_close(passembler);
        return 0;
    }

    if (file_size >= size + sizeof passembler->available_size + sizeof sizeof passembler->requested_blocks)
    {
        if (read(passembler->fd, &passembler->available_size, sizeof passembler->available_size) != sizeof passembler->available_size
            || read(passembler->fd, passembler->requested_blocks, sizeof passembler->requested_blocks) != sizeof passembler->requested_blocks)
        {
            FS_ERR("Unable to read data from the file: \'%s\'", path);
            file_assembler_close(passembler);
            return 0;
        }

        for (int i = 0; i < sizeof passembler->requested_blocks / sizeof *passembler->requested_blocks; ++i)
            if (passembler->requested_blocks[i] == REQ_BLOCK_REQUESTED)
                passembler->requested_blocks[i] = REQ_BLOCK_EMPTY;
    }

    if (write(passembler->fd, &passembler->available_size, sizeof passembler->available_size) < 0
        || write(passembler->fd, passembler->requested_blocks, sizeof passembler->requested_blocks) < 0)
    {
        FS_ERR("Unable to write data into the file: \'%s\'", path);
        file_assembler_close(passembler);
        return 0;
    }

    return passembler;
}

void file_assembler_close(file_assembler_t *passembler)
{
    if (passembler)
    {
        if (passembler->path)
            free(passembler->path);
        if (passembler->fd != -1)
            close(passembler->fd);
        free(passembler);
    }
}

bool file_assembler_request_block(file_assembler_t *passembler, uint32_t *pblock)
{
    if (!passembler || !pblock)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    if (!passembler->size)
        return false;

    uint32_t const blocks_num = (passembler->size + FSYNC_BLOCK_SIZE - 1) / FSYNC_BLOCK_SIZE;
    uint32_t const available_blocks_num = (passembler->available_size + FSYNC_BLOCK_SIZE - 1) / FSYNC_BLOCK_SIZE;

    for (int i = 0; i < sizeof passembler->requested_blocks / sizeof *passembler->requested_blocks && available_blocks_num + i < blocks_num; ++i)
    {
        if (passembler->requested_blocks[i] == REQ_BLOCK_EMPTY)
        {
            if (lseek(passembler->fd, passembler->size + sizeof passembler->available_size + i * sizeof *passembler->requested_blocks, SEEK_SET) < 0)
                return false;

            if (write(passembler->fd, &REQ_BLOCK_REQUESTED, sizeof REQ_BLOCK_REQUESTED) != sizeof REQ_BLOCK_REQUESTED)
                return false;

            passembler->requested_blocks[i] = REQ_BLOCK_REQUESTED;
            *pblock = passembler->available_size / FSYNC_BLOCK_SIZE + i;

            return true;
        }
    }

    return false;
}

bool file_assembler_add_block(file_assembler_t *passembler, uint32_t block, uint8_t const *data, size_t const size)
{
    if (!passembler || !data)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    uint32_t const blocks_num = (passembler->size + FSYNC_BLOCK_SIZE - 1) / FSYNC_BLOCK_SIZE;
    uint32_t const available_blocks_num = passembler->available_size / FSYNC_BLOCK_SIZE;
    size_t const requested_blocks_size = sizeof passembler->requested_blocks / sizeof *passembler->requested_blocks;
    if (block < available_blocks_num)
        return true;

    uint32_t id = block - available_blocks_num;

    if (passembler->requested_blocks[id] != REQ_BLOCK_REQUESTED)
        return false;

    if (lseek(passembler->fd, block * FSYNC_BLOCK_SIZE, SEEK_SET) < 0)
        return false;

    if (write(passembler->fd, data, size) != size)
        return false;

    if (id == 0)
    {
        uint32_t n = 1;
        for(; id + n < requested_blocks_size
              && id + n < blocks_num
              && passembler->requested_blocks[id + n] == REQ_BLOCK_AVAILABLE;
            ++n);

        if (passembler->available_size + n * FSYNC_BLOCK_SIZE >= passembler->size)
        {
            // file is ready
            if (ftruncate(passembler->fd, passembler->size) != 0)
                return false;

            passembler->available_size = passembler->size;
            memset(passembler->requested_blocks, REQ_BLOCK_AVAILABLE, sizeof passembler->requested_blocks);

            // Close file
            close(passembler->fd);
            passembler->fd = -1;

            // rename file
            char tmp_path[strlen(passembler->path) + sizeof TMP_FILE_EXT];
            snprintf(tmp_path, sizeof tmp_path, "%s%s", passembler->path, TMP_FILE_EXT);

            return rename(tmp_path, passembler->path) == 0;
        }
        else
        {
            uint64_t const new_available_size = passembler->available_size + n * FSYNC_BLOCK_SIZE;

            uint8_t new_requested_blocks[requested_blocks_size];
            memcpy(new_requested_blocks, passembler->requested_blocks + n, requested_blocks_size - n);
            memset(new_requested_blocks + requested_blocks_size - n, REQ_BLOCK_EMPTY, n * sizeof *passembler->requested_blocks);

            if (lseek(passembler->fd, passembler->size + sizeof passembler->available_size, SEEK_SET) < 0)
                return false;

            if (write(passembler->fd, new_requested_blocks, sizeof new_requested_blocks) != sizeof new_requested_blocks)
                return false;

            if (lseek(passembler->fd, passembler->size, SEEK_SET) < 0)
                return false;

            if (write(passembler->fd, &new_available_size, sizeof new_available_size) != sizeof new_available_size)
                return false;

            passembler->available_size = new_available_size;
            memcpy(passembler->requested_blocks, new_requested_blocks, sizeof passembler->requested_blocks);
        }
    }
    else
    {
        if (lseek(passembler->fd, passembler->size + sizeof passembler->available_size + id * sizeof *passembler->requested_blocks, SEEK_SET) < 0)
            return false;

        if (write(passembler->fd, &REQ_BLOCK_AVAILABLE, sizeof REQ_BLOCK_AVAILABLE) != sizeof REQ_BLOCK_AVAILABLE)
            return false;

        passembler->requested_blocks[id] = REQ_BLOCK_AVAILABLE;
    }

    return true;
}

bool file_assembler_is_ready(file_assembler_t const *passembler)
{
    return passembler ? passembler->available_size == passembler->size : false;
}
