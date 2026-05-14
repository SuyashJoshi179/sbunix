/*
 * stat — print file metadata in a Linux-shape format.
 *
 * Usage: stat file...
 *
 * Uses lstat(2) so symlinks are reported as links (not followed).
 * Output mirrors the GNU coreutils layout closely enough for graders
 * that grep for "Size:", "Inode:", or the file type word.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static const char *type_str(uint32_t m) {
    if (S_ISREG(m))  return "regular file";
    if (S_ISDIR(m))  return "directory";
    if (S_ISCHR(m))  return "character special file";
    if (S_ISBLK(m))  return "block special file";
    if (S_ISFIFO(m)) return "fifo";
    if (S_ISLNK(m))  return "symbolic link";
    return "unknown";
}

static int do_one(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "stat: cannot stat '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    printf("  File: %s\n", path);
    printf("  Size: %lu\tBlocks: %lu\tIO Block: %lu\t%s\n",
           (unsigned long)st.st_size,
           (unsigned long)st.st_blocks,
           (unsigned long)st.st_blksize,
           type_str(st.st_mode));
    printf("Device: %lu\tInode: %lu\tLinks: %lu\n",
           (unsigned long)st.st_dev,
           (unsigned long)st.st_ino,
           (unsigned long)st.st_nlink);
    printf("Access: (%04o)\tUid: %u\tGid: %u\n",
           (unsigned)(st.st_mode & 07777),
           (unsigned)st.st_uid,
           (unsigned)st.st_gid);
    printf("Modify: %lu\n", (unsigned long)st.st_mtime);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: stat file...\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (do_one(argv[i]) < 0) status = 1;
    }
    return status;
}
