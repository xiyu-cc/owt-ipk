#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OWT_IPK_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
OWT_OUT_ROOT="$(cd "${OWT_IPK_ROOT}/.." && pwd)/owt-out"

PRESET="${1:-gcc-release}"
OUT_DIR_INPUT="${2:-${OWT_OUT_ROOT}}"
OUT_DIR="$(mkdir -p "${OUT_DIR_INPUT}" && cd "${OUT_DIR_INPUT}" && pwd)"

normalize_oauth2_proxy_arch() {
  local raw="${1}"
  case "${raw}" in
    linux-amd64|amd64|x86_64) echo "linux-amd64" ;;
    linux-arm64|arm64|aarch64) echo "linux-arm64" ;;
    *)
      echo "unsupported oauth2-proxy arch: ${raw}" >&2
      echo "supported: linux-amd64, linux-arm64" >&2
      return 1
      ;;
  esac
}

resolve_deb_arch() {
  if command -v dpkg >/dev/null 2>&1; then
    dpkg --print-architecture
    return
  fi

  case "$(uname -m)" in
    x86_64) echo "amd64" ;;
    aarch64|arm64) echo "arm64" ;;
    *)
      echo "unsupported host arch: $(uname -m)" >&2
      return 1
      ;;
  esac
}

fetch_oauth2_proxy_binary() {
  local version_input="${OAUTH2_PROXY_VERSION:-latest}"
  local arch_input="${OAUTH2_PROXY_ARCH:-$(resolve_deb_arch)}"
  local arch
  arch="$(normalize_oauth2_proxy_arch "${arch_input}")"

  local version="${version_input}"
  if [[ "${version}" == "latest" ]]; then
    version="$(curl -fsSL https://api.github.com/repos/oauth2-proxy/oauth2-proxy/releases/latest | awk -F'"' '/"tag_name"[[:space:]]*:/ {tag=$4} END {print tag}')"
    if [[ -z "${version}" ]]; then
      echo "failed to resolve latest oauth2-proxy release" >&2
      return 1
    fi
  fi

  if [[ "${version}" != v* ]]; then
    version="v${version}"
  fi

  local tarball="oauth2-proxy-${version}.${arch}.tar.gz"
  local base_url="https://github.com/oauth2-proxy/oauth2-proxy/releases/download/${version}"
  local vendor_root="${OWT_OUT_ROOT}/owt-ctrl/owt-net/vendor/oauth2-proxy/${version}/${arch}"
  local tarball_path="${vendor_root}/${tarball}"
  local checksum_file="${tarball}-sha256sum.txt"
  local checksum_path="${vendor_root}/${checksum_file}"
  local checksums_legacy_path="${vendor_root}/checksums.txt"
  local extract_root="${vendor_root}/extract"
  local bundled_binary="${vendor_root}/oauth2-proxy"

  mkdir -p "${vendor_root}"

  if [[ ! -f "${tarball_path}" ]]; then
    curl -fL --retry 3 --retry-delay 1 -o "${tarball_path}" "${base_url}/${tarball}"
  fi
  if [[ ! -f "${checksum_path}" ]]; then
    curl -fL --retry 3 --retry-delay 1 -o "${checksum_path}" "${base_url}/${checksum_file}" \
      || curl -fL --retry 3 --retry-delay 1 -o "${checksums_legacy_path}" "${base_url}/checksums.txt"
  fi

  if [[ -f "${checksum_path}" ]]; then
    (cd "${vendor_root}" && sha256sum -c "${checksum_file}" >/dev/null)
  else
    local expected_sha
    expected_sha="$(awk -v name="${tarball}" '$2 == name {print $1; exit}' "${checksums_legacy_path}")"
    if [[ -z "${expected_sha}" ]]; then
      echo "missing checksum for ${tarball} in ${checksums_legacy_path}" >&2
      return 1
    fi
    printf '%s  %s\n' "${expected_sha}" "${tarball_path}" | sha256sum -c - >/dev/null
  fi

  rm -rf "${extract_root}"
  mkdir -p "${extract_root}"
  tar -xzf "${tarball_path}" -C "${extract_root}"

  local extracted_bin="${extract_root}/oauth2-proxy-${version}.${arch}/oauth2-proxy"
  if [[ ! -f "${extracted_bin}" ]]; then
    extracted_bin="$(find "${extract_root}" -type f -name oauth2-proxy | head -n 1)"
  fi

  if [[ -z "${extracted_bin}" || ! -f "${extracted_bin}" ]]; then
    echo "oauth2-proxy binary not found after extraction" >&2
    return 1
  fi

  install -m 0755 "${extracted_bin}" "${bundled_binary}"
  printf '%s\n' "${bundled_binary}"
}

case "${PRESET}" in
  gcc-release) BUILD_DIR="${OWT_OUT_ROOT}/owt-ctrl/owt-net/build/gcc-release" ;;
  gcc-debug) BUILD_DIR="${OWT_OUT_ROOT}/owt-ctrl/owt-net/build/gcc-debug" ;;
  clang-libcxx-release) BUILD_DIR="${OWT_OUT_ROOT}/owt-ctrl/owt-net/build/clang-libcxx-release" ;;
  clang-libcxx-debug) BUILD_DIR="${OWT_OUT_ROOT}/owt-ctrl/owt-net/build/clang-libcxx-debug" ;;
  *)
    echo "unsupported preset: ${PRESET}" >&2
    echo "supported presets: gcc-debug, gcc-release, clang-libcxx-debug, clang-libcxx-release" >&2
    exit 1
    ;;
esac

OAUTH2_PROXY_BIN="$(fetch_oauth2_proxy_binary)"
echo "using oauth2-proxy binary: ${OAUTH2_PROXY_BIN}"

pushd "${SCRIPT_DIR}/../.." >/dev/null
if [[ ! -d frontend/node_modules ]]; then
  (cd frontend && npm ci)
fi
(cd frontend && npm run build)
cmake --preset "${PRESET}" -DOWT_OAUTH2_PROXY_BINARY="${OAUTH2_PROXY_BIN}"
cmake --build --preset "${PRESET}" --target owt_net
cpack --config "${BUILD_DIR}/CPackConfig.cmake" -G DEB -B "${OUT_DIR}"
popd >/dev/null

mapfile -t FOUND_DEBS < <(
  {
    find "${BUILD_DIR}" -maxdepth 1 -type f -name '*.deb' 2>/dev/null
    find "${SCRIPT_DIR}/../.." -maxdepth 1 -type f -name '*.deb' 2>/dev/null
  } | awk '!seen[$0]++'
)

if [[ ${#FOUND_DEBS[@]} -eq 0 ]]; then
  echo "no deb package found (checked ${BUILD_DIR}, project root)" >&2
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
