#pragma once
#include <sys/socket.h>

struct addrinfo {
	int ai_flags, ai_family, ai_socktype, ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

struct hostent {
	char *h_name;
	char **h_aliases;
	int h_addrtype, h_length;
	char **h_addr_list;
};

#define AI_PASSIVE 1

