#!/usr/bin/env bash
# ==============================================================================
# bench_common.sh — benchmark 编译公共函数
# ==============================================================================
# 用法: source "$(dirname "$0")/bench_common.sh"
#       build_bench <source.cc> <output_name>
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BENCH_DIR="${PROJECT_ROOT}/bench"
BIN_DIR="${PROJECT_ROOT}/bin"
BUILD_DIR="${PROJECT_ROOT}/build"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_info() { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()   { echo -e "${GREEN}[OK]${NC}    $*"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }

# 检测并收集依赖库
_collect_libs() {
    local libs="-lzero -lpthread -ldl -lyaml-cpp -ljsoncpp -lz -lssl -lcrypto"

    # protobuf
    if pkg-config --exists protobuf 2>/dev/null; then
        libs="${libs} $(pkg-config --libs protobuf)"
    elif ldconfig -p 2>/dev/null | grep -q libprotobuf; then
        libs="${libs} -lprotobuf"
    fi

    # luajit
    if pkg-config --exists luajit 2>/dev/null; then
        libs="${libs} $(pkg-config --libs luajit)"
    elif ldconfig -p 2>/dev/null | grep -q libluajit; then
        libs="${libs} -lluajit-5.1"
    fi

    # mysqlclient (optional, for db benchmarks)
    if pkg-config --exists mysqlclient 2>/dev/null; then
        libs="${libs} $(pkg-config --libs mysqlclient)"
    fi

    echo "${libs}"
}

# 确保 libzero.so 已存在
_ensure_zero_lib() {
    if [ ! -f "${PROJECT_ROOT}/lib/libzero.so" ]; then
        log_err "libzero.so 不存在，请先构建主库:"
        log_err "  cd ${PROJECT_ROOT}/build && cmake .. && make -j$(nproc 2>/dev/null || echo 4)"
        exit 1
    fi
}

# 构建单个 benchmark 二进制
# build_bench <source.cc> <output_name>
build_bench() {
    local src="$1"
    local out="$2"
    local name=$(basename "${src}")

    _ensure_zero_lib
    mkdir -p "${BIN_DIR}"

    if [ -f "${out}" ] && [ "${out}" -nt "${src}" ] && [ "${out}" -nt "${PROJECT_ROOT}/lib/libzero.so" ]; then
        log_info "二进制已是最新，跳过编译: $(basename "${out}")"
        return 0
    fi

    local libs=$(_collect_libs)
    local includes="-I${PROJECT_ROOT} -I${BENCH_DIR}"
    if [ -d "${BUILD_DIR}" ]; then
        includes="${includes} -I${BUILD_DIR}"
    fi
    if [ -d "/usr/include/jsoncpp" ]; then
        includes="${includes} -I/usr/include/jsoncpp"
    fi

    log_info "编译 ${name}..."
    g++ -std=c++14 -O3 -o "${out}" "${src}" \
        ${includes} \
        -L"${PROJECT_ROOT}/lib" \
        ${libs} \
        -Wl,-rpath,"${PROJECT_ROOT}/lib" \
        2>&1 || { log_err "编译失败: ${name}"; exit 1; }
    log_ok "编译完成 → ${out}"
}
