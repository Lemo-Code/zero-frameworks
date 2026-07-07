#!/usr/bin/env bash
# ==============================================================================
# zero-framework 开发环境一键配置脚本
# ==============================================================================
# 功能：
#   1. 自动检测 Linux 发行版（Ubuntu/Debian, CentOS/RHEL/Fedora, Arch）
#   2. 安装所有编译依赖（编译器、CMake、库、工具链）
#   3. 处理 Google Test 的特殊安装（Ubuntu libgtest-dev 只含源码需手动编译）
#   4. 验证 cmake 配置是否能通过
#
# 用法：
#   chmod +x scripts/setup_env.sh
#   ./scripts/setup_env.sh                # 完整安装 + 验证
#   ./scripts/setup_env.sh --install-only  # 仅安装依赖
#   ./scripts/setup_env.sh --verify-only   # 仅验证 cmake
#   ./scripts/setup_env.sh --help          # 显示帮助
# ==============================================================================

set -euo pipefail

# ---- 颜色输出 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ---- 全局变量 ----
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_ONLY=false
VERIFY_ONLY=false

# ---- 帮助 ----
show_help() {
    cat << EOF
zero-framework 环境配置脚本

用法: $0 [选项]

选项:
  --install-only    仅安装系统依赖，不执行 cmake 验证
  --verify-only     仅验证 cmake 配置（假设依赖已安装）
  --help            显示此帮助

示例:
  ./scripts/setup_env.sh                    # 完整安装 + 验证
  ./scripts/setup_env.sh --install-only     # 仅安装依赖
  ./scripts/setup_env.sh --verify-only      # 仅验证

支持的系统:
  - Ubuntu  (20.04 / 22.04 / 24.04)
  - Debian  (11 / 12)
  - CentOS / RHEL (7 / 8 / 9)
  - Fedora (36+)
  - Arch Linux
EOF
    exit 0
}

# ---- 参数解析 ----
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --install-only) INSTALL_ONLY=true; shift ;;
            --verify-only)  VERIFY_ONLY=true; shift ;;
            --help)         show_help ;;
            *) log_error "未知选项: $1"; show_help ;;
        esac
    done
}

# ---- OS 检测 ----
detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS_ID="${ID}"
        OS_VERSION_ID="${VERSION_ID}"
        OS_PRETTY="${PRETTY_NAME}"
    elif [ -f /etc/redhat-release ]; then
        OS_ID="centos"
        OS_VERSION_ID=$(rpm -q --qf "%{VERSION}" "$(rpm -q --whatprovides redhat-release)" 2>/dev/null || echo "7")
        OS_PRETTY="CentOS ${OS_VERSION_ID}"
    else
        log_error "无法检测操作系统类型"
        exit 1
    fi
    log_info "检测到系统: ${OS_PRETTY}"
}

# ---- 安装: Ubuntu / Debian ----
install_ubuntu() {
    log_info "更新 apt 包索引..."
    sudo apt-get update -y -qq

    log_info "安装基础编译工具链..."
    sudo apt-get install -y -qq \
        build-essential \
        gcc \
        g++ \
        make \
        cmake \
        pkg-config \
        git \
        ragel \
        autoconf \
        automake \
        libtool \
        curl \
        wget \
        ca-certificates

    log_info "安装运行时依赖库..."
    sudo apt-get install -y -qq \
        libboost-all-dev \
        libssl-dev \
        zlib1g-dev \
        libyaml-cpp-dev \
        libjsoncpp-dev \
        libprotobuf-dev \
        protobuf-compiler \
        libgtest-dev \
        libgmock-dev \
        libluajit-5.1-dev \
        libcurl4-openssl-dev \
        libmysqlclient-dev

    # ---- Google Test: Ubuntu 的 libgtest-dev 只提供源码头文件，需手动编译 ----
    install_gtest_ubuntu
}

