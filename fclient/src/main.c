#include <fsync/sync.h>
#include <fnet/transport.h>
#include <stdio.h>
#include <unistd.h>

static void clients_accepter(fnet_server_t const *pserver, fnet_client_t *pclient)
{
    fnet_disconnect(pclient);
}

void test_transport()
{
    fnet_server_t *pserver = fnet_bind(FNET_SSL, "127.0.0.1:12345", clients_accepter);
    if (pserver)
    {
        for(int i = 0; i < 2; ++ i)
        {
            fnet_client_t *pclient = fnet_connect(FNET_SSL, "127.0.0.1:12345");
            if (pclient)
            {
                sleep(1);
                fnet_disconnect(pclient);
            }
        }

        fnet_unbind(pserver);
    }
}

void test_fsync()
{
    fsync_t *sync = fsync_create("C:\\Temp");
    if(sync)
    {
        sleep(600);
        fsync_free(sync);
    }
}

int main()
{
    test_fsync();
    test_transport();
    return 0;
}
