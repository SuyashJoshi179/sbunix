#include <stdio.h>
#include <stdlib.h>

/* Sized to fit comfortably under typical physical RAM (~128 MB minus
 * kernel + already-resident allocations). 64 MB exercises the lazy
 * mmap-arena path with thousands of demand-paged 4 KB writes. */
#define SZ (64UL * 1024 * 1024)
#define PG 4096UL

int main(void) {
    char *p = (char *)malloc(SZ);
    if (!p) { printf("FAIL malloc %lu MiB\n", SZ >> 20); return 1; }
    for (unsigned long off = 0; off < SZ; off += PG)
        p[off] = (char)((off / PG) & 0xFF);
    for (unsigned long off = 0; off < SZ; off += PG) {
        char want = (char)((off / PG) & 0xFF);
        if (p[off] != want) {
            printf("FAIL off=%lu got=%d want=%d\n",
                   off, (int)p[off], (int)want);
            free(p); return 1;
        }
    }
    free(p);
    printf("PASS pattern_write %luMiB\n", SZ >> 20);
    return 0;
}
