/* partial_munmap_test (T2.13/T2.14): partial munmap of a file-backed
 * VMA must split the mapping, propagating file/file_off into the new
 * right half. Previously the kernel returned -EINVAL for any non-whole
 * unmap of a VMA_TYPE_FILE; the audit also flagged the latent NPE
 * because vma_split's middle-cut left right->file == NULL. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

#define PG 4096

int main(void) {
    int fd = open("/bin/init", O_RDONLY);
    if (fd < 0) { printf("open /bin/init failed\n"); return 1; }
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 4 * PG) {
        printf("init too small (%ld); skipping test\n", (long)st.st_size);
        close(fd); return 0;
    }

    /* Middle-cut: map 4 pages, unmap the middle 2. The outer pages must
     * stay readable and still hold their original bytes. */
    char *p = mmap(0, 4 * PG, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(p != MAP_FAILED, "mmap 4 pages of /bin/init");
    if (p == MAP_FAILED) { close(fd); return 1; }
    volatile char b0 = p[0];
    volatile char b3 = p[3 * PG];

    int rc = munmap(p + PG, 2 * PG);
    CHECK(rc == 0, "middle-cut munmap returns 0");

    /* If the unmap succeeded, the right-half VMA must carry file/file_off,
     * so re-reading p[3*PG] returns the original byte instead of NPE'ing
     * on a fault into a file-less file VMA. */
    CHECK(p[0]      == b0, "head page still readable after middle cut");
    CHECK(p[3 * PG] == b3, "tail page still readable after middle cut");

    /* Clean up: unmap the two surviving pages individually. */
    munmap(p, PG);
    munmap(p + 3 * PG, PG);

    /* Head-trim: map 4 pages, unmap the first 2. The trailing pages must
     * still read correctly — file_off advanced. */
    p = mmap(0, 4 * PG, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(p != MAP_FAILED, "mmap again for head-trim");
    b3 = p[3 * PG];
    rc = munmap(p, 2 * PG);
    CHECK(rc == 0, "head-trim munmap returns 0");
    CHECK(p[3 * PG] == b3, "tail page survives head-trim");
    munmap(p + 2 * PG, 2 * PG);

    /* Tail-trim: map 4 pages, unmap the last 2. */
    p = mmap(0, 4 * PG, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(p != MAP_FAILED, "mmap again for tail-trim");
    b0 = p[0];
    rc = munmap(p + 2 * PG, 2 * PG);
    CHECK(rc == 0, "tail-trim munmap returns 0");
    CHECK(p[0] == b0, "head page survives tail-trim");
    munmap(p, 2 * PG);

    close(fd);

    if (fails == 0) printf("partial_munmap_test: PASS\n");
    else printf("partial_munmap_test: %d FAIL(s)\n", fails);
    return fails;
}
