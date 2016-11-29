
#ifndef _MSC_VER
    Unsupported platform
#endif

#include "socket.h"
#include <Winsock2.h>

#define WS_FD_SET(fd, set)                                  \
    /* warning C4127: conditional expression is constant */ \
    __pragma( warning( suppress : 4127 ))                   \
    FD_SET(fd, set)

namespace tl
{
    namespace net
    {
        namespace sock
        {
            socket::data::data() throw():
                _sock ((socket_t)-1),
                _rc (1)
            {
                _sock = (socket_t)::socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
            }

            socket::data::data(socket_t s) throw():
                _sock (s),
                _rc (1)
            {}

            socket::data::~data() throw()
            {
                close();
            }

            int socket::data::retain() throw()
            {
                return ++_rc;
            }

            int socket::data::release() throw()
            {
                int rc = --_rc;
                if (!rc) delete this;
                return rc;
            }

            socket::data::operator socket_t() throw()
            {
                return _sock;
            }

            void socket::data::close() throw()
            {
                if (_sock != (socket_t)-1)
                {
                    ::shutdown ((SOCKET)_sock, SD_BOTH);
                    ::closesocket ((SOCKET)_sock);
                }
                _sock = (socket_t)-1;
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

            socket::socket(const socket & s) throw()
            {
                _data = s._data;
                _data->retain();
            }

            socket::~socket() throw()
            {
                _data->release();
            }

            socket & socket::operator = (const socket & s) throw()
            {
                if (_data == s._data) return *this;
                if (_data) _data->release();
                _data = s._data;
                if (_data) _data->retain();
                return *this;
            }

            socket::operator socket_t() throw()
            {
                if (!is_ok()) return (socket_t)-1;
                return (socket_t)*_data;
            }

            bool socket::is_ok() const throw()
            {
                return _data && *_data != (socket_t)-1;
            }

            bool socket::send(const char * buf, int len) throw()
            {
                if (!is_ok()) return false;

                int res = 0;

                do
                {
                    res = ::send((SOCKET)(socket_t)*_data, buf, len, 0);
                    if (res == SOCKET_ERROR)    // error
                        return false;
                    buf += res;
                    len -= res;
                }
                while (len > 0);

                return true;
            }

            bool socket::read(char * buf, int len, int & read_size, bool read_all) throw()
            {
                if (!is_ok())
                    return false;

                read_size = ::recv((SOCKET)(socket_t)*_data, buf, len, 0);
                if (!read_all)
                    return read_size != SOCKET_ERROR;

                if (read_size == SOCKET_ERROR)  // error
                    return false;

                if (read_size == 0)             // connection closed
                    return read_size == len;

                buf += read_size;
                len -= read_size;

                while (len > 0)
                {
                    int res = ::recv((SOCKET)(socket_t)*_data, buf, len, 0);

                    if (res == SOCKET_ERROR)    // error
                        return false;

                    if (res == 0)             // connection closed
                        return len == 0;

                    read_size += res;
                    buf += res;
                    len -= res;
                }

                return true;
            }

            void socket::close() throw()
            {
                if (_data) _data->close();
            }

            bool socket::timeout(int tsec) throw()
            {
                if (!is_ok()) return false;
                tsec *= 1000;
                return ::setsockopt((SOCKET)(socket_t)*_data, SOL_SOCKET, SO_RCVTIMEO, (char*)&tsec, sizeof(tsec)) == 0;
            }

            bool socket::select(bool * readfd, bool * writefd, bool * exceptfd, const unsigned * msec) throw()
            {
                if (!is_ok()) return false;
                if (!readfd && !writefd && !exceptfd)
                    return false;

                SOCKET sock = (SOCKET)(socket_t)*_data;

                fd_set readfds;
                fd_set writefds;
                fd_set exceptfds;
                timeval timeout = {0, 0};

                fd_set * preadfds = 0;
                fd_set * pwritefds = 0;
                fd_set * pexceptfds = 0;
                timeval * ptimeout = 0;

                if (readfd)
                {
                    FD_ZERO(&readfds);
                    WS_FD_SET(sock, &readfds);
                    preadfds = &readfds;
                }

                if (writefd)
                {
                    FD_ZERO(&writefds);
                    WS_FD_SET(sock, &writefds);
                    pwritefds = &writefds;
                }

                if (exceptfd)
                {
                    FD_ZERO(&exceptfds);
                    WS_FD_SET(sock, &exceptfds);
                    pexceptfds = &exceptfds;
                }

                if (msec)
                {
                    timeout.tv_sec = *msec / 1000;
                    timeout.tv_usec = (*msec - timeout.tv_sec * 1000) * 1000;
                    ptimeout = &timeout;
                }

                if (::select(0, preadfds, pwritefds, pexceptfds, ptimeout) == SOCKET_ERROR)
                    return false;

                if (readfd)
                    *readfd = FD_ISSET(sock, &readfds) != 0;
                if (writefd)
                    *writefd = FD_ISSET(sock, &writefds) != 0;
                if (exceptfd)
                    *exceptfd = FD_ISSET(sock, &exceptfds) != 0;

                return true;
            }

            // **************************************************************

            bool client::connect(const ip_address & addr)
            {
                if (addr.family() == ip_address::IPV4)
                {
                    sockaddr_in remote_addr;
                    memset (&remote_addr, 0, sizeof(sockaddr_in));
                    remote_addr.sin_family = AF_INET;
                    remote_addr.sin_addr.S_un.S_addr = *(unsigned*)addr.addr().v4;
                    remote_addr.sin_port = htons(addr.port());

                    if (::connect ((SOCKET)(socket_t)*this, (SOCKADDR*)&remote_addr, sizeof(remote_addr)) == SOCKET_ERROR)
                    {
                        close();
                        return false;
                    }
                }
                else
                    throw "TODO: ipv6";

                return true;
            }

            client::operator bool() const throw()
            {
                return is_ok();
            }

            // **************************************************************

            server::server(short port) throw()
            {
                sockaddr_in local_addr;
                memset (&local_addr, 0, sizeof (sockaddr_in));
                local_addr.sin_family = AF_INET;
                local_addr.sin_addr.s_addr  = INADDR_ANY; 
                local_addr.sin_port = htons(port);

                if (bind ((SOCKET)(socket_t)_socket, (SOCKADDR*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR)
                {
                    _socket.close();
                    return;
                }

                if (listen ((SOCKET)(socket_t)_socket, SOMAXCONN) == SOCKET_ERROR)
                {
                    _socket.close();
                    return;
                }
            }

            socket server::accept()
            {
                return socket((socket_t)::accept((SOCKET)(socket_t)_socket, 0, 0));
            }

            void server::close() throw()
            {
                _socket.close();
            }

            server::operator bool() const throw()
            {
                return _socket.is_ok();
            }

            // **************************************************************

            bool select(const socket_t * socks, size_t count, const unsigned * msec,
                        socket_t * rsocks, size_t * rcount,
                        socket_t * wsocks, size_t * wcount,
                        socket_t * esocks, size_t * ecount) throw()
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

                for (size_t i = 0; i < count; ++i)
                {
                    SOCKET sock = (SOCKET)socks[i];
                    if (preadfds)   WS_FD_SET(sock, preadfds);
                    if (pwritefds)  WS_FD_SET(sock, pwritefds);
                    if (pexceptfds) WS_FD_SET(sock, pexceptfds);
                }

                if (::select(0, preadfds, pwritefds, pexceptfds, ptimeout) == SOCKET_ERROR)
                    return false;

                for (size_t i = 0; i < count; ++i)
                {
                    socket_t s = socks[i];
                    SOCKET sock = (SOCKET)s;
                    if (preadfds && FD_ISSET(sock, preadfds) != 0)      rsocks[(*rcount)++] = s;
                    if (pwritefds && FD_ISSET(sock, pwritefds) != 0)    wsocks[(*wcount)++] = s;
                    if (pexceptfds && FD_ISSET(sock, pexceptfds) != 0)  esocks[(*ecount)++] = s;
                }

                return true;
            }
        }   // namespace sock
    }   // namespace net
}   // namespace tl
