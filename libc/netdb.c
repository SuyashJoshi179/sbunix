#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>

/* RISC-V is little-endian; network byte order is big-endian, so always swap. */
uint16_t htons(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
uint16_t ntohs(uint16_t x) { return htons(x); }
uint32_t htonl(uint32_t x) {
    return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8)
         | ((x & 0x00ff0000u) >> 8)  | ((x & 0xff000000u) >> 24);
}
uint32_t ntohl(uint32_t x) { return htonl(x); }

/* No networking stack. Resolution always fails. We export the symbols so
 * that ports linking against libresolv-style code at least produce a
 * deterministic failure instead of unresolved-symbol link errors. */

int h_errno = 0;

struct hostent *gethostbyname(const char *name)              { (void)name; h_errno = 1; return 0; }
struct hostent *gethostbyaddr(const void *a, unsigned l, int t){ (void)a; (void)l; (void)t; h_errno = 1; return 0; }
struct servent *getservbyname(const char *n, const char *p)   { (void)n; (void)p; return 0; }
struct servent *getservbyport(int port, const char *p)        { (void)port; (void)p; return 0; }

int getaddrinfo(const char *node, const char *svc,
                const struct addrinfo *hints, struct addrinfo **res) {
    (void)node; (void)svc; (void)hints;
    if (res) *res = 0;
    return EAI_FAIL;
}

void freeaddrinfo(struct addrinfo *res) { (void)res; }

const char *gai_strerror(int err) {
    switch (err) {
    case EAI_NONAME:  return "Name or service not known";
    case EAI_AGAIN:   return "Temporary failure in name resolution";
    case EAI_FAIL:    return "Non-recoverable failure";
    case EAI_FAMILY:  return "ai_family not supported";
    case EAI_SERVICE: return "Service not supported";
    case EAI_MEMORY:  return "Out of memory";
    case EAI_SYSTEM:  return "System error";
    default:          return "Unknown name resolution error";
    }
}