install_gtest_ubuntu() {
    log_info "编译安装 Google Test..."

    local GTEST_SRC="/usr/src/googletest"
    if [ ! -d "${GTEST_SRC}" ]; then
        # Ubuntu 24.04+ 可能路径不同
        GTEST_SRC="/usr/src/gtest"
    fi

    if [ ! -d "${GTEST_SRC}" ]; then
        log_warn "未找到 googletest 源码目录，尝试 apt 安装 googletest 包..."
        # 部分新版 Ubuntu 直接提供已编译好的 gtest
        if ldconfig -p 2>/dev/null | grep -q libgtest; then
            log_ok "libgtest 已可用（预编译包）"
            return 0
        fi
        # 最后手段：从 GitHub 克隆
        log_info "从 GitHub 克隆 googletest..."
        local TMPDIR=$(mktemp -d)
        git clone --depth 1 https://github.com/google/googletest.git "${TMPDIR}/googletest" 2>/dev/null || {
            log_error "无法获取 googletest 源码"
            exit 1
        }
        cd "${TMPDIR}/googletest"
        mkdir -p build && cd build
        cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local > /dev/null 2>&1
        make -j$(nproc) > /dev/null 2>&1
        sudo make install > /dev/null 2>&1
        sudo ldconfig
        cd /
        rm -rf "${TMPDIR}"
        log_ok "Google Test 编译安装完成（从 GitHub）"
        return 0
    fi

    cd "${GTEST_SRC}"
    sudo cmake CMakeLists.txt -DCMAKE_INSTALL_PREFIX=/usr/local > /dev/null 2>&1
    sudo make -j$(nproc) > /dev/null 2>&1

    # 检查编译产物
    if [ -f lib/libgtest.a ] || [ -f lib/libgtest_main.a ]; then
        log_info "安装 gtest 库文件..."
        sudo cp lib/libgtest*.a /usr/local/lib/ 2>/dev/null || sudo cp lib/libgtest*.a /usr/lib/
        sudo ldconfig
        log_ok "Google Test 编译安装完成"
    else
        log_warn "gtest 编译未生成预期产物，尝试替代方案..."
        # Ubuntu 22.04+ 使用 apt 装 libgtest-dev 后 apt install googletest 已编译好
        sudo apt-get install -y -qq googletest 2>/dev/null || true
        if ! ldconfig -p 2>/dev/null | grep -q libgtest; then
            # 从 GitHub 安装
            local TMPDIR=$(mktemp -d)
            git clone --depth 1 https://github.com/google/googletest.git "${TMPDIR}/googletest" 2>/dev/null
            cd "${TMPDIR}/googletest"
            mkdir -p build && cd build
            cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local > /dev/null 2>&1
            make -j$(nproc) > /dev/null 2>&1
            sudo make install > /dev/null 2>&1
            sudo ldconfig
            cd /
            rm -rf "${TMPDIR}"
        fi
        log_ok "Google Test 安装完成（备用方案）"
    fi
    cd "${PROJECT_ROOT}"
}

# ---- 安装: CentOS / RHEL / Fedora ----
install_centos() {
    # 启用 EPEL
    if [[ "${OS_ID}" == "centos" ]] || [[ "${OS_ID}" == "rhel" ]]; then
        log_info "启用 EPEL 仓库..."
        sudo yum install -y epel-release 2>/dev/null || sudo dnf install -y epel-release 2>/dev/null || true
    fi

    # 判断包管理器
    local PKG_MGR="dnf"
    if ! command -v dnf &>/dev/null; then
        PKG_MGR="yum"
    fi

    log_info "安装基础编译工具链..."
    sudo ${PKG_MGR} install -y \
        gcc \
        gcc-c++ \
        make \
        cmake \
        pkgconfig \
        git \
        ragel \
        autoconf \
        automake \
        libtool \
        curl \
        wget

    log_info "安装运行时依赖库..."
    sudo ${PKG_MGR} install -y \
        boost-devel \
        openssl-devel \
        zlib-devel \
        yaml-cpp-devel \
        jsoncpp-devel \
        protobuf-devel \
        protobuf-compiler \
        gtest-devel \
        gmock-devel \
        luajit-devel \
        libcurl-devel \
        mysql-devel \
        mariadb-connector-c-devel 2>/dev/null || true

    # CentOS 7 的 gtest-devel 可能不自带 cmake 配置，编译一份
    if ! find /usr/lib64 /usr/lib -name 'libgtest.a' 2>/dev/null | grep -q .; then
        install_gtest_from_source
    fi

    log_ok "依赖安装完成"
}

# ---- 安装: Arch Linux ----
install_arch() {
    log_info "安装所有依赖..."
    sudo pacman -Sy --noconfirm \
        base-devel \
        gcc \
        cmake \
        pkg-config \
        git \
        ragel \
        boost \
        openssl \
        zlib \
        yaml-cpp \
        jsoncpp \
        protobuf \
        gtest \
        gmock \
        luajit \
        curl \
        mysql++ 2>/dev/null || true
    log_ok "依赖安装完成"
}

# ---- 源码编译 Google Test（CentOS/RHEL 备用）----
install_gtest_from_source() {
    log_info "从源码编译 Google Test..."
    local TMPDIR=$(mktemp -d)
    git clone --depth 1 https://github.com/google/googletest.git "${TMPDIR}/googletest" 2>/dev/null || {
        log_warn "无法从 GitHub 克隆，跳过 gtest（部分测试将不可用）"
        return 0
    }
    cd "${TMPDIR}/googletest"
    mkdir -p build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local > /dev/null 2>&1
    make -j$(nproc) > /dev/null 2>&1
    sudo make install > /dev/null 2>&1
    sudo ldconfig
    cd /
    rm -rf "${TMPDIR}"
    log_ok "Google Test 编译安装完成"
}

