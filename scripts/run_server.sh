#!/usr/bin/env bash
# ==============================================================================
# run_server.sh — 启动 zero-framework 服务器
# ==============================================================================
# 用法:
#   ./scripts/run_server.sh kv          # KV 服务器 (默认 6379)
#   ./scripts/run_server.sh echo        # Echo 服务器 (默认 8020)
#   ./scripts/run_server.sh bench       # Bench TCP 服务器 (默认 8020)
#   ./scripts/run_server.sh http        # HTTP Bench 服务器 (默认 8080)
#   ./scripts/run_server.sh ws          # WebSocket 服务器 (默认 8090)
#   ./scripts/run_server.sh sentinel    # Sentinel 服务器
#   ./scripts/run_server.sh production  # 生产环境服务器
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_DIR="${PROJECT_ROOT}/bin"
BUILD_DIR="${PROJECT_ROOT}/build"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }

# ---- 默认配置 ----
KV_PORT="${KV_PORT:-6379}"
KV_BUS_PORT="${KV_BUS_PORT:-16379}"
ECHO_PORT="${ECHO_PORT:-8020}"
BENCH_PORT="${BENCH_PORT:-8020}"
HTTP_PORT="${HTTP_PORT:-8080}"
WS_PORT="${WS_PORT:-8090}"
SENTINEL_PORT="${SENTINEL_PORT:-17000}"
SENTINEL_PEERS="${SENTINEL_PEERS:-}"

# ---- 服务器定义 ----
declare -A SERVER_MAP
SERVER_MAP["kv"]="kv_server"
SERVER_MAP["echo"]="echo_server"
SERVER_MAP["bench"]="bench_server"
SERVER_MAP["http"]="http_bench_server"
SERVER_MAP["ws"]="ws_bench_server"
SERVER_MAP["sentinel"]="sentinel_server"

show_help() {
    cat << EOF
zero-framework 服务器启动脚本

用法: $0 <服务器类型> [选项]

服务器类型:
  kv           KV 存储服务器            (端口: ${KV_PORT})
  echo         TCP Echo 服务器          (端口: ${ECHO_PORT})
  bench        TCP Benchmark 服务器     (端口: ${BENCH_PORT})
  http         HTTP Benchmark 服务器    (端口: ${HTTP_PORT})
  ws           WebSocket 服务器         (端口: ${WS_PORT})
  sentinel     Sentinel 哨兵服务器      (端口: ${SENTINEL_PORT})

选项:
  --no-build   跳过编译
  --port N     指定端口

环境变量:
  KV_PORT=6379   KV 服务器端口
  HTTP_PORT=8080 HTTP 服务器端口
  WS_PORT=8090   WebSocket 服务器端口

示例:
  ./scripts/run_server.sh kv
  ./scripts/run_server.sh kv --port 6380
  ./scripts/run_server.sh http --no-build
  KV_PORT=6380 ./scripts/run_server.sh kv
EOF
    exit 0
}

# ---- 参数解析 ----
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help) show_help ;;
            --no-build) NO_BUILD=true; shift ;;
            --port) CUSTOM_PORT="$2"; shift 2 ;;
            *) SERVER="$1"; shift ;;
        esac
    done
}

# ---- 构建 ----
ensure_built() {
    if [ "${NO_BUILD:-false}" = true ]; then
        return 0
    fi

    local bin="${BIN_DIR}/${SERVER_BIN}"
    if [ -f "${bin}" ]; then
        log_info "服务器二进制已存在，跳过编译"
        return 0
    fi

    log_info "编译 ${SERVER_BIN}..."
    cd "${BUILD_DIR}"

    if [ ! -f "CMakeCache.txt" ]; then
        log_info "cmake 未配置，先执行 cmake..."
        cmake .. > /dev/null 2>&1
    fi

    if make -j$(nproc) "${SERVER_BIN}" 2>&1 | tail -3; then
        log_ok "编译完成"
    else
        log_err "编译失败"
        exit 1
    fi
}

# ---- 启动 ----
run_server() {
    local bin="${BIN_DIR}/${SERVER_BIN}"

    if [ ! -f "${bin}" ]; then
        log_err "二进制不存在: ${bin}"
        log_info "请先执行: ./scripts/build.sh -t ${SERVER_BIN}"
        exit 1
    fi

    echo ""
    echo "╔══════════════════════════════════════════════╗"
    printf "║  启动: %-37s ║\n" "${SERVER_DESC}"
    echo "╚══════════════════════════════════════════════╝"
    echo ""

    # 根据服务器类型设置参数
    case "${SERVER}" in
        kv)
            local port="${CUSTOM_PORT:-${KV_PORT}}"
            log_info "KV Server → 127.0.0.1:${port}"
            "${bin}" -p "${port}" -b "${KV_BUS_PORT}" "$@"
            ;;
        echo)
            local port="${CUSTOM_PORT:-${ECHO_PORT}}"
            log_info "Echo Server → 0.0.0.0:${port}"
            "${bin}" -p "${port}" "$@"
            ;;
        bench)
            local port="${CUSTOM_PORT:-${BENCH_PORT}}"
            log_info "Bench Server → 0.0.0.0:${port}"
            "${bin}" -p "${port}" "$@"
            ;;
        http)
            local port="${CUSTOM_PORT:-${HTTP_PORT}}"
            log_info "HTTP Bench Server → 0.0.0.0:${port}"
            "${bin}" -p "${port}" "$@"
            ;;
        ws)
            local port="${CUSTOM_PORT:-${WS_PORT}}"
            log_info "WebSocket Server → 0.0.0.0:${port}"
            "${bin}" -p "${port}" "$@"
            ;;
        sentinel)
            local port="${CUSTOM_PORT:-${SENTINEL_PORT}}"
            log_info "Sentinel Server → 127.0.0.1:${port}"
            "${bin}" -p "${port}" "$@"
            ;;
    esac
}

# ---- 主流程 ----
main() {
    if [ $# -eq 0 ]; then
        show_help
    fi

    parse_args "$@"

    # 验证服务器类型
    SERVER_BIN="${SERVER_MAP[${SERVER}]:-}"
    if [ -z "${SERVER_BIN}" ]; then
        log_err "未知服务器类型: ${SERVER}"
        echo "可用: ${!SERVER_MAP[*]}"
        exit 1
    fi

    SERVER_DESC="${SERVER} (${SERVER_BIN})"

    ensure_built
    run_server
}

main "$@"
