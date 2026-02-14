# Deploy/Docker 目录设计文档

## 目录结构

```
deploy/docker/
├── Dockerfile       # 多阶段构建配置（构建 + 运行）
└── entrypoint.sh    # 容器启动入口脚本（服务选择器）
```

---

## 1. 设计概述

### 1.1 核心设计思想

**单镜像多服务架构**：使用同一个 Dockerfile 构建出包含多个服务二进制文件的镜像，通过环境变量在运行时决定启动哪个服务。

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    单镜像多服务架构                                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────┐     │
│  │                    Dockerfile（构建阶段）                      │     │
│  │                                                                │     │
│  │   源代码                                                       │     │
│  │      │                                                         │     │
│  │      ▼                                                         │     │
│  │   CMake 编译                                                   │     │
│  │      │                                                         │     │
│  │      ├──► user_server (二进制文件)                             │     │
│  │      │                                                         │     │
│  │      └──► auth_server (二进制文件)                             │     │
│  │                                                                │     │
│  └───────────────────────────────────────────────────────────────┘     │
│                              │                                          │
│                              ▼                                          │
│  ┌───────────────────────────────────────────────────────────────┐     │
│  │                    Docker 镜像（运行时）                        │     │
│  │                                                                │     │
│  │   /app/                                                        │     │
│  │   ├── user_server      ← 用户服务二进制                        │     │
│  │   ├── auth_server      ← 认证服务二进制                        │     │
│  │   ├── entrypoint.sh    ← 服务选择器                            │     │
│  │   └── configs/         ← 配置文件                              │     │
│  │                                                                │     │
│  └───────────────────────────────────────────────────────────────┘     │
│                              │                                          │
│              ┌───────────────┴───────────────┐                          │
│              ▼                               ▼                          │
│  ┌─────────────────────┐       ┌─────────────────────┐                  │
│  │   user-service      │       │   auth-service      │                  │
│  │   容器实例          │       │   容器实例          │                  │
│  │                     │       │                     │                  │
│  │ SERVICE_NAME=user   │       │ SERVICE_NAME=auth   │                  │
│  │       ↓             │       │       ↓             │                  │
│  │ ./user_server       │       │ ./auth_server       │                  │
│  └─────────────────────┘       └─────────────────────┘                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 设计优势

| 优势 | 说明 |
|------|------|
| **镜像复用** | 相同的基础环境和依赖库，减少存储和拉取时间 |
| **一致性** | 所有服务使用完全相同的运行时环境 |
| **简化 CI/CD** | 只需维护一个 Dockerfile，构建一次产出多个服务 |
| **原子更新** | 更新依赖时，所有服务同步更新 |

---

## 2. Dockerfile 详解

### 2.1 多阶段构建结构

```dockerfile
# ============================================
# 阶段 1: 构建阶段（builder）
# ============================================
FROM ubuntu:22.04 AS builder
# ... 安装编译依赖、编译代码

# ============================================
# 阶段 2: 运行时镜像
# ============================================
FROM ubuntu:22.04
# ... 只包含运行时依赖和二进制文件
```

### 2.2 构建阶段详解

```dockerfile
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# ==================== 安装构建依赖 ====================
RUN apt-get update && apt-get install -y \
    # 基础构建工具
    build-essential \
    cmake \
    git \
    pkg-config \
    # gRPC 和 Protobuf
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    # MySQL 客户端库（开发版，含头文件）
    libmysqlclient-dev \
    # Redis 客户端库
    libhiredis-dev \
    # UUID 生成
    uuid-dev \
    # ZooKeeper 客户端库
    libzookeeper-mt-dev \
    # 测试框架
    libgtest-dev \
    libgmock-dev \
    && rm -rf /var/lib/apt/lists/*

# ==================== 复制源码并编译 ====================
WORKDIR /build
COPY . .

# 编译所有目标（包括 user_server 和 auth_server）
RUN mkdir -p build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && make -j$(nproc)
```

**关键点说明：**

