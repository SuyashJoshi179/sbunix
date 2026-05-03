#ifndef _NET_IF_H
#define _NET_IF_H

#include <sys/socket.h>

#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_hwaddr;
        short           ifru_flags;
        int             ifru_ivalue;
        int             ifru_mtu;
        char            ifru_slave[IFNAMSIZ];
        char            ifru_newname[IFNAMSIZ];
        void           *ifru_data;
    } ifr_ifru;
};

#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_ivalue    ifr_ifru.ifru_ivalue
#define ifr_mtu       ifr_ifru.ifru_mtu
#define ifr_slave     ifr_ifru.ifru_slave
#define ifr_newname   ifr_ifru.ifru_newname
#define ifr_data      ifr_ifru.ifru_data

unsigned if_nametoindex(const char *ifname);
char    *if_indextoname(unsigned ifindex, char *ifname);

#endif
