#!/usr/bin/env bash
# ==============================================================================
# bench_server_qps.sh — 完整服务器端到端 QPS 压测
# ==============================================================================
# 测试 TCP echo server、HTTP echo server、RESP KV server 的完整请求处理 QPS。
#
# 用法:
#   ./scripts/bench_server_qps.sh [tcp|http|kv|all]
#   ./scripts/bench_server_qps.sh tcp   # 仅 TCP echo
#   ./scripts/bench_server_qps.sh http  # 仅 HTTP echo
#   ./scripts/bench_server_qps.sh kv    # 仅 RESP KV (需 redis-benchmark)
#   ./scripts/bench_server_qps.sh all   # 全部（默认）
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_DIR="${PROJECT_ROOT}/bin"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "${CYAN}[STEP]${NC}  $*"; }

# 默认参数
DURATION=${DURATION:-10}
CONN=${CONN:-100}
PAYLOAD=${PAYLOAD:-64}
KV_REQS=${KV_REQS:-100000}

# 启动并等待服务可用
wait_for_port() {
    local host=$1 port=$2 timeout=${3:-30}
    for ((i=0; i<timeout*10; i++)); do
        if nc -z "$host" "$port" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

run_tcp_bench() {
    log_step "TCP Echo Server QPS (port 8020, ${CONN} conn, ${PAYLOAD} bytes, ${DURATION}s)"
    local pid
    "${BIN_DIR}/bench_server" -p 8020 -w 4 > /tmp/zero_bench_server.log 2>&1 &
    pid=$!
    if ! wait_for_port 127.0.0.1 8020; then
        log_error "TCP server 启动失败"
        kill $pid 2>/dev/null || true
        return 1
    fi
    "${BIN_DIR}/qps_client" -p 8020 -c "$CONN" -d "$DURATION" -s "$PAYLOAD" 2>&1 | tail -12
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
}

run_http_bench() {
    log_step "HTTP Echo Server QPS (port 8022, ${CONN} conn, ${PAYLOAD} bytes, ${DURATION}s)"
    local pid
    "${BIN_DIR}/http_bench_server" > /tmp/zero_http_bench_server.log 2>&1 &
    pid=$!
    if ! wait_for_port 127.0.0.1 8022; then
        log_error "HTTP server 启动失败"
        kill $pid 2>/dev/null || true
        return 1
    fi
    "${BIN_DIR}/http_qps_client" -p 8022 -c "$CONN" -d "$DURATION" -s "$PAYLOAD" -u /echo 2>&1 | tail -12
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
}

run_kv_bench() {
    if ! command -v redis-benchmark >/dev/null 2>&1; then
        log_warn "redis-benchmark 未安装，跳过 KV server 压测"
        return 0
    fi
    log_step "RESP KV Server QPS (port 6379, ${CONN} conn, ${KV_REQS} reqs)"
    local pid
    rm -f /tmp/zero_kv_bench.rdb /tmp/zero_kv_bench.aof
    "${BIN_DIR}/kv_server" -p 6379 -rdb /tmp/zero_kv_bench.rdb > /tmp/zero_kv_server.log 2>&1 &
    pid=$!
    if ! wait_for_port 127.0.0.1 6379; then
        log_error "KV server 启动失败"
        kill $pid 2>/dev/null || true
        return 1
    fi
    echo "--- SET ---"
    redis-benchmark -p 6379 -t set -n "$KV_REQS" -c "$CONN" 2>&1 | tail -4
    echo "--- GET ---"
    redis-benchmark -p 6379 -t get -n "$KV_REQS" -c "$CONN" 2>&1 | tail -4
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
    rm -f /tmp/zero_kv_bench.rdb /tmp/zero_kv_bench.aof
}

main() {
    local mode="${1:-all}"
    case "$mode" in
        tcp)  run_tcp_bench ;;
        http) run_http_bench ;;
        kv)   run_kv_bench ;;
        all)
            run_tcp_bench
            echo ""
            run_http_bench
            echo ""
            run_kv_bench
            ;;
        --help|-h)
            echo "用法: $0 [tcp|http|kv|all]"
            echo "环境变量: DURATION CONN PAYLOAD KV_REQS"
            exit 0
            ;;
        *) log_error "未知模式: $mode"; exit 1 ;;
    esac
    log_ok "服务器 QPS 压测完成"
}

main "$@"
