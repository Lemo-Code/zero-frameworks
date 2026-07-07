#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/bench_common.sh"

SRC="${BENCH_DIR}/tcp/bench_tcp_qps.cc"
OUT="${BIN_DIR}/bench_tcp_qps"

main() {
    if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
        echo "用法: $0 [--no-build] [benchmark-args...]"
        echo "  --no-build  跳过编译，直接运行已有二进制"
        exit 0
    fi
    if [[ "${1:-}" != "--no-build" ]]; then
        build_bench "${SRC}" "${OUT}"
    fi
    "${OUT}" "$@"
}

main "$@"
