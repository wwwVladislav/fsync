
#ifndef __GNUC__
    Unsupported platform
#endif

#ifndef _socket_h_76856_
#define _socket_h_76856_
#include "../../ip_address.h"

namespace tl
{
    namespace net
    {
        namespace sock
        {
            typedef int socket_t;

            class socket
            {
                class data
                {
                    socket_t _sock;
                    int _rc;

                private:
                    ~data();

                public:
                    data();
                    data(socket_t s);
                    int add_ref();
                    int release();
                    operator socket_t ();
                    void close();
                };

                data * _data;

            public:
                socket();
                socket(socket_t s);
                socket(const socket & s);
                virtual ~socket();
                socket & operator = (const socket & s);
                operator socket_t ();
                bool is_ok() const;
                bool send(const char * buf, int len);
                bool read(char * buf, int len, int & read_size, bool read_all);
                void close();
                bool timeout(int tsec);
                bool wait_recv();
            };

            class client : public socket
            {
            public:
                bool connect(const ip_address & addr);
                operator bool() const;
            };

            class server
            {
                socket _socket;

            public:
                server(short port);
                socket accept();
                void close();
                operator bool() const;
            };

            bool select(const socket_t * socks, size_t count, const unsigned * msec,
                        socket_t * rsocks, size_t * rcount,
                        socket_t * wsocks, size_t * wcount,
                        socket_t * esocks, size_t * ecount);
        }   // namespace sock
    }   // namespace net
}   // namespace tl

#endif
