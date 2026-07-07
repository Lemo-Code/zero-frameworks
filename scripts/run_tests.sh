#!/usr/bin/env bash
# ==============================================================================
# zero-framework 测试用例运行脚本
# ==============================================================================
# 功能：
#   1. 自动构建所有目标（含测试）
#   2. 通过 CTest 运行全部 / 按分类 / 按名称 执行测试
#   3. 支持并行执行、详细输出、失败重跑
#   4. 彩色输出 + 通过率统计
#
# 用法：
#   ./scripts/run_tests.sh                  # 构建并运行全部测试
#   ./scripts/run_tests.sh --no-build        # 跳过构建，直接运行
#   ./scripts/run_tests.sh -j8               # 8 线程并行
#   ./scripts/run_tests.sh --category kv     # 仅 KV 相关测试
#   ./scripts/run_tests.sh --filter "cache"  # 名称包含 cache 的测试
#   ./scripts/run_tests.sh --verbose         # 详细输出（显示每项测试）
#   ./scripts/run_tests.sh --rerun-failed    # 仅重跑上次失败项
#   ./scripts/run_tests.sh --skip-slow       # 跳过慢速/集成测试
# ==============================================================================

set -euo pipefail

# ---- 颜色 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "${CYAN}[STEP]${NC}  $*"; }

# ---- 默认配置 ----
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BIN_DIR="${PROJECT_ROOT}/bin"
NO_BUILD=false
BUILD_ONLY=false
JOBS=$(nproc 2>/dev/null || echo 4)
CATEGORY=""
FILTER=""
VERBOSE=false
RERUN_FAILED=false
SKIP_SLOW=false
SKIP_INTEGRATION=false
CTEST_ARGS=""

# ---- 测试分类定义 ----
# 与 tests/CMakeLists.txt 中实际启用的目标保持一致
declare -A TEST_CATEGORIES
TEST_CATEGORIES["core"]="
test_util_sample|test_bytearray|test_address|test_fd_manager|
test_iomanager|test_socket|test_fiber|test_timer|test_mutex|
test_thread|test_log|test_config_var|test_config|test_config_center|
test_fiber_stack|test_framework_extensions|test_log_system|
test_production_features"

TEST_CATEGORIES["kv"]="
test_kv_info|test_kv_resp|test_kv_rdb|test_kv_pubsub|test_kv_aof|
test_kv_repl|test_kv_p0p1|test_kv_phase10|test_kv_single|test_kv_commands|
test_kv_blocking|test_kv_extended|test_kv_repl_e2e"

TEST_CATEGORIES["rpc"]="
test_rpc_mini|test_rpc_client|test_rpc_load_balancer|test_rpc_server|
test_rpc_v2|test_rpc$|test_rpc_etcd_integration|test_rpc_qps|
test_service_governance"

TEST_CATEGORIES["http"]="
test_http_parser|test_http2|test_http_server"

TEST_CATEGORIES["db"]="
test_db_orm|test_db_orm_advanced|test_mysql_pool|test_mysql_async|
test_db_mysql_integration"

TEST_CATEGORIES["security"]="
test_jwt|test_cert_manager|test_security"


TEST_CATEGORIES["net"]="
test_socket_stream|test_zlib_stream"

# 集成测试（需要外部服务）
TEST_CATEGORIES["integration"]="
test_db_mysql_integration|test_rpc_etcd_integration"

# 慢速/压测（QPS 类）
TEST_CATEGORIES["bench"]="test_rpc_qps|test_log_system"

# 快速测试（排除集成和压测）
TEST_CATEGORIES["quick"]="all_but_integration_and_bench"

# ---- 帮助 ----
show_help() {
    cat << EOF
zero-framework 测试运行脚本

用法: $0 [选项]

构建选项:
  --no-build          跳过构建，直接运行已有二进制
  --build-only        仅构建，不运行测试
  -j, --jobs N        并行编译/测试线程数（默认: $(nproc)）

筛选选项:
  -c, --category CAT  按分类运行 (core/kv/rpc/http/db/security/cache/net/integration/quick)
  -f, --filter PAT    名称匹配模式（grep 风格正则，传给 ctest -R）
  -e, --exclude PAT   排除匹配的测试（传给 ctest -E）
  --skip-slow         跳过慢速测试（QPS/压测）
  --skip-integration  跳过集成测试（MySQL/etcd 等）

输出选项:
  -v, --verbose       详细输出（显示每条测试的 PASS/FAIL）
  -q, --quiet         安静模式（仅显示摘要）
  -o, --output-on-failure  失败时输出详细信息

重跑选项:
  --rerun-failed      仅重跑上次失败的测试

其他:
  -h, --help          显示此帮助
  --list-categories   列出所有测试分类
  --list-tests        列出所有已注册的测试名称

示例:
  ./scripts/run_tests.sh                          # 全部测试
  ./scripts/run_tests.sh --category kv            # 仅 KV 测试
  ./scripts/run_tests.sh --category quick         # 快速测试（跳过慢速/集成）
  ./scripts/run_tests.sh -f "cache" -v            # 名称含 cache + 详细输出
  ./scripts/run_tests.sh --rerun-failed -v        # 重跑上次失败
  ./scripts/run_tests.sh --skip-integration -j8   # 跳过集成测试 + 8 并行
EOF
    exit 0
}

