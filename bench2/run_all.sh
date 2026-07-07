#!/usr/bin/env bash
# bench2/run_all.sh — 全量 QPS 并发测试脚本
# 用法: bash bench2/run_all.sh [--quick]    (--quick = 快速模式: 2秒/100连接)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${PROJECT_DIR}/bin"

# 配置
if [[ "${1:-}" == "--quick" ]]; then
    DUR=2; CONN=100; SIZE=64
    echo ">>> 快速模式: ${CONN}连接 x ${DUR}秒"
else
    DUR=10; CONN=500; SIZE=64
    echo ">>> 标准模式: ${CONN}连接 x ${DUR}秒"
fi

# 记录结果
RESULT_FILE="${PROJECT_DIR}/BENCH_RESULTS.txt"
echo "Zero Framework QPS Benchmark Results" > "$RESULT_FILE"
echo "Date: $(date)" >> "$RESULT_FILE"
echo "Workers: 4 threads" >> "$RESULT_FILE"
echo "========================================" >> "$RESULT_FILE"

run_bench() {
    local name="$1" server_bin="$2" server_args="$3" client_bin="$4" client_args="$5"
    echo ""
    echo "=============================================="
    echo "  $name"
    echo "=============================================="

    # 启动服务器
    "${BIN}/${server_bin}" ${server_args} &
    local spid=$!
    sleep 1

    # 跑客户端
    set +e
    "${BIN}/${client_bin}" ${client_args} 2>&1 | tee -a "$RESULT_FILE"
    set -e

    # 停止服务器
    kill $spid 2>/dev/null || true
    wait $spid 2>/dev/null || true
    sleep 1
}

# ---- 编译 ----
echo ">>> 编译 bench2 ..."
cd "$PROJECT_DIR"
cmake --build build -j$(nproc) --target tcp_echo_server http_echo_server ws_echo_server tcp_echo_client http_echo_client ws_echo_client kv_bench_client kv_server 2>&1 | tail -5

# ---- TCP Echo ----
run_bench "TCP Echo QPS" \
    "tcp_echo_server" "-p 18020 -w 4" \
    "tcp_echo_client" "-p 18020 -c ${CONN} -d ${DUR} -s ${SIZE}"

# 多 payload 测试
for sz in 64 256 1024; do
    run_bench "TCP Echo (${sz}B)" \
        "tcp_echo_server" "-p 18020 -w 4" \
        "tcp_echo_client" "-p 18020 -c ${CONN} -d ${DUR} -s ${sz}"
done

# ---- HTTP Echo ----
run_bench "HTTP POST Echo QPS" \
    "http_echo_server" "-p 18022 -w 4" \
    "http_echo_client" "-p 18022 -c ${CONN} -d ${DUR} -s ${SIZE}"

for sz in 64 256 1024; do
    run_bench "HTTP Echo (${sz}B)" \
        "http_echo_server" "-p 18022 -w 4" \
        "http_echo_client" "-p 18022 -c ${CONN} -d ${DUR} -s ${sz}"
done

# ---- WebSocket Echo ----
run_bench "WebSocket Echo QPS" \
    "ws_echo_server" "-p 18024 -w 4" \
    "ws_echo_client" "-p 18024 -c ${CONN} -d ${DUR} -s ${SIZE}"

for sz in 64 256 1024; do
    run_bench "WS Echo (${sz}B)" \
        "ws_echo_server" "-p 18024 -w 4" \
        "ws_echo_client" "-p 18024 -c ${CONN} -d ${DUR} -s ${sz}"
done

# ---- KV (Redis Protocol) ----
# Pre-populate keys for GET benchmark
echo ">>> KV: pre-populating 10000 keys..."
"${BIN}/kv_server" -p 16379 -w 4 &
KVPID=$!
sleep 1

# SET a batch of keys
for i in $(seq 0 9999); do
    echo -ne "*3\r\n\$3\r\nSET\r\n\$9\r\nbench:$i\r\n\$64\r\n$(printf 'V%.0s' {1..64})\r\n" | nc -q0 127.0.0.1 16379 > /dev/null 2>&1 || true
done
echo ">>> KV: pre-population done"

for cmd in PING GET SET; do
    run_bench "KV ${cmd} QPS" \
        "" "" \
        "kv_bench_client" "-p 16379 -c ${CONN} -d ${DUR} -s ${SIZE} -t ${cmd}"
done

# Pipeline tests
for pl in 1 10 50; do
    run_bench "KV GET Pipeline x${pl}" \
        "" "" \
        "kv_bench_client" "-p 16379 -c ${CONN} -d ${DUR} -t GET -P ${pl}"
done

kill $KVPID 2>/dev/null || true
wait $KVPID 2>/dev/null || true

echo ""
echo "=============================================="
echo "  全量 QPS 测试完成！结果: ${RESULT_FILE}"
echo "=============================================="
cat "$RESULT_FILE"
