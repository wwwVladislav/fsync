
#ifndef __GNUC__
    Unsupported platform
#endif

#include "socket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

namespace tl
{
    namespace net
    {
        namespace sock
        {
            socket::data::data():
                _sock (-1),
                _rc (1)
            {
                _sock = (socket_t)::socket(AF_INET, SOCK_STREAM, 0);
            }

            socket::data::data(socket_t s):
                _sock (s),
                _rc (1)
            {}

            socket::data::~data()
            {
                close();
            }

            int socket::data::add_ref()
            {
                return ++_rc;
            }

            int socket::data::release()
            {
                int rc = --_rc;
                if (!rc)
                    delete this;
                return rc;
            }

            socket::data::operator socket_t ()
            {
                return _sock;
            }

            void socket::data::close()
            {
                if (_sock != -1)
                {
                    ::shutdown (_sock, SHUT_RDWR);
                    ::close (_sock);
                }
                _sock = -1;
            }

            // *****************************************************

            socket::socket()
            {
                _data = new data;
            }

            socket::socket(socket_t s)
            {
                _data = new data(s);
            }

            socket::socket(const socket & s)
            {
                _data = s._data;
                _data->add_ref();
            }

            socket::~socket()
            {
                _data->release();
            }

            socket & socket::operator = (const socket & s)
            {
                if (_data == s._data) return *this;
                if (_data) _data->release();
                _data = s._data;
                if (_data) _data->add_ref();
                return *this;
            }

            socket::operator socket_t ()
            {
                return *_data;
            }

            bool socket::is_ok() const
            {
                return _data && *_data != (socket_t)-1;
            }

            bool socket::send(const char * buf, int len)
            {
                if (!is_ok())
                    return false;

                int res = 0;

                do
                {
                    res = ::send((socket_t)*_data, buf, len, 0);
                    if (res == -1)    // error
                        return false;
                    buf += res;
                    len -= res;
                }
                while (len > 0);

                return true;
            }

            bool socket::read(char * buf, int len, int & read_size, bool read_all)
            {
                if (!is_ok())
                    return false;

                read_size = ::recv((socket_t)*_data, buf, len, 0);
                if (!read_all)
                    return read_size != -1;

                if (read_size == -1)  // error
                    return false;

                if (read_size == 0)             // connection closed
                    return read_size == len;

                buf += read_size;
                len -= read_size;

                while (len > 0)
                {
                    int res = ::recv((socket_t)*_data, buf, len, 0);

                    if (res == -1)    // error
                        return false;

                    if (res == 0)             // connection closed
                        return len == 0;

                    read_size += res;
                    buf += res;
                    len -= res;
                }

                return true;
            }

            void socket::close()
            {
                if (_data)
                    _data->close();
            }

            bool socket::timeout(int tsec)
            {
                if (!is_ok())
                    return false;

                struct timeval timeout;
                timeout.tv_sec = tsec;
                timeout.tv_usec = 0;

                return ::setsockopt((socket_t)*_data, SOL_SOCKET, SO_RCVTIMEO, (const void*)&timeout, sizeof(timeout)) == 0;
            }

            bool socket::wait_recv()
            {
                if (!is_ok())
                    return false;

                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET((socket_t)*_data, &readfds);

                fd_set exceptfds;
                FD_ZERO(&exceptfds);
                FD_SET((socket_t)*_data, &exceptfds);

                if (::select(1 + (socket_t)*_data, &readfds, 0, &exceptfds, 0) == -1)
                    return false;

                if (FD_ISSET((socket_t)*_data, &exceptfds))
                    return false;

                return FD_ISSET((socket_t)*_data, &readfds) != 0;
            }

            // **************************************************************