list_categories() {
    echo "可用测试分类:"
    echo ""
    for cat in core kv rpc http db security cache net integration bench quick; do
        local count=$(echo "${TEST_CATEGORIES[$cat]}" | tr '|' '\n' | sed '/^$/d' | wc -l)
        printf "  %-15s  (%2d 个测试)\n" "$cat" "$count"
    done
    echo ""
    echo "说明:"
    echo "  core       - 框架核心 (IO/协程/日志/配置)"
    echo "  kv         - KV 存储引擎"
    echo "  rpc        - RPC 通信"
    echo "  http       - HTTP 服务"
    echo "  db         - 数据库 ORM"
    echo "  security   - 安全 (WAF/JWT/mTLS)"
    echo "  cache      - 缓存"
    echo "  net        - 网络层"
    echo "  integration- 集成测试 (需 MySQL/etcd)"
    echo "  bench      - 压测/QPS"
    echo "  quick      - 快速测试 (排除集成+压测)"
    exit 0
}

# ---- 参数解析 ----
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --no-build)        NO_BUILD=true; shift ;;
            --build-only)      BUILD_ONLY=true; shift ;;
            -j|--jobs)         JOBS="$2"; shift 2 ;;
            -j*)               JOBS="${1#-j}"; shift ;;       # -j8 紧凑写法
            -c|--category)     CATEGORY="$2"; shift 2 ;;
            -c*)               CATEGORY="${1#-c}"; shift ;;    # -ckv 紧凑写法
            -f|--filter)       FILTER="$2"; shift 2 ;;
            -f*)               FILTER="${1#-f}"; shift ;;      # -fcache 紧凑写法
            -e|--exclude)      CTEST_ARGS="${CTEST_ARGS} -E '$2'"; shift 2 ;;
            -e*)               CTEST_ARGS="${CTEST_ARGS} -E '${1#-e}'"; shift ;;
            --skip-slow)       SKIP_SLOW=true; shift ;;
            --skip-integration) SKIP_INTEGRATION=true; shift ;;
            -v|--verbose)      VERBOSE=true; CTEST_ARGS="${CTEST_ARGS} -V"; shift ;;
            -q|--quiet)        CTEST_ARGS="${CTEST_ARGS} --quiet"; shift ;;
            -o|--output-on-failure) CTEST_ARGS="${CTEST_ARGS} --output-on-failure"; shift ;;
            --rerun-failed)    RERUN_FAILED=true; shift ;;
            -h|--help)         show_help ;;
            --list-categories) list_categories ;;
            --list-tests)      NO_BUILD=true; LIST_ONLY=true; shift ;;
            *) log_error "未知选项: $1"; show_help ;;
        esac
    done
}

# ---- 构建 ----
run_build() {
    log_step "构建测试目标..."

    if [ ! -d "${BUILD_DIR}" ]; then
        log_info "创建 build 目录..."
        mkdir -p "${BUILD_DIR}"
    fi

    # 如果没跑过 cmake，先配置
    if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        log_info "执行 cmake 配置..."
        if ! cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" > /tmp/cmake_config.log 2>&1; then
            log_error "cmake 配置失败"
            cat /tmp/cmake_config.log
            exit 1
        fi
    fi

    # 全量构建（包含 zero 库与全部 test_* 目标）
    log_info "编译全部目标 (${JOBS} 并行)..."
    if cmake --build "${BUILD_DIR}" --target all -j"${JOBS}"; then
        log_ok "编译完成"
    else
        log_error "编译失败，请检查错误信息"
        exit 1
    fi
}

