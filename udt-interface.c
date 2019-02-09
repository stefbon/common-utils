/*
  2010, 2011, 2012, 2103, 2014, 2015, 2016 Stef Bon <stefbon@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "global-defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>

#include <sys/param.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include "udt-interface.h"

#ifdef HAVE_LIBUDT

#include <udt/udt.h>

int UDTaccept(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return (int) UDT::accept(s, addr, (int) len);
}

int UDTbind(unsigned int fd, struct sockaddr *addr, int *len, int sock)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::bind(s, addr, len, sock);
}

int UDTclose(unsigned int fd)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::close(s);
}

int UDTconnect(unsigned int fd, struct sockaddr *addr, int *len)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::connect(s, addr, len);
}

int UDTgetpeername(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::getpeername(s, addr, len);
}

int UDTgetsockname(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::getsockname(s, addr, len);
}

int UDTgetsockopt(unsigned int fd, int level, int n, char *v, unsigned int *l)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::getsockopt(s, level, n, v, (int *)l);
}

int UDTlisten(unsigned int fd, int blog)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::listen(s, blog);
}

int UDTrecv(unsigned int fd, char *buffer, unsigned int size, unsigned int flags)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::recv(s, buffer, size, flags);
}

int UDTsend(unsigned int fd, char *buffer, unsigned int size, unsigned int flags)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::send(s, buffer, size, flags);
}

int UDTsetsockopt(unsigned int fd, int level, int n, char *v, unsigned int l)
{
    UDTSOCKET s=(UDTSOCKET) fd;
    return UDT::getsockopt(s, level, (SOCKOPT) n, v, l);
}

int UDTsocket(int af, int type, int protocol)
{
    return (int) UDT::socket(af, type, protocol);
}

int UDTstartup()
{
    return UDT::startup();
}

int UDTcleanup()
{
    return UDT::cleanup();
}

#else

int UDTaccept(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return -1;
}

int UDTbind(unsigned int fd, struct sockaddr *addr, int *len, int sock)
{
    return -1;
}

int UDTclose(unsigned int fd)
{
    return -1;
}

int UDTconnect(unsigned int fd, struct sockaddr *addr, int *len)
{
    return -1;
}

int UDTgetpeername(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return -1;
}

int UDTgetsockname(unsigned int fd, struct sockaddr *addr, unsigned int *len)
{
    return -1;
}

int UDTgetsockopt(unsigned int fd, int level, int n, char *v, int *l)
{
    return -1;
}

int UDTlisten(unsigned int fd, int blog)
{
    return -1;
}

int UDTrecv(unsigned int fd, char *buffer, unsigned int size, unsigned int flags)
{
    return -1;
}

int UDTsend(unsigned int fd, char *buffer, unsigned int size, unsigned int flags)
{
    return -1;
}

int UDTsetsockopt(unsigned int fd, int level, int n, char *v, unsigned int l)
{
    return -1;
}

int UDTsocket(int af, int type, int protocol)
{
    return -1;
}

int UDTstartup()
{
    return -1;
}

int UDTcleanup()
{
    return -1;
}

#endif

static struct socket_ops_s udt_sops = {
    .type				=	SOCKET_OPS_TYPE_UDT,
    .accept				=	UDTaccept,
    .bind				=	UDTbind,
    .close				=	UDTclose,
    .connect				=	UDTconnect,
    .getpeername			=	UDTgetpeername,
    .getsockname			=	UDTgetsockname,
    .listen				=	UDTlisten,
    .recv				=	UDTrecv,
    .send				=	UDTsend,
    .setsockopt				=	UDTsetsockopt,
    .socket				=	UDTsocket,
    .start				=	UDTstartup,
    .finish				=	UDTcleanup,
};

void setsocketopsudt(struct fs_connection_s *conn)
{
    conn->sops=&udt_sops;
}
