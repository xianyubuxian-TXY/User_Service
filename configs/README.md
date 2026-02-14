# Configs 目录设计文档

## 目录结构

```
configs/
├── config.yaml          # 本地开发配置（Docker 辅助开发模式）
└── config.docker.yaml   # 容器内运行配置（全容器模式）
```

---

## 1. 配置文件概述

### 1.1 双配置文件设计

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        配置文件适用场景                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  config.yaml（本地开发）                                         │   │
│  │                                                                  │   │
│  │  ┌──────────────┐         ┌─────────────────────────────┐       │   │
│  │  │   宿主机      │         │      Docker 容器             │       │   │
│  │  │              │         │                             │       │   │
│  │  │  [你的代码]  │ ──────► │  MySQL  (3307 → 3306)       │       │   │
│  │  │  localhost   │         │  Redis  (6380 → 6379)       │       │   │
│  │  │              │         │  ZK     (2181 → 2181)       │       │   │
│  │  └──────────────┘         └─────────────────────────────┘       │   │
│  │                                                                  │   │
│  │  特点：代码在宿主机运行，通过端口映射访问 Docker 中的服务         │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  config.docker.yaml（全容器模式）                                │   │
│  │                                                                  │   │
│  │  ┌─────────────────────────────────────────────────────────┐    │   │
│  │  │                   Docker Network                         │    │   │
│  │  │                                                          │    │   │
│  │  │  [user-service]  ──► mysql:3306                          │    │   │
│  │  │  [auth-service]  ──► redis:6379                          │    │   │
│  │  │                  ──► zookeeper:2181                       │    │   │
│  │  │                                                          │    │   │
│  │  └─────────────────────────────────────────────────────────┘    │   │
│  │                                                                  │   │
│  │  特点：所有服务都在容器内，通过容器名互相访问                    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. config.yaml 详解

### 2.1 用途

**本地开发配置**，适用于"Docker 辅助开发"模式：

- 代码在宿主机编译运行（方便调试、热重载）
- MySQL、Redis、ZooKeeper 在 Docker 容器中运行
- 通过端口映射访问容器内服务

### 2.2 关键配置

```yaml
# 服务连接地址：localhost + 映射端口
mysql:
  host: "localhost"
  port: 3307              # Docker 映射：3307 → 容器内 3306

redis:
  host: "localhost"
  port: 6380              # Docker 映射：6380 → 容器内 6379

zookeeper:
  hosts: "localhost:2181"  # Docker 映射：2181 → 容器内 2181
```

### 2.3 配置模块说明

| 模块 | 说明 |
|------|------|
| `server` | gRPC 服务监听配置 |
| `mysql` | MySQL 连接池、超时、重试配置 |
| `redis` | Redis 连接池、超时配置 |
| `zookeeper` | 服务注册与发现配置 |
| `security` | JWT 密钥、Token 有效期配置 |
| `sms` | 短信验证码配置（长度、有效期、限流） |
| `login` | 登录安全策略（失败锁定、会话管理） |
| `password` | 密码策略（长度、复杂度要求） |
| `log` | 日志配置（级别、路径、轮转） |

### 2.4 使用方式

```bash
# 方式1：默认路径（代码中硬编码）
./user_service_server

# 方式2：命令行指定
./user_service_server --config /path/to/config.yaml

# 方式3：环境变量指定
export CONFIG_PATH=/path/to/config.yaml
./user_service_server
```

---

## 3. config.docker.yaml 详解

### 3.1 用途

**容器内运行配置**，适用于"全容器模式"：

- 所有服务（user-service、auth-service、MySQL、Redis、ZK）都在 Docker 容器中
- 服务间通过 Docker 网络和容器名互相访问
- 通过环境变量实现配置的动态覆盖

### 3.2 关键配置差异

```yaml
# 与 config.yaml 的核心差异：使用容器名而非 localhost

server:
  host: "0.0.0.0"         # 容器内必须绑定 0.0.0.0，否则无法被外部访问

database:
  host: "mysql"           # 使用容器名，Docker DNS 自动解析
  port: 3306              # 容器内部端口，无需映射

redis:
  host: "redis"           # 使用容器名
  port: 6379              # 容器内部端口

zookeeper:
  hosts: "zookeeper:2181" # 使用容器名
```

