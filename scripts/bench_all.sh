#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)
source "${SCRIPT_DIR}/bench_common.sh"

BENCH_SCRIPTS=(
    bench_tcp.sh
    bench_http.sh
    bench_kv.sh
    bench_db.sh
    bench_log.sh
    bench_rpc.sh
    bench_ws.sh
)

main() {
    local run=false
    if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
        echo "用法: $0 [--run]"
        echo "  默认仅编译全部 benchmark 二进制"
        echo "  --run  编译并运行全部 benchmark（耗时较长）"
        exit 0
    fi
    if [[ "${1:-}" == "--run" ]]; then
        run=true
    fi

    log_info "开始处理全部 benchmark..."
    for s in "${BENCH_SCRIPTS[@]}"; do
        local script="${SCRIPT_DIR}/${s}"
        if [ ! -x "${script}" ]; then
            log_err "脚本不存在或不可执行: ${script}"
            exit 1
        fi
        if [ "${run}" == "true" ]; then
            log_step "运行 ${s}..."
            "${script}"
        else
            log_info "编译 ${s}..."
            "${script}" --no-build
        fi
    done

    if [ "${run}" == "true" ]; then
        log_ok "全部 benchmark 运行完成"
    else
        log_ok "全部 benchmark 编译完成，二进制位于 ${BIN_DIR}"
    fi
}

main "$@"
