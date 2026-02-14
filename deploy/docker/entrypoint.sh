#!/bin/bash
# ==============================================================================
# deploy/docker/entrypoint.sh
#
# 容器启动入口脚本
# 功能：
#   1. 等待依赖服务就绪（MySQL、Redis、ZooKeeper）
#   2. 根据 SERVICE_NAME 启动对应服务
# ==============================================================================

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ==============================================================================
# 等待服务就绪
# ==============================================================================
wait_for_service() {
    local host=$1
    local port=$2
    local service_name=$3
    local max_attempts=${4:-30}
    local attempt=0

    log_info "Waiting for $service_name ($host:$port)..."

    while [ $attempt -lt $max_attempts ]; do
        if nc -z "$host" "$port" 2>/dev/null; then
            log_info "$service_name is ready!"
            return 0
        fi
        attempt=$((attempt + 1))
        log_warn "$service_name not ready, attempt $attempt/$max_attempts..."
        sleep 2
    done

    log_error "$service_name failed to become ready after $max_attempts attempts"
    return 1
}

# ==============================================================================
# 等待 MySQL 就绪（需要额外检查是否可以连接）
# ==============================================================================
wait_for_mysql() {
    local host=${MYSQL_HOST:-mysql}
    local port=${MYSQL_PORT:-3306}
    
    wait_for_service "$host" "$port" "MySQL" 60
}

# ==============================================================================
# 等待 Redis 就绪
# ==============================================================================
wait_for_redis() {
    local host=${REDIS_HOST:-redis}
    local port=${REDIS_PORT:-6379}
    
    wait_for_service "$host" "$port" "Redis" 30
}

# ==============================================================================
# 等待 ZooKeeper 就绪
# ==============================================================================
wait_for_zookeeper() {
    local hosts=${ZK_HOSTS:-zookeeper:2181}
    
    # 解析第一个 ZK 地址
    local first_host=$(echo "$hosts" | cut -d',' -f1)
    local zk_host=$(echo "$first_host" | cut -d':' -f1)
    local zk_port=$(echo "$first_host" | cut -d':' -f2)
    
    if [ -z "$zk_port" ]; then
        zk_port=2181
    fi
    
    wait_for_service "$zk_host" "$zk_port" "ZooKeeper" 30
}

# ==============================================================================
# 主逻辑
# ==============================================================================
main() {
    log_info "=========================================="
    log_info "Starting $SERVICE_NAME"
    log_info "Config: $CONFIG_PATH"
    log_info "=========================================="

    # 等待依赖服务
    wait_for_mysql
    wait_for_redis
    
    # 如果启用了 ZooKeeper，等待它就绪
    if [ "${ZK_ENABLED:-true}" = "true" ]; then
        wait_for_zookeeper
    fi

    log_info "All dependencies are ready!"

    # 根据服务名选择启动的可执行文件
    case "$SERVICE_NAME" in
        "user-service"|"user")
            log_info "Starting User Service..."
            exec ./user_server
            ;;
        "auth-service"|"auth")
            log_info "Starting Auth Service..."
            exec ./auth_server
            ;;
        *)
            log_error "Unknown SERVICE_NAME: $SERVICE_NAME"
            log_error "Valid values: user-service, auth-service"
            exit 1
            ;;
    esac
}

# 执行主函数
main "$@"
