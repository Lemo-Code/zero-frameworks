#!/usr/bin/env bash
# ==============================================================================
# docker.sh — 集成测试环境管理 (MySQL + etcd)
# ==============================================================================
# 用法:
#   ./scripts/docker.sh start     # 启动 MySQL + etcd
#   ./scripts/docker.sh stop      # 停止所有容器
#   ./scripts/docker.sh status    # 查看状态
#   ./scripts/docker.sh clean     # 停止并删除容器+数据
# ==============================================================================
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }

# ---- 配置 ----
MYSQL_CONTAINER="zero-mysql"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_ROOT_PASS="${MYSQL_ROOT_PASS:-root123}"
MYSQL_DB="${MYSQL_DB:-zero_test}"
MYSQL_USER="${MYSQL_USER:-zero}"
MYSQL_PASS="${MYSQL_PASS:-zero123}"

ETCD_CONTAINER="zero-etcd"
ETCD_PORT="${ETCD_PORT:-2379}"
ETCD_PEER_PORT="${ETCD_PEER_PORT:-2380}"

# ---- 检查 docker ----
check_docker() {
    if ! command -v docker &>/dev/null; then
        log_err "Docker 未安装，请先安装 Docker"
        exit 1
    fi
    if ! docker info >/dev/null 2>&1; then
        log_err "Docker 未运行或权限不足"
        exit 1
    fi
}

# ---- 启动 MySQL ----
start_mysql() {
    if docker ps --format '{{.Names}}' | grep -q "^${MYSQL_CONTAINER}$"; then
        log_info "MySQL 已在运行 → localhost:${MYSQL_PORT}"
        return 0
    fi

    if docker ps -a --format '{{.Names}}' | grep -q "^${MYSQL_CONTAINER}$"; then
        log_info "启动已存在的 MySQL 容器..."
        docker start "${MYSQL_CONTAINER}" > /dev/null
    else
        log_info "创建并启动 MySQL 容器..."
        docker run -d \
            --name "${MYSQL_CONTAINER}" \
            -p "${MYSQL_PORT}:3306" \
            -e "MYSQL_ROOT_PASSWORD=${MYSQL_ROOT_PASS}" \
            -e "MYSQL_DATABASE=${MYSQL_DB}" \
            -e "MYSQL_USER=${MYSQL_USER}" \
            -e "MYSQL_PASSWORD=${MYSQL_PASS}" \
            mysql:8.0 \
            --default-authentication-plugin=mysql_native_password \
            > /dev/null
    fi

    # 等待就绪
    log_info "等待 MySQL 就绪..."
    for i in $(seq 1 30); do
        if docker exec "${MYSQL_CONTAINER}" mysqladmin ping -h localhost --silent 2>/dev/null; then
            break
        fi
        sleep 1
    done

    if docker exec "${MYSQL_CONTAINER}" mysqladmin ping -h localhost --silent 2>/dev/null; then
        log_ok "MySQL 就绪 → localhost:${MYSQL_PORT}  db=${MYSQL_DB}  user=${MYSQL_USER}"
    else
        log_warn "MySQL 可能尚未完全就绪，请稍等几秒"
    fi
}

# ---- 启动 etcd ----
start_etcd() {
    if docker ps --format '{{.Names}}' | grep -q "^${ETCD_CONTAINER}$"; then
        log_info "etcd 已在运行 → localhost:${ETCD_PORT}"
        return 0
    fi

    if docker ps -a --format '{{.Names}}' | grep -q "^${ETCD_CONTAINER}$"; then
        log_info "启动已存在的 etcd 容器..."
        docker start "${ETCD_CONTAINER}" > /dev/null
    else
        log_info "创建并启动 etcd 容器..."
        docker run -d \
            --name "${ETCD_CONTAINER}" \
            -p "${ETCD_PORT}:2379" \
            -p "${ETCD_PEER_PORT}:2380" \
            -e "ALLOW_NONE_AUTHENTICATION=yes" \
            bitnami/etcd:latest \
            > /dev/null
    fi

    # 等待就绪
    log_info "等待 etcd 就绪..."
    for i in $(seq 1 20); do
        if curl -s "http://localhost:${ETCD_PORT}/health" 2>/dev/null | grep -q "true"; then
            break
        fi
        sleep 1
    done
    log_ok "etcd 就绪 → localhost:${ETCD_PORT}"
}

