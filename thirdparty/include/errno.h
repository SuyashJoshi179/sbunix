#pragma once
#include_next <errno.h>

#define ENOTSOCK    88
#define EDESTADDRREQ 89
#define EMSGSIZE    90
#define EPROTOTYPE  91
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP  95
#define EAFNOSUPPORT 97
#define EADDRINUSE  98
#define EADDRNOTAVAIL 99
#define ENETUNREACH 101
#define ECONNABORTED 103
#define ECONNRESET  104
#define ENOBUFS     105
#define EISCONN     106
#define ENOTCONN    107
#define ETIMEDOUT   110
#define ECONNREFUSED 111
#define EHOSTUNREACH 113
#define EALREADY    114
#define EINPROGRESS 115