### 3.3 环境变量覆盖机制

config.docker.yaml 设计为**基础模板**，具体值由环境变量覆盖：

```yaml
# config.docker.yaml 中的值是默认值
database:
  host: "mysql"           # 会被环境变量 MYSQL_HOST 覆盖

zookeeper:
  service_name: "user-service"  # 会被环境变量 ZK_SERVICE_NAME 覆盖
```

---

## 4. Docker Compose 多服务适配机制

### 4.1 架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    docker-compose.yaml 服务编排                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌────────────────────────────────────────────────────────────────┐    │
│  │                      共享资源                                   │    │
│  │                                                                 │    │
│  │  ┌──────────┐    ┌──────────┐    ┌──────────┐                  │    │
│  │  │  mysql   │    │  redis   │    │zookeeper │                  │    │
│  │  │  :3306   │    │  :6379   │    │  :2181   │                  │    │
│  │  └──────────┘    └──────────┘    └──────────┘                  │    │
│  │       ▲               ▲               ▲                         │    │
│  └───────┼───────────────┼───────────────┼─────────────────────────┘    │
│          │               │               │                              │
│          │    Docker Network (user-network)                             │
│          │               │               │                              │
│  ┌───────┼───────────────┼───────────────┼─────────────────────────┐    │
│  │       │               │               │                         │    │
│  │  ┌────┴────┐     ┌────┴────┐                                    │    │
│  │  │  user   │     │  auth   │         应用服务                   │    │
│  │  │ service │     │ service │                                    │    │
│  │  │ :50051  │     │ :50051  │         （同一镜像，不同配置）      │    │
│  │  └─────────┘     └─────────┘                                    │    │
│  │       │               │                                         │    │
│  │       ▼               ▼                                         │    │
│  │  ┌─────────────────────────────────────────────────────────┐   │    │
│  │  │           config.docker.yaml（共享配置文件）             │   │    │
│  │  │                                                          │   │    │
│  │  │  + 环境变量覆盖 = 最终配置                               │   │    │
│  │  └─────────────────────────────────────────────────────────┘   │    │
│  │                                                                 │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.2 同一镜像、不同服务的实现

**核心思路：** 同一个 Dockerfile 构建的镜像，通过环境变量区分不同服务。

#### docker-compose.yaml 关键配置对比

```yaml
# user-service 配置
user-service:
  build:
    dockerfile: deploy/docker/Dockerfile    # 同一个 Dockerfile
  environment:
    - CONFIG_PATH=/app/configs/config.docker.yaml  # 同一个配置文件
    - SERVICE_NAME=user-service             # ← 区分点1：服务名
    - ZK_SERVICE_NAME=user-service          # ← 区分点2：ZK 注册名
  ports:
    - "50051:50051"                          # ← 区分点3：端口映射

# auth-service 配置
auth-service:
  build:
    dockerfile: deploy/docker/Dockerfile    # 同一个 Dockerfile
  environment:
    - CONFIG_PATH=/app/configs/config.docker.yaml  # 同一个配置文件
    - SERVICE_NAME=auth-service             # ← 不同的服务名
    - ZK_SERVICE_NAME=auth-service          # ← 不同的 ZK 注册名
  ports:
    - "50052:50051"                          # ← 不同的主机端口
```

