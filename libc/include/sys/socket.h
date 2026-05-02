#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>

/* Networking is not implemented in SBUnix. This header provides shape-correct
 * declarations so libbb.h compiles; any applet that actually calls socket(),
 * connect(), etc. will fail to link, signalling that it should be disabled
 * in the BusyBox config rather than ported. */

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __pad[126];
};

struct msghdr {
    void           *msg_name;
    socklen_t       msg_namelen;
    struct iovec   *msg_iov;
    size_t          msg_iovlen;
    void           *msg_control;
    size_t          msg_controllen;
    int             msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

struct linger {
    int l_onoff;
    int l_linger;
};

#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_SEQPACKET 5
#define SOCK_CLOEXEC   02000000
#define SOCK_NONBLOCK  04000

#define AF_UNSPEC      0
#define AF_UNIX        1
#define AF_LOCAL       AF_UNIX
#define AF_INET        2
#define AF_INET6       10
#define AF_PACKET      17
#define AF_NETLINK     16

#define PF_UNSPEC      AF_UNSPEC
#define PF_UNIX        AF_UNIX
#define PF_LOCAL       AF_LOCAL
#define PF_INET        AF_INET
#define PF_INET6       AF_INET6
#define PF_PACKET      AF_PACKET
#define PF_NETLINK     AF_NETLINK

#define SOL_SOCKET     1
#define SO_REUSEADDR   2
#define SO_TYPE        3
#define SO_ERROR       4
#define SO_BROADCAST   6
#define SO_KEEPALIVE   9
#define SO_LINGER      13
#define SO_RCVBUF      8
#define SO_SNDBUF      7
#define SO_RCVTIMEO    20
#define SO_SNDTIMEO    21
#define SO_BINDTODEVICE 25

#define MSG_OOB        0x0001
#define MSG_PEEK       0x0002
#define MSG_DONTROUTE  0x0004
#define MSG_DONTWAIT   0x0040
#define MSG_NOSIGNAL   0x4000

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

int  socket(int domain, int type, int protocol);
int  socketpair(int domain, int type, int protocol, int sv[2]);
int  bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int  connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int  listen(int sockfd, int backlog);
int  accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t send(int s, const void *buf, size_t len, int flags);
ssize_t recv(int s, void *buf, size_t len, int flags);
ssize_t sendto(int s, const void *buf, size_t len, int flags,
               const struct sockaddr *to, socklen_t tolen);
ssize_t recvfrom(int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen);
ssize_t sendmsg(int s, const struct msghdr *msg, int flags);
ssize_t recvmsg(int s, struct msghdr *msg, int flags);
int  getsockname(int s, struct sockaddr *addr, socklen_t *addrlen);
int  getpeername(int s, struct sockaddr *addr, socklen_t *addrlen);
int  setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int  getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int  shutdown(int s, int how);

#endif
