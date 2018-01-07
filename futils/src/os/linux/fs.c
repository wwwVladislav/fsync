#include "../../fs.h"
#include "../../log.h"
#include "../../mutex.h"

struct fsdir_listener
{
    volatile bool       is_active;
    pthread_t           thread;
};

bool fsdir_listener_reg_handler(fsdir_listener_t *listener, fsdir_evt_handler_t handler, void *arg)
{
    // TODO: fsdir_listener_reg_handler
    FS_ERR("TODO!");
    return false;
}

fsdir_listener_t *fsdir_listener_create()
{
    // TODO: fsdir_listener_create
    FS_ERR("TODO!");
    return 0;
}

void fsdir_listener_free(fsdir_listener_t *listener)
{
    if (listener)
    {
        listener->is_active = false;
        pthread_join(listener->thread, 0);

        // TODO: fsdir_listener_free
        FS_ERR("TODO!");

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

    // TODO: fsdir_listener_add_path
    FS_ERR("TODO!");

    FS_ERR("The maximum number of allowed for listening directories was reached.");

    return false;
}