# ---- 运行测试 ----
run_tests() {
    if [ ! -d "${BUILD_DIR}" ]; then
        log_error "build 目录不存在，请先运行构建"
        exit 1
    fi

    cd "${BUILD_DIR}"

    local args="--test-dir ${BUILD_DIR} -j${JOBS}"

    # 分类筛选
    if [ -n "${CATEGORY}" ]; then
        if [ "${CATEGORY}" == "quick" ]; then
            args="${args} -E 'mysql_integration|etcd_integration|rpc_qps'"
        elif [ -n "${TEST_CATEGORIES[$CATEGORY]}" ]; then
            local pattern=$(echo "${TEST_CATEGORIES[$CATEGORY]}" | sed 's/|$//' | sed 's/^[[:space:]]*//' | tr -d '\n' | sed 's/|/|/g')
            args="${args} -R '${pattern}'"
        else
            log_error "未知分类: ${CATEGORY}"
            list_categories
            exit 1
        fi
    fi

    # 名称过滤
    if [ -n "${FILTER}" ]; then
        args="${args} -R '${FILTER}'"
    fi

    # 慢速排除
    if [ "${SKIP_SLOW}" = true ]; then
        args="${args} -E 'rpc_qps|Qps'"
    fi

    # 集成排除
    if [ "${SKIP_INTEGRATION}" = true ]; then
        args="${args} -E 'mysql_integration|etcd_integration'"
    fi

    # 重跑失败
    if [ "${RERUN_FAILED}" = true ]; then
        args="${args} --rerun-failed"
    fi

    # 额外参数
    args="${args} ${CTEST_ARGS}"

    # 统计
    local total_tests=$(ctest -N ${args} 2>/dev/null | grep -c 'Test #' || echo "?")
    log_step "运行测试 (约 ${total_tests} 项, ${JOBS} 并行)..."
    echo ""

    # 实际执行
    local start_time=$(date +%s)
    local ctest_rc=0
    ctest ${args} 2>&1 || ctest_rc=$?
    local end_time=$(date +%s)
    local elapsed=$((end_time - start_time))

    echo ""
    print_summary ${ctest_rc} ${elapsed} "${total_tests}"

    cd "${PROJECT_ROOT}"
    return ${ctest_rc}
}

# ---- 输出摘要 ----
print_summary() {
    local rc=$1
    local elapsed=$2
    local total=$3

    echo "╔══════════════════════════════════════════════╗"
    echo "║          测试运行结果                         ║"
    echo "╠══════════════════════════════════════════════╣"

    # 尝试从 ctest 输出统计（如果可用，从 LastTest.log 读取）
    local logfile="${BUILD_DIR}/Testing/TAG"
    if [ -f "${logfile}" ]; then
        local tag=$(cat "${logfile}" 2>/dev/null)
        local testlog="${BUILD_DIR}/Testing/${tag}/Test.xml"
        if [ -f "${testlog}" ]; then
            # 简单统计
            local passed=$(grep -c 'Status="passed"' "${testlog}" 2>/dev/null || echo "?")
            local failed=$(grep -c 'Status="failed"' "${testlog}" 2>/dev/null || echo "0")
            local skipped=$(grep -c 'Status="notrun"' "${testlog}" 2>/dev/null || echo "0")
            printf "║  %-44s ║\n" "通过: ${GREEN}${passed}${NC}  失败: ${RED}${failed}${NC}  跳过: ${YELLOW}${skipped}${NC}"
        fi
    fi

    printf "║  %-44s ║\n" "耗时: ${elapsed}秒"
    printf "║  %-44s ║\n" "并行度: ${JOBS}"

    if [ ${rc} -eq 0 ]; then
        echo "╠══════════════════════════════════════════════╣"
        echo -e "║  ${GREEN}全部测试通过! ✓${NC}                           ║"
        echo "╚══════════════════════════════════════════════╝"
        echo ""
    else
        echo "╠══════════════════════════════════════════════╣"
        echo -e "║  ${RED}存在失败测试 ✗${NC}                               ║"
        echo "╚══════════════════════════════════════════════╝"
        echo ""
        echo "查看失败详情:"
        echo "  cat ${BUILD_DIR}/Testing/Temporary/LastTest.log"
        echo "  ./scripts/run_tests.sh --rerun-failed -v"
        echo ""
    fi
}

# ---- 列出测试 ----
list_all_tests() {
    if [ ! -d "${BUILD_DIR}" ] || [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
        log_info "需要先运行 cmake 配置..."
        mkdir -p "${BUILD_DIR}"
        cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" > /dev/null 2>&1
    fi

    cd "${BUILD_DIR}"
    echo ""
    echo "已注册的测试列表:"
    echo "=================="
    ctest -N 2>/dev/null | grep 'Test #' | while read line; do
        local name=$(echo "$line" | awk '{print $3}')
        echo "  ${name}"
    done
    echo ""
    local count=$(ctest -N 2>/dev/null | grep -c 'Test #' || echo "0")
    echo "共 ${count} 项测试"
    cd "${PROJECT_ROOT}"
}

# ---- 主流程 ----
main() {
    parse_args "$@"

    echo ""
    echo "╔══════════════════════════════════════════════╗"
    echo "║  zero-framework 测试执行器                   ║"
    echo "╚══════════════════════════════════════════════╝"
    echo ""

    # 列出测试
    if [ "${LIST_ONLY:-false}" = true ]; then
        list_all_tests
        exit 0
    fi

    # 构建
    if [ "${NO_BUILD}" = false ]; then
        run_build
    fi

    # 构建完即止
    if [ "${BUILD_ONLY}" = true ]; then
        log_ok "构建完成（--build-only）"
        exit 0
    fi

    # 运行
    run_tests
}

main "$@"
