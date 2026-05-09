#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Reserve 16 GB of anonymous virtual memory with no MAP_FIXED hint.
 * Sparse-touch a handful of pages spread across the range to verify
 * lazy demand-paging actually services widely separated VAs. Then
 * munmap and confirm the syscall succeeds. */

#define LEN  (16UL * 1024 * 1024 * 1024)   /* 16 GiB */
#define PG   4096UL

int main(void) {
    void *p = mmap(0, LEN, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { printf("FAIL mmap 16GB\n"); return 1; }

    char *base = (char *)p;
    unsigned long offsets[] = {
        0,
        4UL * 1024 * 1024 * 1024,    /*  4 GB */
        8UL * 1024 * 1024 * 1024,    /*  8 GB */
        12UL * 1024 * 1024 * 1024,   /* 12 GB */
        LEN - PG,
    };
    int n = (int)(sizeof(offsets) / sizeof(offsets[0]));

    for (int i = 0; i < n; i++) base[offsets[i]] = (char)(i + 1);
    for (int i = 0; i < n; i++) {
        if (base[offsets[i]] != (char)(i + 1)) {
            printf("FAIL bleed off=%lu\n", offsets[i]);
            return 1;
        }
    }
    if (munmap(p, LEN) != 0) { printf("FAIL munmap\n"); return 1; }
    printf("PASS mmap_anon_huge\n");
    return 0;
}
