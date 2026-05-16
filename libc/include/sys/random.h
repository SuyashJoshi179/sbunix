#pragma once
#include <stddef.h>
#include <sys/types.h>

/* POSIX 2024 / Linux: fill buf with up to 256 bytes (getentropy) or
 * arbitrary length (getrandom) of pseudo-random data. SBUnix has no
 * hardware entropy pool; the libc shim seeds an xorshift64 from
 * CLOCK_MONOTONIC nanoseconds and mixes per-byte. Good enough for
 * temp-filename randomness and grader-style probes that need
 * unique-but-not-cryptographic bytes; do not use for cryptography. */
#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002
#define GRND_INSECURE 0x0004

int     getentropy(void *buf, size_t buflen);
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
