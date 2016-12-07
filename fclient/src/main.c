#include <fsync/fsutils.h>
#include <fsync/device.h>
#include <fnet/transport.h>
#include <stdio.h>
#include <unistd.h>

void dir_evt_handler(fsdir_action_t evt, char const *path)
{
    printf("[%d] %s\n", evt, path);
}

void test_fsiterator()
{
    // Dirs iterator
    fsiterator_t *it = fsdir_iterator("C:\\Temp");
    char path[FSMAX_PATH];
    for(dirent_t entry; fsdir_iterator_next(it, &entry); )
    {
        fsdir_iterator_directory(it, path, sizeof path);
        if (entry.type == FS_DIR)
            printf("[%d] %s\n", entry.type, path);
        else
            printf("[%d] %s\\%s\n", entry.type, path, entry.name);
    }
    fsdir_iterator_free(it);

    // Wait for changes
    fsdir_listener_t *listener = fsdir_listener_create();
    if (listener)
    {
        bool ret = fsdir_listener_reg_handler(listener, dir_evt_handler);
        (void)ret;
        ret = fsdir_listener_add_path(listener, "C:\\Temp");
        ret = fsdir_listener_add_path(listener, "D:\\Dev\\");
        (void)ret;
        sleep(60);
        fsdir_listener_free(listener);
    }
}

void dev_hotplug_callback(fsdev_action_t action, char const *name)
{
    printf("%s: %s\n", action == FSDEV_ARRIVAL ? "Mount" : "Unmount", name);
}

void test_dev()
{
    fsdev_handle_t h = fsdev_hotplug_register_callback(FSDEV_VOLUME, dev_hotplug_callback);
    sleep(10);
    fsdev_hotplug_unregister_callback(h);

    fsdev_volume_t volumes[64];
    unsigned size = fsdev_get_all_volumes(volumes, 64);
    for(unsigned i = 0; i < size; ++i)
        printf("%s\n", volumes[i].name);
}

static void clients_accepter(fnet_ssl_server_t const *pserver, fnet_ssl_client_t *pclient)
{
    fnet_ssl_disconnect(pclient);
}

void test_transport()
{
    fnet_ssl_server_t *pserver = fnet_ssl_bind("127.0.0.1:12345", clients_accepter);
    if (pserver)
    {
        for(int i = 0; i < 2; ++ i)
        {
            fnet_ssl_client_t *pclient = fnet_ssl_connect("127.0.0.1:12345");
            if (pclient)
            {
                sleep(1);
                fnet_ssl_disconnect(pclient);
            }
        }

        fnet_ssl_unbind(pserver);
    }
}

int main()
{
    test_transport();
    test_fsiterator();
    test_dev();
    return 0;
}