# ---- 执行安装 ----
run_install() {
    detect_os

    case "${OS_ID}" in
        ubuntu|debian|linuxmint)
            install_ubuntu
            ;;
        centos|rhel|fedora|rocky|almalinux)
            install_centos
            ;;
        arch|manjaro)
            install_arch
            ;;
        *)
            log_error "不支持的操作系统: ${OS_ID}"
            echo ""
            echo "当前支持的发行版:"
            echo "  - Ubuntu 20.04+ / Debian 11+"
            echo "  - CentOS 7+ / RHEL 7+ / Fedora 36+"
            echo "  - Arch Linux"
            echo ""
            echo "请手动安装下列依赖后重新运行脚本（带 --verify-only）："
            echo "  编译器:    gcc/g++ >= 7, cmake >= 3.10"
            echo "  必需库:    boost, openssl, zlib, yaml-cpp, jsoncpp, protobuf"
            echo "  测试:      gtest, gmock"
            echo "  可选:      luajit, ragel, mysql"
            exit 1
            ;;
    esac

    log_ok "所有系统依赖安装完成"
}

# ---- 验证 cmake 配置 ----
run_verify() {
    log_info "验证 CMake 配置..."
    echo "  项目路径: ${PROJECT_ROOT}"

    if [ ! -f "${PROJECT_ROOT}/CMakeLists.txt" ]; then
        log_error "未找到 CMakeLists.txt，请确认项目路径: ${PROJECT_ROOT}"
        exit 1
    fi

    # 清理旧的 build
    if [ -d "${BUILD_DIR}" ]; then
        log_info "清理旧的 build 目录..."
        rm -rf "${BUILD_DIR}"
    fi

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    log_info "执行 cmake 配置..."
    if cmake .. 2>&1 | tee /tmp/zero_cmake_output.log; then
        log_ok "CMake 配置成功！"
        echo ""
        log_info "接下来可执行:"
        echo "  cd ${BUILD_DIR}"
        echo "  make -j\$(nproc)"
        echo ""
    else
        log_error "CMake 配置失败"
        echo ""
        echo "===== 诊断信息 ====="
        echo "cmake 输出已保存到: /tmp/zero_cmake_output.log"
        echo ""
        echo "常见问题排查:"
        echo "  1. 检查缺失的包: grep -i 'NOT FOUND' /tmp/zero_cmake_output.log"
        echo "  2. 重新安装依赖: $0 --install-only"
        echo "  3. 检查 GTest:   ls /usr/local/lib/libgtest* /usr/lib/libgtest*"
        exit 1
    fi
}

# ---- 打印依赖摘要 ----
print_summary() {
    echo ""
    echo "============================================="
    echo "  zero-framework 环境配置完成"
    echo "============================================="
    echo ""
    echo "已安装的依赖："
    echo "  编译器:     $(gcc --version 2>/dev/null | head -1 || echo 'N/A')"
    echo "  CMake:      $(cmake --version 2>/dev/null | head -1 || echo 'N/A')"
    echo "  Boost:      $(dpkg -l libboost-dev 2>/dev/null | grep libboost-dev || rpm -q boost-devel 2>/dev/null || echo 'installed')"
    echo "  OpenSSL:    $(openssl version 2>/dev/null || echo 'N/A')"
    echo "  Protobuf:   $(protoc --version 2>/dev/null || echo 'N/A')"
    echo "  LuaJIT:     $(pkg-config --modversion luajit 2>/dev/null || echo 'N/A')"
    echo "  GTest:      $(find /usr/lib* /usr/local/lib* -name 'libgtest*' 2>/dev/null | head -1 || echo 'N/A')"
    echo "  Ragel:      $(ragel --version 2>/dev/null || echo 'N/A')"
    echo ""
    echo "构建命令:"
    echo "  cd ${BUILD_DIR} && make -j\$(nproc)"
    echo ""
    echo "============================================="
}

# ---- 主流程 ----
main() {
    parse_args "$@"

    echo ""
    echo "╔══════════════════════════════════════════════╗"
    echo "║  zero-framework 环境配置脚本                 ║"
    echo "╚══════════════════════════════════════════════╝"
    echo ""

    if [ "${VERIFY_ONLY}" = true ]; then
        run_verify
        exit 0
    fi

    # 需要 root 检查（依赖安装需要 sudo）
    if [ "${INSTALL_ONLY}" = true ] || [ "${VERIFY_ONLY}" = false ]; then
        if ! sudo -n true 2>/dev/null; then
            log_info "依赖安装需要 sudo 权限"
        fi
    fi

    run_install

    if [ "${INSTALL_ONLY}" = false ]; then
        run_verify
    fi

    print_summary
}

main "$@"
