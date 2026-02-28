#!/bin/bash
set -e

log_info() { echo -e "\033[0;32m[INFO]\033[0m $1"; }
log_warn() { echo -e "\033[1;33m[WARN]\033[0m $1"; }
log_error() { echo -e "\033[0;31m[ERROR]\033[0m $1"; }

wait_for_service() {
    local host=$1 port=$2 name=$3 max=30 attempt=0
    log_info "Waiting for $name ($host:$port)..."
    while [ $attempt -lt $max ]; do
        nc -z "$host" "$port" 2>/dev/null && { log_info "$name ready!"; return 0; }
        attempt=$((attempt + 1))
        sleep 2
    done
    log_error "$name not ready after $max attempts"
    return 1
}

main() {
    log_info "=========================================="
    log_info "Starting $SERVICE_NAME"
    log_info "=========================================="

    wait_for_service "${MYSQL_HOST:-mysql}" "${MYSQL_PORT:-3306}" "MySQL"
    wait_for_service "${REDIS_HOST:-redis}" "${REDIS_PORT:-6379}" "Redis"
    
    [ "${ZK_ENABLED:-true}" = "true" ] && \
        wait_for_service "$(echo ${ZK_HOSTS:-zookeeper:2181} | cut -d: -f1)" \
                         "$(echo ${ZK_HOSTS:-zookeeper:2181} | cut -d: -f2)" "ZooKeeper"

    case "$SERVICE_NAME" in
        "user-service"|"user") exec ./user_server ;;
        "auth-service"|"auth") exec ./auth_server ;;
        *) log_error "Unknown SERVICE_NAME: $SERVICE_NAME"; exit 1 ;;
    esac
}

main "$@"