| 步骤 | 说明 |
|------|------|
| `AS builder` | 命名构建阶段，方便后续引用 |
| `-dev` 包 | 开发包包含头文件，编译需要但运行时不需要 |
| `make -j$(nproc)` | 使用所有 CPU 核心并行编译 |
| `rm -rf /var/lib/apt/lists/*` | 清理 apt 缓存，减小镜像体积 |

### 2.3 运行时阶段详解

```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# ==================== 只安装运行时依赖 ====================
RUN apt-get update && apt-get install -y \
    # gRPC 运行时（不带 -dev）
    libgrpc++1 \
    # Protobuf 运行时
    libprotobuf23 \
    # MySQL 客户端运行时
    libmysqlclient21 \
    # Redis 客户端运行时
    libhiredis0.14 \
    # UUID 运行时
    libuuid1 \
    # ZooKeeper 运行时
    libzookeeper-mt2 \
    # 调试工具（可选）
    curl \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# ==================== 安装健康检查工具 ====================
RUN curl -fsSL \
    https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/v0.4.24/grpc_health_probe-linux-amd64 \
    -o /usr/local/bin/grpc_health_probe && \
    chmod +x /usr/local/bin/grpc_health_probe

# ==================== 安全配置：非 root 用户 ====================
RUN useradd -m -s /bin/bash appuser

WORKDIR /app

# ==================== 从构建阶段复制二进制文件 ====================
COPY --from=builder /build/build/bin/user_server .
COPY --from=builder /build/build/bin/auth_server .

# ==================== 复制启动脚本和配置 ====================
COPY deploy/docker/entrypoint.sh .
RUN chmod +x entrypoint.sh

COPY configs/ /app/configs/

# ==================== 设置权限 ====================
RUN mkdir -p /app/logs && chown -R appuser:appuser /app

USER appuser

# ==================== 端口和健康检查 ====================
EXPOSE 50051 9090

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD grpc_health_probe -addr=:50051 || exit 1

# ==================== 启动配置 ====================
ENV CONFIG_PATH=/app/configs/config.docker.yaml
ENV SERVICE_NAME=user-service

ENTRYPOINT ["./entrypoint.sh"]
```

### 2.4 构建阶段 vs 运行时阶段对比

```
┌─────────────────────────────────────────────────────────────────────────┐
│                   多阶段构建的体积优化                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  构建阶段镜像（builder）                                                │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                                                                  │   │
│  │  Ubuntu Base ........................... ~70MB                   │   │
│  │  + build-essential (gcc, make...) ...... ~200MB                  │   │
│  │  + cmake ............................... ~50MB                   │   │
│  │  + libXXX-dev (头文件+静态库) .......... ~500MB                  │   │
│  │  + 源代码 .............................. ~10MB                   │   │
│  │  + 编译产物 (obj文件) .................. ~200MB                  │   │
│  │  ─────────────────────────────────────────────                   │   │
│  │  总计: ~1GB+ (构建完成后丢弃)                                    │   │
│  │                                                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              │ COPY --from=builder                      │
│                              │ (只复制二进制文件)                        │
│                              ▼                                          │
│  运行时镜像（最终产物）                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                                                                  │   │
│  │  Ubuntu Base ........................... ~70MB                   │   │
│  │  + libXXX (仅.so动态库) ................ ~100MB                  │   │
│  │  + user_server ......................... ~15MB                   │   │
│  │  + auth_server ......................... ~15MB                   │   │
│  │  + grpc_health_probe ................... ~10MB                   │   │
│  │  + configs ............................. ~1KB                    │   │
│  │  ─────────────────────────────────────────────                   │   │
│  │  总计: ~210MB (最终镜像)                                         │   │
│  │                                                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  体积缩减: 1GB+ → 210MB (约 80% 缩减)                                    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. entrypoint.sh 详解

### 3.1 完整脚本

```bash
#!/bin/bash
# deploy/docker/entrypoint.sh
# 
# 容器启动入口脚本
# 根据 SERVICE_NAME 环境变量选择启动对应的服务

set -e  # 遇到错误立即退出

# ==================== 服务选择逻辑 ====================

