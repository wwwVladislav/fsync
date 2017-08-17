#include "../../fs.h"
#include "../../log.h"
#include "../../mutex.h"

#ifndef _WIN32_WINNT
#   define WIN32_LEAN_AND_MEAN
#   define _WIN32_WINNT   0x0501
#endif
#include <windows.h>

#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

enum FSDIR_CONFIG
{
    FSDIR_MAX_LISTEN_DIRS = 64,
    FSDIR_MAX_HANDLERS    = 64,
    FSDIR_EXIT_SIGNAL     = 99999999
};

typedef struct fsdir_dir_info
{
    HANDLE          dir;
    DWORD           data_size;
    char            data[8196];
    OVERLAPPED      overlapped;
    size_t          path_len;
    fsdir_event_t   event;
} fsdir_dir_info_t;

struct fsdir_listener
{
    volatile bool       is_active;
    HANDLE              iocp;
    pthread_t           thread;
    fsdir_dir_info_t    dirs[FSDIR_MAX_LISTEN_DIRS];
    pthread_mutex_t     handlers_mutex;
    fsdir_evt_handler_t handlers[FSDIR_MAX_HANDLERS];
    void*               args[FSDIR_MAX_HANDLERS];
};

static DWORD const FSDIR_FILTER = FILE_NOTIFY_CHANGE_FILE_NAME
                                  | FILE_NOTIFY_CHANGE_DIR_NAME
                                  | FILE_NOTIFY_CHANGE_LAST_WRITE
                                  | FILE_NOTIFY_CHANGE_CREATION;

static fsdir_action_t fsdir_event_convert(DWORD action)
{
    switch(action)
    {
        case FILE_ACTION_ADDED:             return FSDIR_ACTION_ADDED;
        case FILE_ACTION_REMOVED:           return FSDIR_ACTION_REMOVED;
        case FILE_ACTION_MODIFIED:          return FSDIR_ACTION_MODIFIED;
        case FILE_ACTION_RENAMED_OLD_NAME:
        case FILE_ACTION_RENAMED_NEW_NAME:  return FSDIR_ACTION_RENAMED;
    }
    return FSDIR_ACTION_UNKNOWN;
}

static size_t fsdir_set_path(fsdir_dir_info_t *dir, char const *path, size_t path_len)
{
    char delimeter = '\\';
    char const *s = path;
    unsigned short i = 0;
    for(char *d = dir->event.path; i < path_len; ++s, ++d, ++i)
    {
        *d = *s;
        if (*s == '/')
            delimeter = '/';
    }
    if (dir->event.path[path_len - 1] != delimeter)
        dir->event.path[path_len++] = delimeter;
    dir->event.path[path_len] = 0;
    dir->path_len = path_len;
    return path_len;
}

