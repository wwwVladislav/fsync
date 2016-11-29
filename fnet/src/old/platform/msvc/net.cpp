
#ifndef _MSC_VER
    Unsupported platform
#endif

#include "../../net.h"
#include <Winsock2.h>

namespace tl
{
    namespace net
    {
        volatile static unsigned net_init_count = 0;

        void init()
        {
            if (!net_init_count)
            {
                    WSADATA wsaInfo;
                    int err = ::WSAStartup(MAKEWORD(2, 2), &wsaInfo);
                    if(err)
                        throw err;
                    if(wsaInfo.wVersion != MAKEWORD(2, 2))
                        throw WSAVERNOTSUPPORTED;
            }
            ++net_init_count;
        }

        void uninit()
        {
            --net_init_count;
            if (!net_init_count)
                ::WSACleanup();
        }
    }
}
