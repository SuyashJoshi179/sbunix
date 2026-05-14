#pragma once
#include <sys/socket.h>
#include <stdint.h>

#define IPPROTO_IP  0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

struct in_addr { uint32_t s_addr; };

struct sockaddr_in {
	sa_family_t sin_family;
	uint16_t sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};

struct in6_addr { uint8_t s6_addr[16]; };

struct sockaddr_in6 {
	sa_family_t sin6_family;
	uint16_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

#define INADDR_ANY       ((uint32_t)0)
#define INADDR_BROADCAST ((uint32_t)0xffffffff)
#define INADDR_LOOPBACK  ((uint32_t)0x7f000001)
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46
