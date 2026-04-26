#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <owt-agent-root>" >&2
  exit 2
fi

ROOT="$1"
SRC_DIR="$ROOT/src"
INCLUDE_DIR="$ROOT/include"

if [ ! -d "$SRC_DIR" ] || [ ! -d "$INCLUDE_DIR" ]; then
  echo "invalid owt-agent root: $ROOT" >&2
  exit 2
fi

PATTERN='std::ofstream|std::fstream|fopen[[:space:]]*\(|freopen[[:space:]]*\(|openat2?[[:space:]]*\(|creat[[:space:]]*\(|pwrite[[:space:]]*\(|fwrite[[:space:]]*\(|fprintf[[:space:]]*\(|rename(at2?)?[[:space:]]*\(|unlink(at)?[[:space:]]*\(|remove[[:space:]]*\(|mkdir(at)?[[:space:]]*\(|rmdir[[:space:]]*\(|truncate[[:space:]]*\(|ftruncate[[:space:]]*\(|mkstemp[[:space:]]*\(|tmpfile[[:space:]]*\(|std::filesystem|boost::filesystem|spdlog/sinks/basic_file_sink\.h|spdlog/sinks/rotating_file_sink\.h|spdlog/sinks/daily_file_sink\.h|basic_logger_(mt|st)[[:space:]]*\(|rotating_logger_(mt|st)[[:space:]]*\(|daily_logger_(mt|st)[[:space:]]*\('

if command -v rg >/dev/null 2>&1; then
  if rg -n -S -e "$PATTERN" "$SRC_DIR" "$INCLUDE_DIR"; then
    echo "no-file-write static check failed: forbidden patterns found" >&2
    exit 1
  fi
else
  if grep -RInE "$PATTERN" "$SRC_DIR" "$INCLUDE_DIR"; then
    echo "no-file-write static check failed: forbidden patterns found" >&2
    exit 1
  fi
fi

echo "no-file-write static check passed"