case "${SERVICE_NAME}" in
    "user-service")
        echo "Starting User Service..."
        exec ./user_server
        ;;
    "auth-service")
        echo "Starting Auth Service..."
        exec ./auth_server
        ;;
    *)
        echo "Error: Unknown SERVICE_NAME: ${SERVICE_NAME}"
        echo "Valid options: user-service, auth-service"
        exit 1
        ;;
esac
```

### 3.2 关键设计说明

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    entrypoint.sh 工作流程                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  容器启动                                                               │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  读取环境变量 SERVICE_NAME                                       │   │
│  │                                                                  │   │
│  │  • docker-compose.yaml 中设置                                    │   │
│  │  • 或 docker run -e SERVICE_NAME=xxx                            │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  case 匹配                                                       │   │
│  │                                                                  │   │
│  │  SERVICE_NAME="user-service"  ──► exec ./user_server            │   │
│  │  SERVICE_NAME="auth-service"  ──► exec ./auth_server            │   │
│  │  其他值                        ──► 报错退出                      │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  exec 替换当前 shell 进程                                        │   │
│  │                                                                  │   │
│  │  • PID 1 直接是服务进程（而非 shell）                            │   │
│  │  • 信号正确传递到服务进程                                        │   │
│  │  • 正确响应 docker stop 的 SIGTERM                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.3 为什么使用 exec

```bash
# ❌ 不使用 exec（不推荐）
./user_server
# 进程树：
# PID 1: /bin/bash entrypoint.sh
# └── PID 2: ./user_server
#
# 问题：
# 1. docker stop 发送 SIGTERM 给 PID 1 (shell)
# 2. shell 可能不转发信号给子进程
# 3. 服务无法优雅关闭

# ✓ 使用 exec（推荐）
exec ./user_server
# 进程树：
# PID 1: ./user_server
#
# 优势：
# 1. docker stop 发送 SIGTERM 直接到服务进程
# 2. 服务可以优雅关闭（保存状态、关闭连接等）
```

---

## 4. 与 docker-compose.yaml 的配合

### 4.1 服务定义对比

```yaml
# docker-compose.yaml

services:
  # ==================== User Service ====================
  user-service:
    build:
      context: .
      dockerfile: deploy/docker/Dockerfile    # 同一个 Dockerfile
    container_name: user-service
    environment:
      - CONFIG_PATH=/app/configs/config.docker.yaml
      - SERVICE_NAME=user-service             # ← 关键：决定启动哪个服务
      - ZK_SERVICE_NAME=user-service          # ZK 注册名
    ports:
      - "50051:50051"                          # 主机端口 50051
    # ...

  # ==================== Auth Service ====================
  auth-service:
    build:
      context: .
      dockerfile: deploy/docker/Dockerfile    # 同一个 Dockerfile
    container_name: auth-service
    environment:
      - CONFIG_PATH=/app/configs/config.docker.yaml
      - SERVICE_NAME=auth-service             # ← 关键：决定启动哪个服务
      - ZK_SERVICE_NAME=auth-service          # ZK 注册名
    ports:
      - "50052:50051"                          # 主机端口 50052（容器内仍是 50051）
    # ...
