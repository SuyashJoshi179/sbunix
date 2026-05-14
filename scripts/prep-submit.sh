#!/usr/bin/env bash
# prep-submit.sh — strip test artifacts before `make submit`.
#
# Workflow:
#   1. Cut a release branch off develop:    git checkout -b release/YYYY-MM-DD develop
#   2. Run this script:                     ./scripts/prep-submit.sh
#   3. Inspect the result:                  git status && git diff
#   4. Commit:                              git add -A && git commit -m "submission prep"
#   5. Submit:                              make submit
#
# The script refuses to run on develop/master/main, refuses to run with a
# dirty working tree, and runs a sanity build at the end so a broken strip
# fails loudly instead of silently producing a non-bootable submission.

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Production utilities — kept in the submission. Everything else under bin/
# is treated as a test and removed.
#
# If you add a new production binary, add it here. If you add a new test,
# you don't need to do anything — it will be auto-stripped.
PROD_BINS=(
    cat
    cp
    date
    echo
    false
    init
    ln
    ls
    mkdir
    mount
    mv
    ps
    rm
    sh
    sleep
    test
    true
    wc
)

# ---------------------------------------------------------------------------
# Safety checks
# ---------------------------------------------------------------------------

cd "$(git rev-parse --show-toplevel)"

BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null || echo "<detached>")
case "$BRANCH" in
    develop|master|main|"<detached>")
        echo "ERROR: refusing to strip on '$BRANCH'." >&2
        echo "       Cut a release branch first:" >&2
        echo "         git checkout -b release/\$(date +%F) develop" >&2
        exit 1
        ;;
esac

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "ERROR: working tree has uncommitted changes." >&2
    echo "       Commit or stash first so 'git reset --hard' can recover" >&2
    echo "       if the strip goes wrong." >&2
    exit 1
fi

if [ -n "$(git ls-files --others --exclude-standard)" ]; then
    echo "ERROR: untracked files present:" >&2
    git ls-files --others --exclude-standard | sed 's/^/         /' >&2
    echo "       Commit, stash, remove, or .gitignore them first." >&2
    exit 1
fi

BASE_SHA=$(git rev-parse HEAD)
echo "==> Stripping branch '$BRANCH' (base $BASE_SHA)"
echo "    Recover with: git reset --hard $BASE_SHA"
echo

# ---------------------------------------------------------------------------
# Strip
# ---------------------------------------------------------------------------

echo "==> Removing docs/, thirdparty/, third_party/"
rm -rf docs thirdparty third_party

echo "==> Removing test binaries from bin/ (whitelist of ${#PROD_BINS[@]} production utilities)"
removed=0
kept=0
for d in bin/*/; do
    name=$(basename "$d")
    keep=0
    for p in "${PROD_BINS[@]}"; do
        if [ "$name" = "$p" ]; then keep=1; break; fi
    done
    if [ "$keep" = "1" ]; then
        kept=$((kept + 1))
    else
        rm -rf "$d"
        removed=$((removed + 1))
    fi
done
echo "    kept $kept, removed $removed"

echo "==> Removing kernel selftest files"
rm -f kernel/selftest.c kernel/include/selftest.h

echo "==> Patching kernel/kernel.c (drop selftest include + call)"
# Idempotent: re-running on an already-stripped tree is a no-op.
sed -i '/^#include <selftest.h>$/d' kernel/kernel.c
sed -i '/^    selftest_run();$/d' kernel/kernel.c

echo "==> Replacing bin/init/init.c with the minimal init"
# This init does exactly two things: run /etc/rc once, then loop /bin/sh.
# It matches the structure of the audited init.c but without the in-tree
# test-suite execution that we just stripped from bin/.
cat > bin/init/init.c <<'INIT_EOF'
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

int main(void) {
    printf("init: starting\n");

    setsid();
    int shell_pgid = getpgrp();
    ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

    /* Run /etc/rc once at boot to mount /proc, /mnt, /tmp. */
    {
        int rc_pid = fork();
        if (rc_pid == 0) {
            char *args[] = {"/bin/sh", "/etc/rc", 0};
            execv("/bin/sh", args);
            printf("init: exec /bin/sh /etc/rc failed\n");
            exit(1);
        }
        if (rc_pid > 0) {
            int rc_st;
            while (1) {
                int got = wait(&rc_st);
                if (got == rc_pid) break;
                if (got < 0 && errno != EINTR) break;
            }
        }
    }

    int sh_failures = 0;
    while (1) {
        while (waitpid(-1, 0, WNOHANG) > 0) { }

        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            char *sh_args[] = { "/bin/sh", 0 };
            execv("/bin/sh", sh_args);
            printf("init: exec /bin/sh failed\n");
            exit(127);
        }
        setpgid(pid, pid);
        ioctl(0, 0x5410 /* TIOCSPGRP */, &pid);
        int sh_status = 0;
        while (1) {
            int got = wait(&sh_status);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) break;
        }
        ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

        if (sh_status == 127) {
            sh_failures++;
            if (sh_failures >= 5) {
                printf("init: /bin/sh fails repeatedly; sleeping 5s\n");
                sleep(5);
                sh_failures = 0;
            }
        } else {
            sh_failures = 0;
        }
    }
}
INIT_EOF

# ---------------------------------------------------------------------------
# Sanity build
# ---------------------------------------------------------------------------

echo
echo "==> Sanity build (make clean && make)"
make clean >/dev/null 2>&1 || true
if ! make >/tmp/prep-submit-build.log 2>&1; then
    echo "ERROR: build failed after strip. Tail of build log:" >&2
    tail -40 /tmp/prep-submit-build.log >&2
    echo
    echo "Recover with: git reset --hard $BASE_SHA" >&2
    exit 1
fi
echo "    build OK"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo
echo "==> Strip complete."
echo
echo "Next steps:"
echo "    git status                     # see what changed"
echo "    git diff --stat                # quick summary"
echo "    make qemu                      # smoke-test the stripped tree boots"
echo "    git add -A && git commit -m 'submission prep'"
echo "    make submit"
echo
echo "To undo and start over:"
echo "    git reset --hard $BASE_SHA"
