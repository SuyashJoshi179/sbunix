#include <stdio.h>

// Large .bss array. POSIX requires every byte to read as zero at process
// start (memsz > filesz on the data segment). This is what we verify.
static int big[4096];

// A small .bss tail to catch the partial-page case (memsz tail inside the
// last file-backed page).
static char tail[37];

int main(void) {
    int nonzero = 0;
    for (int i = 0; i < 4096; i++)
        if (big[i] != 0) nonzero++;
    for (int i = 0; i < 37; i++)
        if (tail[i] != 0) nonzero++;

    printf("bss nonzero=%d (expect 0)\n", nonzero);
    printf("big[0]=%d big[4095]=%d tail[36]=%d (expect 0 0 0)\n",
           big[0], big[4095], tail[36]);
    return nonzero;
}