```

### 4.2 完整配置流程图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                 docker-compose up 启动流程                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  docker-compose.yaml                                                    │
│      │                                                                  │
│      │  定义两个 service:                                               │
│      │  • user-service (SERVICE_NAME=user-service)                      │
│      │  • auth-service (SERVICE_NAME=auth-service)                      │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  docker build（首次或镜像不存在时）                              │   │
│  │                                                                  │   │
│  │  Dockerfile 构建出单一镜像，包含：                               │   │
│  │  • user_server 二进制                                            │   │
│  │  • auth_server 二进制                                            │   │
│  │  • entrypoint.sh                                                 │   │
│  │  • 配置文件                                                      │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      │  两个服务使用同一镜像创建不同容器                                │
│      │                                                                  │
│      ├──────────────────────────┬──────────────────────────┐           │
│      ▼                          ▼                          │           │
│  ┌────────────────┐      ┌────────────────┐                │           │
│  │ user-service   │      │ auth-service   │                │           │
│  │ 容器           │      │ 容器           │                │           │
│  │                │      │                │                │           │
│  │ ENV:           │      │ ENV:           │                │           │
│  │ SERVICE_NAME   │      │ SERVICE_NAME   │                │           │
│  │ =user-service  │      │ =auth-service  │                │           │
│  │                │      │                │                │           │
│  │      │         │      │      │         │                │           │
│  │      ▼         │      │      ▼         │                │           │
│  │ entrypoint.sh  │      │ entrypoint.sh  │                │           │
│  │      │         │      │      │         │                │           │
│  │      ▼         │      │      ▼         │                │           │
│  │ case 匹配      │      │ case 匹配      │                │           │
│  │      │         │      │      │         │                │           │
│  │      ▼         │      │      ▼         │                │           │
│  │ exec           │      │ exec           │                │           │
│  │ ./user_server  │      │ ./auth_server  │                │           │
│  │                │      │                │                │           │
│  │ :50051 ──────► │      │ :50051 ──────► │                │           │
│  └───────│────────┘      └───────│────────┘                │           │
│          │                       │                          │           │
│          ▼                       ▼                          │           │
│     主机:50051              主机:50052                      │           │
│                                                             │           │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.3 环境变量传递链

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     环境变量传递与使用                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  docker-compose.yaml                                                    │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  environment:                                                    │   │
│  │    - SERVICE_NAME=auth-service                                   │   │
│  │    - CONFIG_PATH=/app/configs/config.docker.yaml                │   │
│  │    - ZK_SERVICE_NAME=auth-service                               │   │
│  │    - ZK_HOSTS=zookeeper:2181                                    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  entrypoint.sh 使用                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  case "${SERVICE_NAME}" in                                       │   │
│  │      "auth-service")                                             │   │
│  │          exec ./auth_server  ← 选择启动 auth_server              │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  C++ 代码使用                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  // config.cpp                                                   │   │
│  │  void Config::LoadFromEnv() {                                    │   │
│  │      if (auto env = getenv("ZK_SERVICE_NAME")) {                │   │
│  │          zookeeper.service_name = env;  // = "auth-service"     │   │
│  │      }                                                           │   │
│  │      if (auto env = getenv("ZK_HOSTS")) {                       │   │
│  │          zookeeper.hosts = env;  // = "zookeeper:2181"          │   │
│  │      }                                                           │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  ZooKeeper 注册                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  /services                                                       │   │
│  │  └── /auth-service          ← 使用 ZK_SERVICE_NAME 注册         │   │
│  │      └── /172.18.0.6:50051                                      │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 5. 健康检查机制

### 5.1 Dockerfile 中的健康检查

```dockerfile
# 安装 gRPC 健康检查工具
RUN curl -fsSL \
    https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/v0.4.24/grpc_health_probe-linux-amd64 \
    -o /usr/local/bin/grpc_health_probe && \
    chmod +x /usr/local/bin/grpc_health_probe

# 配置健康检查
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD grpc_health_probe -addr=:50051 || exit 1
```

### 5.2 健康检查参数说明

| 参数 | 值 | 说明 |
|------|------|------|
| `--interval` | 30s | 每 30 秒检查一次 |
| `--timeout` | 5s | 单次检查超时时间 |
| `--start-period` | 10s | 启动后 10 秒内不计入失败 |
| `--retries` | 3 | 连续失败 3 次判定为不健康 |

### 5.3 docker-compose 中的依赖检查

```yaml
user-service:
  depends_on:
    mysql:
      condition: service_healthy     # 等待 MySQL 健康
    redis:
      condition: service_healthy     # 等待 Redis 健康
    zookeeper:
      condition: service_healthy     # 等待 ZK 健康
```

---

## 6. 安全最佳实践

### 6.1 非 root 用户运行

```dockerfile
# 创建专用用户
RUN useradd -m -s /bin/bash appuser

