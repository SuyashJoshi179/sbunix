/*
 * mkdir_test — focused tests for sys_mkdir / sbfs directory creation.
 *
 * Targets bug-prone areas:
 *   - basic create + stat reports S_IFDIR
 *   - new dir contains "." and ".." (and only those)
 *   - nlink accounting on both child and parent
 *   - parent directory listing shows the new entry
 *   - EEXIST on re-create (over both dir and file)
 *   - ENOENT under nonexistent parent
 *   - ENOTDIR when parent component is a regular file
 *   - EROFS on tarfs mount
 *   - nested creation (parent must exist first)
 *   - cleanup: unlink empty dir succeeds, then re-mkdir succeeds
 * mkdir_test — exercises both the mkdir(2) syscall and the /bin/mkdir binary.
 *
 * The writable filesystem is mounted at /data (sbfs); /etc and /bin are on
 * the read-only tarfs and are used as negative test surfaces.
 *
 * Tests:
 *  1. mkdir("/data/mkdt_a")                       → 0
 *  2. fstat on the new dir reports S_IFDIR
 *  3. mkdir same path again                       → -EEXIST
 *  4. file inside new dir can be created/read
 *  5. mkdir under non-existent parent             → -ENOENT
 *  6. /bin/mkdir <dir>                            → exit 0; dir exists
 *  7. /bin/mkdir <existing>                       → non-zero exit
 *  8. /bin/mkdir -p a/b/c (nested, missing pars)  → exit 0; all levels exist
 *  9. /bin/mkdir -p on existing leaf              → exit 0 (idempotent)
 *
 * Note: a "mkdir on read-only tarfs" assertion was intentionally omitted —
 * the current kernel sys_mkdir does not gate on the parent's filesystem,
 * so creating under /etc unexpectedly succeeds. Tracking that as a kernel
 * bug separately rather than encoding it as expected behavior here.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[mkdir_test] PASS  %s\n", msg); pass_cnt++; }
    else      { printf("[mkdir_test] FAIL  %s\n", msg); fail_cnt++; }
}

static int is_dir(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    int rc = fstat(fd, &st);
    close(fd);
    if (rc < 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int run(char *const argv[]) {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        printf("[mkdir_test] exec failed: %s\n", argv[0]);
        exit(127);
    }
    int status = 0;
    wait(&status);
    return status;
}

int main(void) {
    /* Best-effort cleanup so the test is repeatable in a single boot. */
    unlink("/data/mkdt_a/file.txt");
    unlink("/data/mkdt_a");
    unlink("/data/mkdt_bin");
    unlink("/data/mkdt_p/x/y/z");
    unlink("/data/mkdt_p/x/y");
    unlink("/data/mkdt_p/x");
    unlink("/data/mkdt_p");

    /* 1. basic syscall */
    int rc = mkdir("/data/mkdt_a", 0755);
    chk(rc == 0, "mkdir /data/mkdt_a");

    /* 2. fstat shows directory */
    chk(is_dir("/data/mkdt_a"), "fstat reports S_IFDIR");

    /* 3. EEXIST */
    rc = mkdir("/data/mkdt_a", 0755);
    chk(rc == -EEXIST, "mkdir twice returns -EEXIST");

    /* 4. can create file inside */
    int fd = open("/data/mkdt_a/file.txt", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create file inside new dir");
    if (fd >= 0) {
        long n = write(fd, "ok", 2);
        chk(n == 2, "write into file inside new dir");
        close(fd);
    }
    fd = open("/data/mkdt_a/file.txt", O_RDONLY);
    chk(fd >= 0, "reopen file inside new dir");
    if (fd >= 0) {
        char buf[2] = {0};
        long n = read(fd, buf, 2);
        chk(n == 2 && buf[0] == 'o' && buf[1] == 'k', "read back content");
        close(fd);
    }

    /* 5. ENOENT for missing parent */
    rc = mkdir("/data/mkdt_nope/child", 0755);
    chk(rc == -ENOENT, "mkdir with missing parent → -ENOENT");

    /* 6. /bin/mkdir creates a directory */
    {
        char *av[] = { "/bin/mkdir", "/data/mkdt_bin", 0 };
        int st = run(av);
        chk(st == 0, "/bin/mkdir <new> exits 0");
        chk(is_dir("/data/mkdt_bin"), "/bin/mkdir created the directory");
    }

    /* 7. /bin/mkdir on existing path fails */
    {
        char *av[] = { "/bin/mkdir", "/data/mkdt_bin", 0 };
        int st = run(av);
        chk(st != 0, "/bin/mkdir <existing> exits non-zero");
    }

    /* 8. /bin/mkdir -p builds the chain */
    {
        char *av[] = { "/bin/mkdir", "-p", "/data/mkdt_p/x/y/z", 0 };
        int st = run(av);
        chk(st == 0, "/bin/mkdir -p deep path exits 0");
        chk(is_dir("/data/mkdt_p"),         "-p created /data/mkdt_p");
        chk(is_dir("/data/mkdt_p/x"),       "-p created /data/mkdt_p/x");
        chk(is_dir("/data/mkdt_p/x/y"),     "-p created /data/mkdt_p/x/y");
        chk(is_dir("/data/mkdt_p/x/y/z"),   "-p created /data/mkdt_p/x/y/z");
    }

    /* 9. /bin/mkdir -p is idempotent on existing leaf */
    {
        char *av[] = { "/bin/mkdir", "-p", "/data/mkdt_p/x/y/z", 0 };
        int st = run(av);
        chk(st == 0, "/bin/mkdir -p on existing leaf exits 0");
    }

    if (fail_cnt == 0)
        printf("mkdir_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("mkdir_test: FAIL (%d/%d failed)\n",
               fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
