#!/usr/bin/env bash
# ==============================================================================
# build.sh — 统一构建脚本
# ==============================================================================
# 用法:
#   ./scripts/build.sh                  # Release 构建 (默认)
#   ./scripts/build.sh debug            # Debug 构建
#   ./scripts/build.sh clean            # 清理构建产物
#   ./scripts/build.sh release -j8      # Release 8 并行
#   ./scripts/build.sh --target kv_server  # 只构建指定目标
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }

# ---- 默认值 ----
BUILD_TYPE="Release"
JOBS=$(nproc 2>/dev/null || echo 4)
TARGET=""

# ---- 帮助 ----
show_help() {
    cat << EOF
zero-framework 构建脚本

用法: $0 [模式] [选项]

模式:
  release               Release 构建 (默认, -O3)
  debug                 Debug 构建 (-O0 -g, 带 ASAN)
  clean                 清理 build/ bin/ lib/ 目录

选项:
  -j, --jobs N          并行编译数 (默认: $(nproc))
  -t, --target NAME     只构建指定目标
  --cmake-only          仅 cmake 配置，不编译
  -v, --verbose         详细编译输出

示例:
  ./scripts/build.sh                    # Release 构建
  ./scripts/build.sh debug              # Debug 构建
  ./scripts/build.sh release -j8        # 8 线程并行
  ./scripts/build.sh -t kv_server       # 只构建 kv_server
  ./scripts/build.sh clean              # 清理
EOF
    exit 0
}

# ---- 参数解析 ----
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            release|Release)
                BUILD_TYPE="Release"; shift ;;
            debug|Debug)
                BUILD_TYPE="Debug"; shift ;;
            clean|Clean)
                do_clean; exit 0 ;;
            -j|--jobs)
                JOBS="$2"; shift 2 ;;
            -t|--target)
                TARGET="$2"; shift 2 ;;
            --cmake-only)
                CMAKE_ONLY=true; shift ;;
            -v|--verbose)
                VERBOSE=true; shift ;;
            -h|--help)
                show_help ;;
            *)
                log_err "未知参数: $1"
                show_help ;;
        esac
    done
}

# ---- 清理 ----
do_clean() {
    log_info "清理构建产物..."
    rm -rf "${BUILD_DIR}"
    rm -rf "${PROJECT_ROOT}/bin"
    rm -rf "${PROJECT_ROOT}/lib"
    log_ok "清理完成"
}

# ---- cmake 配置 ----
do_cmake() {
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    local cmake_flags="-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"

    # Debug 模式下开启 AddressSanitizer
    if [ "${BUILD_TYPE}" = "Debug" ]; then
        cmake_flags="${cmake_flags} -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g -fsanitize=address -fno-omit-frame-pointer'"
    fi

    log_info "cmake 配置 (${BUILD_TYPE})..."
    cmake .. ${cmake_flags} 2>&1 | tail -3
    log_ok "cmake 配置完成"
}

# ---- 编译 ----
do_build() {
    cd "${BUILD_DIR}"

    local make_args="-j${JOBS}"
    if [ "${VERBOSE:-false}" = true ]; then
        make_args="${make_args} VERBOSE=1"
    fi

    if [ -n "${TARGET}" ]; then
        log_info "编译目标: ${TARGET}"
        make ${make_args} "${TARGET}"
    else
        log_info "全量编译 (${JOBS} 并行)..."
        local total=$(make help 2>/dev/null | grep -c '^\.' || echo "?")
        make ${make_args} 2>&1 | grep -E '^\[' | tail -5
        log_ok "编译完成"
    fi
}

# ---- 构建摘要 ----
print_summary() {
    echo ""
    echo "============================================="
    printf "  构建类型: %s\n" "${BUILD_TYPE}"
    printf "  并行数:   %s\n" "${JOBS}"
    printf "  目标:     %s\n" "${TARGET:-全部}"
    if [ -d "${PROJECT_ROOT}/bin" ]; then
        printf "  产物:     %s/bin/\n" "${PROJECT_ROOT}"
        ls "${PROJECT_ROOT}/bin/" 2>/dev/null | head -10 | while read f; do
            echo "            → $f"
        done
    fi
    echo "============================================="
}

# ---- 主流程 ----
main() {
    parse_args "$@"

    echo ""
    echo "╔══════════════════════════════════════════════╗"
    echo "║  zero-framework 构建                         ║"
    echo "╚══════════════════════════════════════════════╝"
    echo ""

    do_cmake

    if [ "${CMAKE_ONLY:-false}" = true ]; then
        log_info "仅 cmake 配置 (--cmake-only)"
        exit 0
    fi

    do_build
    print_summary
}

main "$@"