# 设置文件权限
RUN chown -R appuser:appuser /app

# 切换到非 root 用户
USER appuser
```

**安全意义：**

- 即使应用被攻破，攻击者也没有 root 权限
- 符合最小权限原则
- 满足大多数安全合规要求

### 6.2 最小化镜像内容

```dockerfile
# 只安装运行时必需的库
RUN apt-get install -y \
    libgrpc++1 \          # 只要 .so，不要 -dev
    libprotobuf23 \
    libmysqlclient21 \
    # ...
    && rm -rf /var/lib/apt/lists/*  # 清理缓存
```

---

## 7. 常用命令

### 7.1 构建镜像

```bash
# 构建镜像
docker build -t user-service:latest -f deploy/docker/Dockerfile .

# 带构建参数
docker build \
    --build-arg BUILD_TYPE=Debug \
    -t user-service:dev \
    -f deploy/docker/Dockerfile .
```

### 7.2 运行容器

```bash
# 运行 user-service
docker run -d \
    --name user-service \
    -e SERVICE_NAME=user-service \
    -e CONFIG_PATH=/app/configs/config.docker.yaml \
    -p 50051:50051 \
    user-service:latest

# 运行 auth-service
docker run -d \
    --name auth-service \
    -e SERVICE_NAME=auth-service \
    -e CONFIG_PATH=/app/configs/config.docker.yaml \
    -p 50052:50051 \
    user-service:latest  # 同一镜像
```

### 7.3 调试命令

```bash
# 进入运行中的容器
docker exec -it user-service /bin/bash

# 查看日志
docker logs -f user-service

# 检查健康状态
docker inspect --format='{{.State.Health.Status}}' user-service

# 手动健康检查
docker exec user-service grpc_health_probe -addr=:50051
```

### 7.4 docker-compose 命令

```bash
# 构建并启动所有服务
docker-compose up -d --build

# 只启动指定服务
docker-compose up -d user-service

# 查看服务状态
docker-compose ps

# 查看服务日志
docker-compose logs -f user-service auth-service

# 停止并删除所有容器
docker-compose down

# 停止并删除（包括数据卷）
docker-compose down -v
```

---

## 8. 扩展：添加新服务

### 8.1 步骤概览

```
1. 编写新服务代码 (src/xxx_server/main.cpp)
2. 更新 CMakeLists.txt 添加新的构建目标
3. 更新 Dockerfile 复制新的二进制文件
4. 更新 entrypoint.sh 添加新的 case 分支
5. 更新 docker-compose.yaml 添加新服务定义
```

### 8.2 示例：添加 gateway-service

```dockerfile
# Dockerfile 修改
COPY --from=builder /build/build/bin/user_server .
COPY --from=builder /build/build/bin/auth_server .
COPY --from=builder /build/build/bin/gateway_server .  # 新增
```

```bash
# entrypoint.sh 修改
case "${SERVICE_NAME}" in
    "user-service")
        exec ./user_server
        ;;
    "auth-service")
        exec ./auth_server
        ;;
    "gateway-service")    # 新增
        exec ./gateway_server
        ;;
    *)
        echo "Unknown SERVICE_NAME: ${SERVICE_NAME}"
        exit 1
        ;;
esac
```

```yaml
# docker-compose.yaml 新增
gateway-service:
  build:
    context: .
    dockerfile: deploy/docker/Dockerfile
  environment:
    - SERVICE_NAME=gateway-service
    - CONFIG_PATH=/app/configs/config.docker.yaml
  ports:
    - "8080:8080"
  depends_on:
    user-service:
      condition: service_healthy
    auth-service:
      condition: service_healthy
```

---

## 9. 相关文件

| 文件 | 说明 |
|------|------|
| `deploy/docker/Dockerfile` | 多阶段构建配置 |
| `deploy/docker/entrypoint.sh` | 服务选择启动脚本 |
| `docker-compose.yaml` | 服务编排配置 |
| `configs/config.docker.yaml` | 容器内运行配置 |
| `CMakeLists.txt` | 编译配置（定义构建目标） |