# ---- 停止 ----
stop_all() {
    log_info "停止容器..."
    for c in "${MYSQL_CONTAINER}" "${ETCD_CONTAINER}"; do
        if docker ps --format '{{.Names}}' | grep -q "^${c}$"; then
            docker stop "${c}" > /dev/null && log_ok "已停止: ${c}"
        fi
    done
}

# ---- 清理 ----
clean_all() {
    log_info "清理容器和数据..."
    for c in "${MYSQL_CONTAINER}" "${ETCD_CONTAINER}"; do
        if docker ps -a --format '{{.Names}}' | grep -q "^${c}$"; then
            docker stop "${c}" 2>/dev/null || true
            docker rm "${c}" > /dev/null && log_ok "已删除: ${c}"
        fi
    done
}

# ---- 状态 ----
show_status() {
    echo ""
    echo "╔══════════════════════════════════════════════╗"
    echo "║  集成测试容器状态                            ║"
    echo "╚══════════════════════════════════════════════╝"
    echo ""

    for c in "${MYSQL_CONTAINER}" "${ETCD_CONTAINER}"; do
        if docker ps --format '{{.Names}}' | grep -q "^${c}$"; then
            local port=$(docker port "${c}" 2>/dev/null | head -1)
            echo -e "  ${GREEN}●${NC} ${c}  运行中  ${port}"
        elif docker ps -a --format '{{.Names}}' | grep -q "^${c}$"; then
            echo -e "  ${YELLOW}○${NC} ${c}  已停止"
        else
            echo -e "  ${RED}✗${NC} ${c}  未创建"
        fi
    done

    echo ""
    echo "连接信息:"
    echo "  MySQL:  mysql -h 127.0.0.1 -P ${MYSQL_PORT} -u ${MYSQL_USER} -p${MYSQL_PASS} ${MYSQL_DB}"
    echo "  etcd:   curl http://127.0.0.1:${ETCD_PORT}/version"
    echo ""
}

# ---- 主流程 ----
main() {
    check_docker

    case "${1:-}" in
        start)
            echo ""
            echo "╔══════════════════════════════════════════════╗"
            echo "║  启动集成测试环境                            ║"
            echo "╚══════════════════════════════════════════════╝"
            echo ""
            start_mysql
            start_etcd
            echo ""
            log_ok "环境就绪，可运行集成测试:"
            echo "  ./scripts/run_tests.sh --category integration"
            ;;

        stop)
            stop_all
            ;;

        status)
            show_status
            ;;

        clean)
            clean_all
            log_ok "清理完成"
            ;;

        -h|--help|*)
            cat << EOF
zero-framework 集成测试环境管理

用法: $0 <命令>

命令:
  start     启动 MySQL + etcd 容器
  stop      停止容器 (保留数据)
  status    查看容器状态
  clean     停止并删除容器+数据

环境变量:
  MYSQL_PORT=3306          MySQL 端口
  MYSQL_ROOT_PASS=root123  MySQL root 密码
  MYSQL_DB=zero_test       测试数据库名
  MYSQL_USER=zero           数据库用户
  MYSQL_PASS=zero123        数据库密码
  ETCD_PORT=2379            etcd 客户端端口

示例:
  ./scripts/docker.sh start
  MYSQL_PORT=3307 ./scripts/docker.sh start
  ./scripts/docker.sh status
EOF
            exit 0
            ;;
    esac
}

main "$@"
