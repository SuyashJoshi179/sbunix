#!/usr/bin/env bash
# cluster-errors.sh — bucket build errors from results.txt by regex.
#
# Reads results.txt on stdin or as $1, emits a two-section markdown
# report on stdout:
#   ## Fix         — clusters we should close in libc/kernel
#   ## Out-of-scope — clusters we explicitly will not address
#
# Add/edit clusters by appending to the BUCKETS arrays below.
# Each entry: pattern|label
# Patterns are extended regex matched against the full results.txt line.

set -euo pipefail
in=${1:-/dev/stdin}

# Fix clusters: things that look like real gaps in libc/kernel.
declare -a FIX_BUCKETS=(
  "sys/time\.h: No such file|missing header sys/time.h"
  "sys/mman\.h: No such file|missing header sys/mman.h"
  "dirent\.h: No such file|missing header dirent.h"
  "implicit declaration of function 'strdup'|missing decl strdup"
  "implicit declaration of function 'malloc'|missing decl malloc"
  "implicit declaration of function 'sigaction'|missing decl sigaction"
  "'sigset_t' undeclared|missing type sigset_t"
  "'CLOCK_[A-Z_]+' undeclared|missing CLOCK_* constants"
  "'SA_[A-Z_]+' undeclared|missing SA_* sigaction flags"
  "'_Noreturn' has not been declared|missing _Noreturn"
  "implicit declaration of function 'getpid'|missing decl getpid"
  "implicit declaration of function 'chdir'|missing decl chdir"
  "implicit declaration of function 'getcwd'|missing decl getcwd"
  "undefined reference to .clock_settime.|missing impl clock_settime"
  "undefined reference to .sigwaitinfo.|missing impl sigwaitinfo"
  "undefined reference to .sigwait.|missing impl sigwait"
  "undefined reference to .sigpending.|missing impl sigpending"
  "undefined reference to .sighold.|missing impl sighold"
  "undefined reference to .sigrelse.|missing impl sigrelse"
  "undefined reference to .sigignore.|missing impl sigignore"
)

# Out-of-scope clusters: things we explicitly will not address.
declare -a OOS_BUCKETS=(
  "pthread\.h: No such file|pthread (no threading)"
  "aio\.h: No such file|AIO"
  "mqueue\.h: No such file|message queues"
  "implicit declaration of function 'pthread_|pthread (no threading)"
  "implicit declaration of function 'aio_|AIO"
  "implicit declaration of function 'mq_|message queues"
  "implicit declaration of function 'sched_|realtime scheduling"
  "implicit declaration of function 'clock_nanosleep'|realtime timers"
  "implicit declaration of function 'sem_|POSIX semaphores"
  "'pthread_[a-zA-Z_]+' undeclared|pthread (no threading)"
  "'PTHREAD_[A-Z_]+' undeclared|pthread (no threading)"
  "undefined reference to .main.|helper-only TU (no main() — link gate noise, not a libc gap)"
)

count_bucket() {
  # awk avoids the grep -c quirk where "no match" still prints "0" and
  # returns 1; under set -e that's a footgun. awk always succeeds and
  # always prints exactly one integer.
  local pat=$1
  awk -v p="$pat" '$0 ~ p { c++ } END { print c+0 }' "$in"
}

emit_section() {
  local title=$1; shift
  local -a buckets=("$@")
  echo "## $title"
  echo
  local label count
  declare -a rows=()
  for entry in "${buckets[@]}"; do
    pat=${entry%%|*}
    label=${entry#*|}
    count=$(count_bucket "$pat")
    if [ "$count" -gt 0 ]; then
      rows+=("$count|$label")
    fi
  done
  if [ ${#rows[@]} -eq 0 ]; then
    echo "_(no clusters matched)_"
    echo
    return
  fi
  printf '%s\n' "${rows[@]}" | sort -t '|' -k1,1 -nr | \
    awk -F '|' 'BEGIN{print "| TUs | Cluster |"; print "|----:|---------|"} {printf "| %d | %s |\n", $1, $2}'
  echo
}

total=$(wc -l < "$in")
ok=$(awk '/^BUILD-OK/ { c++ } END { print c+0 }' "$in")
bf=$(awk '/^BUILD-FAIL/ { c++ } END { print c+0 }' "$in")
lf=$(awk '/^LINK-FAIL/ { c++ } END { print c+0 }' "$in")

cat <<EOF
# OPTS gap-list — generated $(date -u +%Y-%m-%dT%H:%M:%SZ)

**Summary.** Total TUs: $total — BUILD-OK $ok, BUILD-FAIL $bf, LINK-FAIL $lf.

EOF

emit_section "Fix" "${FIX_BUCKETS[@]}"
emit_section "Out-of-scope" "${OOS_BUCKETS[@]}"

cat <<EOF
---

_Edit \`thirdparty/open-posix/scripts/cluster-errors.sh\` to add new
buckets as you discover them. Counts only reflect clusters whose
pattern matched at least once; everything else is currently un-bucketed
and lives in \`build/results.txt\`._
EOF
