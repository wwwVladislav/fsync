
#ifndef _MSC_VER
    Unsupported platform
#endif

#ifndef _socket_h_06954_
#define _socket_h_06954_
#include "../../ip_address.h"

namespace tl
{
    namespace net
    {
        namespace sock
        {
            typedef unsigned socket_t;

            class socket
            {
                class data
                {
                    socket_t _sock;
                    int _rc;

                private:
                    ~data() throw();

                public:
                    data() throw();
                    data(socket_t s) throw();
                    int retain() throw();
                    int release() throw();
                    operator socket_t () throw();
                    void close() throw();
                };

                data * _data;

            public:
                socket();
                socket(socket_t s);
                socket(const socket & s) throw();
                ~socket() throw();
                socket & operator = (const socket & s) throw();
                operator socket_t () throw();
                bool is_ok() const throw();
                bool send(const char * buf, int len) throw();
                bool read(char * buf, int len, int & read_size, bool read_all) throw();
                void close() throw();
                bool timeout(int tsec) throw();
                bool select(bool * readfd, bool * writefd, bool * exceptfd, const unsigned * msec) throw();
            };

            class client : public socket
            {
            public:
                bool connect(const ip_address & addr);
                operator bool() const throw();
            };

            class server
            {
                socket _socket;

            public:
                server(short port) throw();
                socket accept();
                void close() throw();
                operator bool() const throw();
            };

            bool select(const socket_t * socks, size_t count, const unsigned * msec,
                        socket_t * rsocks, size_t * rcount,
                        socket_t * wsocks, size_t * wcount,
                        socket_t * esocks, size_t * ecount) throw();
        }   // namespace sock
    }   // namespace net
}   // namespace tl

#endif  // _socket_h_06954_
