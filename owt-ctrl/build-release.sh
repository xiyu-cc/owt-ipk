#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OWT_IPK_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OPENWRT_ROOT="$(cd "${OWT_IPK_ROOT}/.." && pwd)"
OWT_OUT_ROOT="$(cd "${OWT_IPK_ROOT}/.." && pwd)/owt-out"

mkdir -p "${OWT_OUT_ROOT}"

if [[ -n "${OPENWRT_SDK_DIR:-}" ]]; then
  SDK_DIR="${OPENWRT_SDK_DIR}"
else
  SDK_DIR=""
  for candidate in "${OPENWRT_ROOT}"/openwrt-sdk-*; do
    if [[ -d "${candidate}" ]]; then
      SDK_DIR="${candidate}"
      break
    fi
  done
fi

if [[ -z "${SDK_DIR}" || ! -d "${SDK_DIR}" ]]; then
  echo "OpenWrt SDK not found. Set OPENWRT_SDK_DIR explicitly." >&2
  exit 1
fi

if [[ -z "${OWT_AGENT_TP_SOURCE:-}" ]]; then
  echo "OWT_AGENT_TP_SOURCE is required (point it to the owt third_party directory)." >&2
  exit 4
fi
if [[ ! -d "${OWT_AGENT_TP_SOURCE}/spdlog" || ! -d "${OWT_AGENT_TP_SOURCE}/nlohmann" || ! -d "${OWT_AGENT_TP_SOURCE}/jsoncpp" ]]; then
  echo "OWT_AGENT_TP_SOURCE='${OWT_AGENT_TP_SOURCE}' is missing required third_party components." >&2
  exit 4
fi

echo "[clean] Purge old release artifacts -> ${OWT_OUT_ROOT}"
find "${OWT_OUT_ROOT}" -maxdepth 1 -type f \( -name '*.deb' -o -name 'owt-agent*.ipk' \) -delete 2>/dev/null || true

echo "[1/2] Build deb release -> ${OWT_OUT_ROOT}"
"${SCRIPT_DIR}/owt-net/deploy/deb/build-deb.sh" gcc-release "${OWT_OUT_ROOT}"

echo "[2/2] Build ipk release (owt-agent) -> ${OWT_OUT_ROOT}"
if [[ -d "${SDK_DIR}/package/feeds/owt-ctrl/owt-agent" ]]; then
  OWT_AGENT_PKG_DIR="${SDK_DIR}/package/feeds/owt-ctrl/owt-agent"
  OWT_AGENT_BUILD_TARGET="package/feeds/owt-ctrl/owt-agent/compile"
  OWT_AGENT_CLEAN_TARGET="package/feeds/owt-ctrl/owt-agent/clean"
elif [[ -d "${SDK_DIR}/package/owt-agent" ]]; then
  OWT_AGENT_PKG_DIR="${SDK_DIR}/package/owt-agent"
  OWT_AGENT_BUILD_TARGET="package/owt-agent/compile"
  OWT_AGENT_CLEAN_TARGET="package/owt-agent/clean"
else
  echo "owt-agent package not found in SDK. Expected one of:" >&2
  echo "  ${SDK_DIR}/package/feeds/owt-ctrl/owt-agent" >&2
  echo "  ${SDK_DIR}/package/owt-agent" >&2
  echo "Please install/link owt-agent package first." >&2
  exit 3
fi

# Always sync package definition from repo to avoid stale SDK copies
# (e.g. old params.ini/sqlite settings being packed into ipk).
OWT_AGENT_REPO_PKG_DIR="${SCRIPT_DIR}/owt-agent/application/owt-agent"
OWT_AGENT_REPO_ROOT="${SCRIPT_DIR}/owt-agent"
cp -f "${OWT_AGENT_REPO_PKG_DIR}/Makefile" "${OWT_AGENT_PKG_DIR}/Makefile"
cp -f "${OWT_AGENT_REPO_PKG_DIR}/CMakeLists.txt" "${OWT_AGENT_PKG_DIR}/CMakeLists.txt"
rsync -a --delete "${OWT_AGENT_REPO_PKG_DIR}/cmake/" "${OWT_AGENT_PKG_DIR}/cmake/"
mkdir -p "${OWT_AGENT_PKG_DIR}/files"
rsync -a --delete "${OWT_AGENT_REPO_PKG_DIR}/files/" "${OWT_AGENT_PKG_DIR}/files/"

# Ensure SDK source/header entrypoints always point to current repo tree.
ln -snf "${OWT_AGENT_REPO_ROOT}/src" "${SDK_DIR}/src"
mkdir -p "${SDK_DIR}/include"
ln -snf "${OWT_AGENT_REPO_ROOT}/include" "${SDK_DIR}/include/include"

# Remove stale ipk outputs so a skipped compile cannot silently reuse old artifacts.
find "${SDK_DIR}/bin/packages" -type f -name 'owt-agent*.ipk' -delete 2>/dev/null || true
find "${OWT_OUT_ROOT}" -maxdepth 1 -type f -name 'owt-agent*.ipk' -delete 2>/dev/null || true

echo "[clean] Force OpenWrt package clean target: ${OWT_AGENT_CLEAN_TARGET}"
make -C "${SDK_DIR}" OWT_AGENT_TP_SOURCE="${OWT_AGENT_TP_SOURCE}" "${OWT_AGENT_CLEAN_TARGET}" -j"$(nproc)"

make -C "${SDK_DIR}" OWT_AGENT_TP_SOURCE="${OWT_AGENT_TP_SOURCE}" "${OWT_AGENT_BUILD_TARGET}" -j"$(nproc)"

mapfile -t IPK_FILES < <(find "${SDK_DIR}/bin/packages" -type f -name 'owt-agent*.ipk' | sort)
if [[ ${#IPK_FILES[@]} -eq 0 ]]; then
  echo "No owt-agent ipk found under ${SDK_DIR}/bin/packages" >&2
  exit 2
fi

cp -f "${IPK_FILES[@]}" "${OWT_OUT_ROOT}/"

echo "Done. Output artifacts:"
echo "  out: ${OWT_OUT_ROOT}"
ls -1 "${OWT_OUT_ROOT}"/*.deb || true
ls -1 "${OWT_OUT_ROOT}"/owt-agent*.ipk || true