            bool client::connect(const ip_address & addr)
            {
                if (addr.family() == ip_address::IPV4)
                {
                    sockaddr_in remote_addr;
                    memset (&remote_addr, 0, sizeof(sockaddr_in));
                    remote_addr.sin_family = AF_INET;
                    unsigned * puint = (unsigned*)addr.addr().v4;
                    remote_addr.sin_addr.s_addr = *puint;
                    remote_addr.sin_port = htons(addr.port());

                    if (::connect((socket_t)*this, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) == -1)
                    {
                        close();
                        return false;
                    }
                }
                else
                    throw "TODO: ipv6";

                return true;
            }

            client::operator bool() const
            {
                return is_ok();
            }

            // **************************************************************

            server::server(short port)
            {
                if ((socket_t)_socket != -1)
                {
                    int optval = 1;
                    setsockopt((socket_t)_socket, SOL_SOCKET, SO_REUSEADDR, (void*)&optval, sizeof(optval));
                }

                sockaddr_in local_addr;
                memset (&local_addr, 0, sizeof (sockaddr_in));
                local_addr.sin_family = AF_INET;
                local_addr.sin_addr.s_addr  = INADDR_ANY; 
                local_addr.sin_port = htons(port);

                if (::bind((socket_t)_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) == -1)
                {
                    _socket.close();
                    return;
                }

                if (::listen((socket_t)_socket, SOMAXCONN) == -1)
                {
                    _socket.close();
                    return;
                }
            }

            socket server::accept()
            {
                return socket((socket_t)::accept((socket_t)_socket, 0, 0));
            }

            void server::close()
            {
                _socket.close();
            }

            server::operator bool() const
            {
                return _socket.is_ok();
            }

            // **************************************************************

            bool select(const socket_t * socks, size_t count, const unsigned * msec,
                        socket_t * rsocks, size_t * rcount,
                        socket_t * wsocks, size_t * wcount,
                        socket_t * esocks, size_t * ecount)
            {
                if (!count || !socks)
                    return false;

                fd_set readfds;
                fd_set writefds;
                fd_set exceptfds;
                timeval timeout = {0, 0};

                fd_set * preadfds = 0;
                fd_set * pwritefds = 0;
                fd_set * pexceptfds = 0;
                timeval * ptimeout = 0;

                if (rsocks && rcount)
                {
                    FD_ZERO(&readfds);
                    preadfds = &readfds;
                    *rcount = 0;
                }

                if (wsocks && wcount)
                {
                    FD_ZERO(&writefds);
                    pwritefds = &writefds;
                    *wcount = 0;
                }

                if (esocks && ecount)
                {
                    FD_ZERO(&exceptfds);
                    pexceptfds = &exceptfds;
                    *ecount = 0;
                }

                if (!preadfds && !pwritefds && !pexceptfds)
                    return false;

                if (msec)
                {
                    timeout.tv_sec = *msec / 1000;
                    timeout.tv_usec = (*msec - timeout.tv_sec * 1000) * 1000;
                    ptimeout = &timeout;
                }

                int fd_max = 0;

                for (size_t i = 0; i < count; ++i)
                {
                    int sock = (int)socks[i];
                    if (preadfds)   FD_SET(sock, preadfds);
                    if (pwritefds)  FD_SET(sock, pwritefds);
                    if (pexceptfds) FD_SET(sock, pexceptfds);
                    if (sock > fd_max)
                        fd_max = sock;
                }

                if (::select(fd_max + 1, preadfds, pwritefds, pexceptfds, ptimeout) == -1)
                    return false;

                for (size_t i = 0; i < count; ++i)
                {
                    socket_t s = socks[i];
                    int sock = (int)s;
                    if (preadfds && FD_ISSET(sock, preadfds) != 0)      rsocks[(*rcount)++] = s;
                    if (pwritefds && FD_ISSET(sock, pwritefds) != 0)    wsocks[(*wcount)++] = s;
                    if (pexceptfds && FD_ISSET(sock, pexceptfds) != 0)  esocks[(*ecount)++] = s;
                }

                return true;
            }
        }   // namespace sock
    }   // namespace net
}   // namespace tl
