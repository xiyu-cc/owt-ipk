#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OWT_IPK_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
OWT_OUT_ROOT="$(cd "${OWT_IPK_ROOT}/.." && pwd)/owt-out"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PRESET="${1:-gcc-release}"
OUT_DIR_INPUT="${2:-${OWT_OUT_ROOT}}"
OUT_DIR="$(mkdir -p "${OUT_DIR_INPUT}" && cd "${OUT_DIR_INPUT}" && pwd)"

case "${PRESET}" in
  gcc-release) BUILD_DIR="${OWT_OUT_ROOT}/owt-ctrl/owt-net/build/gcc-release" ;;
  clang-libcxx-release) BUILD_DIR="${OWT_OUT_ROOT}/owt-ctrl/owt-net/build/clang-libcxx-release" ;;
  *)
    echo "unsupported preset: ${PRESET}" >&2
    echo "supported presets: gcc-release, clang-libcxx-release" >&2
    exit 1
    ;;
esac

echo "[clean] Purge old deb artifacts and full-build cache"
find "${OUT_DIR}" -maxdepth 1 -type f -name '*.deb' -delete 2>/dev/null || true
find "${PROJECT_ROOT}" -maxdepth 1 -type f -name '*.deb' -delete 2>/dev/null || true
rm -rf "${BUILD_DIR}"

pushd "${PROJECT_ROOT}" >/dev/null
rm -rf frontend/dist
if [[ ! -d frontend/node_modules ]]; then
  (cd frontend && npm ci)
fi
(cd frontend && npm run build)
cmake --preset "${PRESET}"
cmake --build --preset "${PRESET}" --target owt_net owt_ctrl_tests owt_agent_protocol_tests
ctest --test-dir "${BUILD_DIR}" --output-on-failure
cpack --config "${BUILD_DIR}/CPackConfig.cmake" -G DEB -B "${OUT_DIR}"
popd >/dev/null

mapfile -t FOUND_DEBS < <(
  {
    find "${OUT_DIR}" -maxdepth 1 -type f -name '*.deb' 2>/dev/null
    find "${BUILD_DIR}" -maxdepth 1 -type f -name '*.deb' 2>/dev/null
    find "${SCRIPT_DIR}/../.." -maxdepth 1 -type f -name '*.deb' 2>/dev/null
  } | awk '!seen[$0]++'
)

if [[ ${#FOUND_DEBS[@]} -eq 0 ]]; then
  echo "no deb package found (checked ${OUT_DIR}, ${BUILD_DIR}, project root)" >&2
  exit 2
fi

for src in "${FOUND_DEBS[@]}"; do
  dst="${OUT_DIR}/$(basename "${src}")"
  if [[ "$(cd "$(dirname "${src}")" && pwd)/$(basename "${src}")" == "${dst}" ]]; then
    continue
  fi
  cp -f "${src}" "${dst}"
done

echo "deb package(s):"
ls -1 "${OUT_DIR}"/*.deb