### 4.3 环境变量覆盖流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      配置加载优先级（低 → 高）                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Step 1: 加载 config.docker.yaml 基础配置                        │   │
│  │                                                                  │   │
│  │  zookeeper:                                                      │   │
│  │    hosts: "zookeeper:2181"     # 默认值                          │   │
│  │    service_name: "user-service" # 默认值                         │   │
│  │    register_self: true                                           │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Step 2: 环境变量覆盖                                            │   │
│  │                                                                  │   │
│  │  // C++ 代码中的实现                                             │   │
│  │  void Config::LoadFromEnv() {                                    │   │
│  │      if (auto env = getenv("ZK_SERVICE_NAME")) {                │   │
│  │          zookeeper.service_name = env;  // 覆盖配置文件的值      │   │
│  │      }                                                           │   │
│  │      if (auto env = getenv("ZK_HOSTS")) {                       │   │
│  │          zookeeper.hosts = env;                                  │   │
│  │      }                                                           │   │
│  │      // ... 其他环境变量                                         │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Step 3: 最终配置                                                │   │
│  │                                                                  │   │
│  │  user-service 的最终配置：                auth-service 的最终配置：│  │
│  │  zookeeper:                              zookeeper:              │   │
│  │    hosts: "zookeeper:2181"                hosts: "zookeeper:2181"│   │
│  │    service_name: "user-service" ←        service_name: "auth-service"│
│  │                                                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.4 支持的环境变量清单

| 环境变量 | 覆盖的配置项 | 说明 |
|----------|--------------|------|
| `CONFIG_PATH` | - | 配置文件路径 |
| `SERVICE_NAME` | - | 服务标识（启动脚本使用） |
| `MYSQL_HOST` | `mysql.host` | MySQL 主机地址 |
| `MYSQL_PASSWORD` | `mysql.password` | MySQL 密码 |
| `REDIS_HOST` | `redis.host` | Redis 主机地址 |
| `ZK_HOSTS` | `zookeeper.hosts` | ZooKeeper 地址 |
| `ZK_ROOT_PATH` | `zookeeper.root_path` | 服务注册根路径 |
| `ZK_SERVICE_NAME` | `zookeeper.service_name` | 当前服务名 |
| `ZK_ENABLED` | `zookeeper.enabled` | 是否启用 ZK |
| `ZK_REGISTER_SELF` | `zookeeper.register_self` | 是否注册自身 |
| `JWT_SECRET` | `security.jwt_secret` | JWT 签名密钥 |

### 4.5 entrypoint.sh 服务选择逻辑

```bash
#!/bin/bash
# deploy/docker/entrypoint.sh

# 根据 SERVICE_NAME 环境变量决定启动哪个服务
case "${SERVICE_NAME}" in
    "user-service")
        exec ./user_server
        ;;
    "auth-service")
        exec ./auth_server
        ;;
    *)
        echo "Unknown SERVICE_NAME: ${SERVICE_NAME}"
        exit 1
        ;;
esac
```

---

## 5. ZooKeeper 服务注册适配

### 5.1 多服务注册示意

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     ZooKeeper 服务注册结构                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  /services                                    ← root_path               │
│  │                                                                      │
│  ├── /user-service                           ← service_name (持久节点)  │
│  │   └── /172.18.0.5:50051                   ← 实例节点 (临时节点)      │
│  │       {                                                              │
│  │         "service_name": "user-service",                              │
│  │         "host": "172.18.0.5",                                        │
│  │         "port": 50051,                                               │
│  │         "weight": 100                                                │
│  │       }                                                              │
│  │                                                                      │
│  └── /auth-service                           ← service_name (持久节点)  │
│      └── /172.18.0.6:50051                   ← 实例节点 (临时节点)      │
│          {                                                              │
│            "service_name": "auth-service",                              │
│            "host": "172.18.0.6",                                        │
│            "port": 50051,                                               │
│            "weight": 100                                                │
│          }                                                              │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.2 配置与环境变量的协作

```yaml
# config.docker.yaml 中的默认值
zookeeper:
  hosts: "zookeeper:2181"
  root_path: "/services"
  service_name: "user-service"   # 默认值，会被环境变量覆盖
  register_self: true
```

```yaml
# docker-compose.yaml 中的环境变量覆盖
auth-service:
  environment:
    - ZK_SERVICE_NAME=auth-service  # 覆盖为 auth-service
```

---

## 6. 配置最佳实践

### 6.1 敏感信息处理

```yaml
# ❌ 错误：敏感信息写在配置文件中
security:
  jwt_secret: "my-super-secret-key-123"

mysql:
  password: "root123"
```

```yaml
# ✓ 正确：配置文件使用占位值，实际值通过环境变量传入
security:
  jwt_secret: "placeholder-change-in-production"

# docker-compose.yaml 或 K8s Secret 中设置
environment:
  - JWT_SECRET=${JWT_SECRET_FROM_VAULT}
  - MYSQL_PASSWORD=${MYSQL_PASSWORD_FROM_VAULT}
```

### 6.2 开发与生产配置分离

```
configs/
├── config.yaml           # 本地开发
├── config.docker.yaml    # Docker Compose 部署
├── config.staging.yaml   # 预发布环境（可选）
└── config.prod.yaml      # 生产环境（可选，建议用环境变量）
```

### 6.3 配置校验

程序启动时会自动校验配置合法性（`ValidateConfig` 函数）：

```cpp
void ValidateConfig(const Config& config) {
    // 校验端口合法性
    if (config.server.grpc_port <= 0 || config.server.grpc_port > 65535) {
        throw std::runtime_error("Invalid gRPC port");
    }
    
    // 校验必填项
    if (config.security.jwt_secret.empty()) {
        throw std::runtime_error("JWT secret is empty");
    }
    
    // 校验逻辑关系
    if (config.security.refresh_token_ttl_seconds <= 
        config.security.access_token_ttl_seconds) {
        throw std::runtime_error("Refresh token TTL should be greater than access token TTL");
    }
    
    // ... 更多校验
}
```

---

## 7. 快速开始

### 7.1 本地开发（Docker 辅助）

```bash
# 1. 启动基础服务
docker compose up -d mysql redis zookeeper

# 2. 编译并运行（使用默认配置 config.yaml）
cd build
cmake .. && make -j$(nproc)
./bin/user_service_server
```

### 7.2 全容器模式

```bash
# 一键启动所有服务
docker compose up -d

# 查看服务状态
docker compose ps

# 查看日志
docker compose logs -f user-service
docker compose logs -f auth-service
```

### 7.3 单独启动指定服务

```bash
# 只启动 user-service
docker compose up -d mysql redis zookeeper user-service

# 只启动 auth-service
docker compose up -d mysql redis zookeeper auth-service
```

---

## 8. 配置项速查表

### 8.1 核心配置项

| 配置路径 | 类型 | 默认值 | 说明 |
|----------|------|--------|------|
| `server.grpc_port` | int | 50051 | gRPC 服务端口 |
| `mysql.host` | string | localhost | MySQL 地址 |
| `mysql.pool_size` | int | 10 | 连接池大小 |
| `redis.host` | string | localhost | Redis 地址 |
| `zookeeper.enabled` | bool | true | 是否启用服务发现 |
| `security.jwt_secret` | string | - | JWT 签名密钥（必填） |
| `security.access_token_ttl_seconds` | int | 900 | Access Token 有效期 |
| `security.refresh_token_ttl_seconds` | int | 604800 | Refresh Token 有效期 |

### 8.2 安全相关配置

| 配置路径 | 默认值 | 说明 |
|----------|--------|------|
| `login.max_failed_attempts` | 5 | 登录失败锁定阈值 |
| `login.lock_duration_seconds` | 1800 | 锁定时长（30分钟） |
| `sms.code_ttl_seconds` | 300 | 验证码有效期（5分钟） |
| `sms.max_retry_count` | 5 | 验证码最大错误次数 |
| `password.min_length` | 8 | 密码最小长度 |

---

## 9. 相关文件

| 文件 | 说明 |
|------|------|
| `configs/config.yaml` | 本地开发配置 |
| `configs/config.docker.yaml` | 容器内运行配置 |
| `docker-compose.yaml` | 服务编排配置 |
| `deploy/docker/Dockerfile` | 容器构建配置 |
| `deploy/docker/entrypoint.sh` | 容器启动脚本 |
| `include/config/config.h` | 配置结构体定义 |
| `src/config/config.cpp` | 配置加载与校验实现 |

