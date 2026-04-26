#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <strace-bin> <test-binary>" >&2
  exit 2
fi

STRACE_BIN="$1"
TEST_BIN="$2"

if [ ! -x "$STRACE_BIN" ]; then
  echo "strace not executable: $STRACE_BIN" >&2
  exit 2
fi
if [ ! -x "$TEST_BIN" ]; then
  echo "test binary not executable: $TEST_BIN" >&2
  exit 2
fi

TMP_BASE="$(mktemp -t owt-agent-no-file-write.XXXXXX)"
TMP_LOG="${TMP_BASE}.log"
trap 'rm -f "$TMP_BASE" "$TMP_BASE".* "$TMP_LOG"' EXIT

set +e
"$STRACE_BIN" -f -qq \
  -e trace=open,openat,openat2,creat,rename,renameat,renameat2,unlink,unlinkat,mkdir,mkdirat,rmdir,truncate,ftruncate,link,linkat,symlink,symlinkat \
  -o "$TMP_BASE" \
  "$TEST_BIN" >"$TMP_LOG" 2>&1
TEST_RC=$?
set -e

if [ "$TEST_RC" -ne 0 ]; then
  echo "runtime probe target failed: $TEST_BIN (exit=$TEST_RC)" >&2
  cat "$TMP_LOG" >&2
  exit "$TEST_RC"
fi

STRACE_FILES=""
for f in "$TMP_BASE" "$TMP_BASE".*; do
  if [ -f "$f" ]; then
    STRACE_FILES="$STRACE_FILES $f"
  fi
done

if [ -z "$STRACE_FILES" ]; then
  echo "no strace output generated" >&2
  exit 1
fi

if grep -HnE '^(open|openat|openat2)\(.*(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND)' $STRACE_FILES; then
  echo "no-file-write runtime check failed: writable open detected" >&2
  exit 1
fi

if grep -HnE '^(creat|rename|renameat|renameat2|unlink|unlinkat|mkdir|mkdirat|rmdir|truncate|ftruncate|link|linkat|symlink|symlinkat)\(' $STRACE_FILES; then
  echo "no-file-write runtime check failed: mutating fs syscall detected" >&2
  exit 1
fi

echo "no-file-write runtime check passed"