static bool open_directory_and_listen(HANDLE iocp, fsdir_dir_info_t *dir)
{
    dir->dir = CreateFileA(dir->event.path,
                           FILE_LIST_DIRECTORY | GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           0,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, 0);
    if (dir->dir == INVALID_HANDLE_VALUE)
    {
        FS_ERR("Unable to open directory \'%s\'. Error: %d", dir->event.path, GetLastError());
        return false;
    }

    if (0 == CreateIoCompletionPort(dir->dir, iocp, (ULONG_PTR)dir, 0))
    {
        FS_ERR("Unable to create the IO completion port for directory \'%s\'. Error: %d", dir->event.path, GetLastError());
        CloseHandle(dir->dir);
        dir->dir = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

static void *fsdir_listener_thread(void *param)
{
    fsdir_listener_t *listener = (fsdir_listener_t*)param;
    listener->is_active = true;

    while(listener->is_active)
    {
        DWORD number_of_bytes = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED *pov = 0;

        if (GetQueuedCompletionStatus(listener->iocp,
                                      &number_of_bytes,
                                      &completion_key,
                                      &pov,
                                      INFINITE))
        {
            if (!completion_key) continue;
            fsdir_dir_info_t *dir = (fsdir_dir_info_t*)completion_key;

            if (number_of_bytes)
            {
                DWORD offset = 0;
                FILE_NOTIFY_INFORMATION *event;

                char *path = dir->event.path + dir->path_len;
                size_t size = sizeof dir->event.path - dir->path_len;

                if (size)
                {
                    do
                    {
                        event = (FILE_NOTIFY_INFORMATION *)(dir->data + offset);

                        int len = WideCharToMultiByte(CP_UTF8, 0, event->FileName, event->FileNameLength / sizeof(WCHAR), path, size - 1, 0, 0);
                        path[len] = 0;
                        dir->event.action = fsdir_event_convert(event->Action);

                        fpush_lock(listener->handlers_mutex);
                        for (unsigned i = 0; i < FSDIR_MAX_HANDLERS; ++i)
                        {
                            if (listener->handlers[i])
                                listener->handlers[i](&dir->event, listener->args[i]);
                        }
                        fpop_lock();

                        offset += event->NextEntryOffset;
                    }
                    while(event->NextEntryOffset);
                }
                else
                {
                    FS_ERR("No empty space for the path. The path is too long.");
                }
            }
            else
            {
                // Directory was closed
                dir->event.path[dir->path_len] = 0;
                FS_ERR("Directory \'%s\'was closed", dir->event.path);

                CancelIo(dir->dir);
                CloseHandle(dir->dir);
                dir->dir = INVALID_HANDLE_VALUE;

                if (!open_directory_and_listen(listener->iocp, dir))
                    continue;

                // Directory was reopened. We should notify about it.
                dir->event.action = FSDIR_ACTION_REOPENED;

                fpush_lock(listener->handlers_mutex);
                for (unsigned i = 0; i < FSDIR_MAX_HANDLERS; ++i)
                {
                    if (listener->handlers[i])
                        listener->handlers[i](&dir->event, listener->args[i]);
                }
                fpop_lock();
            }

            memset(dir->data, 0, sizeof dir->data);
            memset(&dir->overlapped, 0, sizeof dir->overlapped);
            dir->data_size = 0;

            if (!ReadDirectoryChangesW(dir->dir,
                                       dir->data,
                                       sizeof dir->data,
                                       TRUE,
                                       FSDIR_FILTER,
                                       &dir->data_size,
                                       &dir->overlapped,
                                       0))
            {
                FS_ERR("ReadDirectoryChanges failed. Error: %d", GetLastError());
            }
        }
    }

    return 0;
}

fsdir_listener_t *fsdir_listener_create()
{
    fsdir_listener_t *listener = malloc(sizeof(fsdir_listener_t));
    if (!listener)
    {
        FS_ERR("Unable to allocate memory for directory listener");
        return 0;
    }

    memset(listener, 0, sizeof *listener);
    for (unsigned i = 0; i < FSDIR_MAX_LISTEN_DIRS; ++i)
    {
        listener->dirs[i].dir = INVALID_HANDLE_VALUE;
    }

    listener->handlers_mutex = PTHREAD_MUTEX_INITIALIZER;

    listener->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    if (!listener->iocp)
    {
        FS_ERR("Unable to create the IO completion port for directory messages processing. Error: %d", GetLastError());
        free(listener);
        return 0;
    }

    int rc = pthread_create(&listener->thread, 0, fsdir_listener_thread, listener);
    if (rc)
    {
        FS_ERR("Unable to create the thread for directory messages processing. Error: %d", rc);
        CloseHandle(listener->iocp);
        free(listener);
        return 0;
    }

    static struct timespec const ts = { 1, 0 };
    while(!listener->is_active)
        nanosleep(&ts, NULL);

    return listener;
}

void fsdir_listener_free(fsdir_listener_t *listener)
{
    if (listener)
    {
        listener->is_active = false;
        PostQueuedCompletionStatus(listener->iocp, FSDIR_EXIT_SIGNAL, 0, 0);
        pthread_join(listener->thread, 0);

        for (unsigned i = 0; i < FSDIR_MAX_LISTEN_DIRS; ++i)
        {
            fsdir_dir_info_t *dir = listener->dirs + i;
            if (dir->dir != INVALID_HANDLE_VALUE)
            {
                CancelIo(dir->dir);
                CloseHandle(dir->dir);
                dir->dir = INVALID_HANDLE_VALUE;
            }
        }
        CloseHandle(listener->iocp);
        free(listener);
    }
}

bool fsdir_listener_add_path(fsdir_listener_t *listener, char const *path)
{
    if (!listener) return false;

    if (!path || !*path)
    {
        FS_ERR("The path for listener is not specified");
        return false;
    }

    size_t path_len = strlen(path);
    if (path_len + 1 > FMAX_PATH)
    {
        FS_ERR("The path for listener is too long: \'%s\'", path);
        return false;
    }

    for (unsigned i = 0; i < FSDIR_MAX_LISTEN_DIRS; ++i)
    {
        fsdir_dir_info_t *dir = listener->dirs + i;
        if (dir->dir == INVALID_HANDLE_VALUE)
        {
            memset(dir, 0, sizeof *dir);
            fsdir_set_path(dir, path, path_len);

            if (!open_directory_and_listen(listener->iocp, dir))
                return false;

            if (!ReadDirectoryChangesW(dir->dir,
                                       dir->data,
                                       sizeof dir->data,
                                       TRUE,
                                       FSDIR_FILTER,
                                       &dir->data_size,
                                       &dir->overlapped,
                                       0))
            {
                FS_ERR("ReadDirectoryChanges failed. Error: %d", GetLastError());
                CancelIo(dir->dir);
                CloseHandle(dir->dir);
                dir->dir = INVALID_HANDLE_VALUE;
                return false;
            }

            return true;
        }
    }

    FS_ERR("The maximum number of allowed for listening directories was reached.");

    return false;
}

bool fsdir_listener_reg_handler(fsdir_listener_t *listener, fsdir_evt_handler_t handler, void *arg)
{
    if (!listener) return false;
    bool ret = false;

    fpush_lock(listener->handlers_mutex);
    for (unsigned i = 0; i < FSDIR_MAX_HANDLERS; ++i)
    {
        if (!listener->handlers[i])
        {
            listener->handlers[i] = handler;
            listener->args[i] = arg;
            ret = true;
            break;
        }
    }
    fpop_lock();

    if (!ret)
        FS_ERR("The maximum number of event handlers was reached.");

    return ret;
}
