#!/usr/bin/env bash
# ==============================================================================
# format.sh — 代码格式化 (clang-format)
# ==============================================================================
# 用法:
#   ./scripts/format.sh              # 格式化所有 .cc/.h 文件
#   ./scripts/format.sh --check      # 仅检查，不修改 (CI 模式)
#   ./scripts/format.sh --staged     # 仅格式化 git staged 文件
#   ./scripts/format.sh path/to/file # 格式化指定文件
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
log_info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $*"; }

# ---- 查找 clang-format ----
find_clang_format() {
    for candidate in clang-format clang-format-18 clang-format-17 clang-format-16 clang-format-15 clang-format-14; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# ---- 确保 .clang-format 存在 ----
ensure_config() {
    local config="${PROJECT_ROOT}/.clang-format"
    if [ ! -f "${config}" ]; then
        log_info "生成 .clang-format 配置..."
        cat > "${config}" << 'EOF'
---
BasedOnStyle: Google
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 120
AccessModifierOffset: -4
AllowShortIfStatementsOnASingleLine: false
AllowShortLoopsOnASingleLine: false
AllowShortFunctionsOnASingleLine: Inline
BreakBeforeBraces: Attach
SortIncludes: false
PointerAlignment: Left
SpaceAfterCStyleCast: true
IncludeBlocks: Preserve

# C++ 标准
Standard: c++14

# 头文件排序
IncludeCategories:
  - Regex: '^<.*\.h>'
    Priority: 1
  - Regex: '^<.*>'
    Priority: 2
  - Regex: '^"zero/.*"'
    Priority: 3
  - Regex: '.*'
    Priority: 4
EOF
        log_ok ".clang-format 已生成"
    fi
}

# ---- 获取源文件列表 ----
get_sources() {
    if [ "${STAGED:-false}" = true ]; then
        cd "${PROJECT_ROOT}"
        git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cc|h|hpp|cpp)$' || true
    elif [ $# -gt 0 ]; then
        echo "$@"
    else
        find "${PROJECT_ROOT}/zero" \
             "${PROJECT_ROOT}/bench" \
             "${PROJECT_ROOT}/examples" \
             "${PROJECT_ROOT}/tests" \
             -type f \( -name '*.cc' -o -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) \
             ! -name '*.rl.cc' ! -name '*.pb.cc' ! -name '*.pb.h' ! -path '*/build/*'
    fi
}

# ---- 执行格式化 ----
run_format() {
    local clang_format="$1"
    shift
    local files=("$@")

    if [ ${#files[@]} -eq 0 ]; then
        log_info "没有需要格式化的文件"
        return 0
    fi

    if [ "${CHECK_ONLY:-false}" = true ]; then
        log_info "检查 ${#files[@]} 个文件..."
        local violations=0
        for f in "${files[@]}"; do
            if ! "${clang_format}" --dry-run --Werror "${f}" 2>/dev/null; then
                echo "  需要格式化: ${f}"
                violations=$((violations + 1))
            fi
        done
        if [ ${violations} -eq 0 ]; then
            log_ok "所有 ${#files[@]} 个文件格式正确"
        else
            log_err "${violations} 个文件格式不正确"
            echo "  请运行: ./scripts/format.sh"
            exit 1
        fi
    else
        log_info "格式化 ${#files[@]} 个文件..."
        echo "${files[@]}" | tr ' ' '\n' | xargs -P$(nproc) -I{} "${clang_format}" -i "{}"
        log_ok "格式化完成"
    fi
}

# ---- 主流程 ----
main() {
    CHECK_ONLY=false
    STAGED=false

    # 解析参数
    local files=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --check) CHECK_ONLY=true; shift ;;
            --staged) STAGED=true; shift ;;
            -h|--help)
                echo "用法: $0 [--check] [--staged] [文件...]"
                echo "  --check   仅检查，不修改"
                echo "  --staged  仅格式化 git staged 文件"
                exit 0
                ;;
            *) files+=("$1"); shift ;;
        esac
    done

    ensure_config

    local cf=$(find_clang_format)
    if [ -z "${cf}" ]; then
        log_err "未找到 clang-format，请安装: sudo apt install clang-format"
        exit 1
    fi
    log_info "使用: ${cf} ($(${cf} --version | head -1))"

    # 获取文件列表
    local src_files=()
    if [ ${#files[@]} -gt 0 ]; then
        src_files=("${files[@]}")
    else
        mapfile -t src_files < <(get_sources)
    fi

    run_format "${cf}" "${src_files[@]}"
}

main "$@"
