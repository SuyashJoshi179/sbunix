#include <stdio.h>
#include <stdlib.h>

#define SZ (256UL * 1024 * 1024)
#define PG 4096UL

int main(void) {
    char *p = (char *)malloc(SZ);
    if (!p) { printf("FAIL malloc 256MiB\n"); return 1; }
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
    printf("PASS pattern_write 256MiB\n");
    return 0;
}
