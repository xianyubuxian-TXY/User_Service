# User Service - C++ 微服务用户系统

一个基于 **gRPC + C++23** 构建的高性能用户认证与管理微服务系统，支持服务注册发现、JWT 双令牌认证、短信验证码等特性。

---

## 📑 目录

- [项目概述](#项目概述)
- [技术架构](#技术架构)
- [项目结构](#项目结构)
- [快速开始](#快速开始)
- [详细使用指南](#详细使用指南)
  - [Server 模块详解](#server-模块详解)
  - [ServerBuilder 使用指南](#serverbuilder-使用指南)
  - [客户端使用](#客户端使用)
- [配置说明](#配置说明)
- [API 参考](#api-参考)
- [部署指南](#部署指南)
- [待实现与优化](#待实现与优化)
- [贡献指南](#贡献指南)

---

## 项目概述

### 功能特性

| 模块 | 功能 |
|------|------|
| **认证服务** | 短信验证码、用户注册、密码/验证码登录、JWT双令牌、Token刷新、登出 |
| **用户服务** | 用户信息查询/更新、密码修改、账号注销、管理员用户管理 |
| **基础设施** | MySQL连接池、Redis缓存、ZooKeeper服务发现、异步日志 |

### 系统架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Client (gRPC)                                  │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           ZooKeeper (服务发现)                               │
│   /services/auth-service/192.168.1.10:50051                                 │
│   /services/user-service/192.168.1.10:50052                                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
              ┌───────────────────────┼───────────────────────┐
              ▼                       ▼                       ▼
┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐
│    Auth Service     │  │    User Service     │  │   Other Services    │
│      (50051)        │  │      (50052)        │  │        ...          │
├─────────────────────┤  ├─────────────────────┤  └─────────────────────┘
│  AuthHandler        │  │  UserHandler        │
│  AuthService        │  │  UserService        │
│  JwtService         │  │  Authenticator      │
│  SmsService         │  └─────────────────────┘
└─────────────────────┘
              │                       │
              └───────────┬───────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Infrastructure Layer                                 │
├──────────────────────┬──────────────────────┬───────────────────────────────┤
│      MySQL Pool      │    Redis Client      │        Token Repository       │
│  (用户数据、会话)     │  (验证码、限流缓存)   │        (令牌存储)              │
└──────────────────────┴──────────────────────┴───────────────────────────────┘
```

---

## 技术架构

### 技术栈

| 类别 | 技术 | 版本 |
|------|------|------|
| 语言 | C++ | 23 |
| RPC框架 | gRPC | 1.50+ |
| 序列化 | Protocol Buffers | 3.x |
| 数据库 | MySQL | 8.0 |
| 缓存 | Redis | 7.x |
| 服务发现 | ZooKeeper | 3.8 |
| 配置 | yaml-cpp | 0.7+ |
| 日志 | spdlog | 1.x |
| 加密 | OpenSSL | 3.x |
| 构建 | CMake | 3.16+ |
| 容器化 | Docker + Docker Compose | - |

### 分层架构

```
┌────────────────────────────────────────────────────────────┐
│                    Handlers (表示层)                        │
│         AuthHandler / UserHandler                          │
│         - gRPC 请求接收与响应                               │
│         - 参数校验与转换                                    │
├────────────────────────────────────────────────────────────┤
│                    Services (业务层)                        │
│         AuthService / UserService                          │
│         - 核心业务逻辑                                      │
│         - 事务协调                                          │
├────────────────────────────────────────────────────────────┤
│                    Auth (认证层)                            │
│         JwtService / SmsService / Authenticator            │
│         - JWT 生成与验证                                    │
│         - 验证码发送与校验                                  │
├────────────────────────────────────────────────────────────┤
│                    Repository (数据层)                      │
│         UserDB / TokenRepository                           │
│         - 数据库 CRUD 操作                                  │
│         - SQL 参数化查询                                    │
├────────────────────────────────────────────────────────────┤
│                    Infrastructure (基础设施)                │
│         MySQLPool / RedisClient / ZooKeeperClient          │
│         - 连接池管理                                        │
│         - 缓存操作                                          │
│         - 服务注册发现                                      │
└────────────────────────────────────────────────────────────┘
```

---

## 项目结构

```
user-service/
├── api/                          # Protobuf 定义
│   └── proto/
│       ├── pb_auth/auth.proto    # 认证服务接口
│       ├── pb_user/user.proto    # 用户服务接口
│       └── pb_common/result.proto # 通用响应结构
│
├── include/                      # 头文件
│   ├── server/                   # ★ 服务器核心
│   │   ├── grpc_server.h         # gRPC 服务器封装
│   │   └── server_builder.h      # 服务器构建器（Builder 模式）
│   ├── handlers/                 # gRPC Handler
│   ├── service/                  # 业务逻辑层
│   ├── auth/                     # 认证模块
│   ├── db/                       # 数据库访问
│   ├── cache/                    # Redis 缓存
│   ├── discovery/                # ZooKeeper 服务发现
│   ├── client/                   # gRPC 客户端
│   ├── pool/                     # 连接池模板
│   ├── config/                   # 配置管理
│   ├── common/                   # 公共工具
│   ├── entity/                   # 数据实体
│   └── exception/                # 异常定义
│
├── src/                          # 源文件
│   ├── server/
│   │   ├── grpc_server.cpp
│   │   ├── server_builder.cpp
│   │   ├── auth_main.cpp         # 认证服务入口
│   │   └── user_main.cpp         # 用户服务入口
│   └── ...
│
├── configs/                      # 配置文件
│   ├── config.yaml               # 本地开发配置
│   └── config.docker.yaml        # Docker 环境配置
│
├── scripts/
│   └── init_db.sql               # 数据库初始化脚本
│
├── deploy/docker/                # Docker 部署文件
├── tests/                        # 单元测试
└── docker-compose.yml            # 容器编排
```

---

## 快速开始

### 环境要求

- Ubuntu 20.04+ / Debian 11+
- CMake 3.16+
- GCC 11+ (支持 C++23)
- Docker & Docker Compose（可选）

### 依赖安装 (Ubuntu)

```bash
# 系统依赖
sudo apt-get update && sudo apt-get install -y \
    build-essential cmake pkg-config \
    libgrpc++-dev libprotobuf-dev \
    protobuf-compiler protobuf-compiler-grpc \
    libmysqlclient-dev libhiredis-dev uuid-dev \
    libzookeeper-mt-dev \
    libyaml-cpp-dev libspdlog-dev libfmt-dev libssl-dev

# redis-plus-plus (需要手动编译)
tar xzf thirdparty/redis-plus-plus-1.3.10.tar.gz
cd redis-plus-plus-1.3.10 && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) && sudo make install && sudo ldconfig
```

### 方式一：Docker Compose（推荐）

```bash
# 一键启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps

# 查看日志
docker-compose logs -f user-service
docker-compose logs -f auth-service
```

服务启动后：
- Auth Service: `localhost:50052`
- User Service: `localhost:50051`
- MySQL: `localhost:3307`
- Redis: `localhost:6380`
- ZooKeeper: `localhost:2181`

### 方式二：本地编译运行

```bash
# 1. 启动依赖服务（MySQL、Redis、ZooKeeper）
docker-compose up -d mysql redis zookeeper

# 2. 编译项目
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 3. 启动服务
# 终端1 - 认证服务
./bin/auth_server

# 终端2 - 用户服务  
./bin/user_server
```

---

## 详细使用指南

### Server 模块详解

#### 核心类关系

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           ServerBuilder                                   │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  - WithConfigFile(path)     // 加载配置文件                          │ │
│  │  - WithConfig(config)       // 使用配置对象                          │ │
│  │  - WithPort(port)           // 设置端口                              │ │
│  │  - WithHost(host)           // 设置主机                              │ │
│  │  - WithServiceName(name)    // 设置服务名（ZK注册）                  │ │
│  │  - EnableServiceDiscovery() // 启用/禁用服务发现                     │ │
│  │  - LoadFromEnvironment()    // 从环境变量加载                        │ │
│  │  - OnShutdown(callback)     // 设置关闭回调                          │ │
│  │  - Build() ──────────────────────────────────────────────────────┐  │ │
│  └──────────────────────────────────────────────────────────────────│──┘ │
└─────────────────────────────────────────────────────────────────────│────┘
                                                                      │
                                                                      ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                            GrpcServer                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  Initialize()                                                        │ │
│  │    ├─ InitInfrastructure()  // MySQL连接池、Redis客户端              │ │
│  │    ├─ InitRepositories()    // UserDB、TokenRepository               │ │
│  │    ├─ InitServices()        // AuthService、UserService、JwtService  │ │
│  │    ├─ InitHandlers()        // AuthHandler、UserHandler              │ │
│  │    └─ InitServiceDiscovery()// ZooKeeper客户端、服务注册             │ │
│  │                                                                       │ │
│  │  Start() / Run()            // 启动gRPC服务器                        │ │
│  │  Shutdown()                 // 优雅关闭                               │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
```

#### GrpcServer 初始化流程

```cpp
// GrpcServer::Initialize() 内部流程
bool GrpcServer::Initialize() {
    // 1. 初始化基础设施
    InitInfrastructure();
    //    └─ 创建 MySQL 连接池 (pool_size 个连接)
    //    └─ 创建 Redis 客户端 (连接池模式)
    //    └─ 验证连接 (Ping)
    
    // 2. 初始化数据访问层
    InitRepositories();
    //    └─ UserDB (用户表 CRUD)
    //    └─ TokenRepository (会话表 CRUD)
    
    // 3. 初始化业务服务
    InitServices();
    //    └─ JwtService (JWT 生成/验证)
    //    └─ SmsService (验证码发送/校验)
    //    └─ AuthService (认证业务逻辑)
    //    └─ UserService (用户业务逻辑)
    //    └─ TokenCleanupTask (后台清理过期Token)
    
    // 4. 初始化 gRPC Handler
    InitHandlers();
    //    └─ JwtAuthenticator (请求认证)
    //    └─ AuthHandler (认证接口)
    //    └─ UserHandler (用户接口)
    
    // 5. 初始化服务发现 (可选)
    if (config_->zookeeper.enabled) {
        InitServiceDiscovery();
        //    └─ ZooKeeperClient (连接 ZK)
        //    └─ ServiceRegistry (服务注册器)
    }
}
```

### ServerBuilder 使用指南

`ServerBuilder` 采用 **Builder 模式**，提供流畅的 API 来配置和创建服务器。

#### 基础用法

```cpp
#include "server/server_builder.h"

int main() {
    // 最简单的用法
    auto server = user_service::ServerBuilder()
        .WithConfigFile("configs/config.yaml")
        .Build();
    
    if (!server->Initialize()) {
        std::cerr << "初始化失败" << std::endl;
        return 1;
    }
    
    server->Run();  // 阻塞直到收到关闭信号
    return 0;
}
```

#### 完整配置示例

```cpp
#include "server/server_builder.h"
#include "common/logger.h"
#include <csignal>

user_service::GrpcServer* g_server = nullptr;

void SignalHandler(int signal) {
    if (g_server) {
        g_server->Shutdown();
    }
}

int main() {
    try {
        // 1. 加载配置
        auto config = user_service::Config::LoadFromFile("configs/config.yaml");
        
        // 2. 初始化日志（可选，ServerBuilder 会自动初始化）
        user_service::Logger::Init(
            config.log.path,
            config.log.filename,
            config.log.level
        );
        
        // 3. 构建服务器
        auto server = user_service::ServerBuilder()
            .WithConfig(std::make_shared<user_service::Config>(config))
            .LoadFromEnvironment()           // 环境变量覆盖配置
            .WithServiceName("auth-service") // ZooKeeper 注册名
            .WithPort(50051)                 // gRPC 端口
            .WithHost("0.0.0.0")            // 监听地址
            .EnableServiceDiscovery(true)    // 启用服务发现
            .OnShutdown([]() {
                LOG_INFO("Server shutting down, cleaning up...");
            })
            .Build();
        
        // 4. 设置信号处理
        g_server = server.get();
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
        
        // 5. 初始化并运行
        if (!server->Initialize()) {
            LOG_ERROR("Server initialization failed");
            return 1;
        }
        
        LOG_INFO("Server starting on port 50051...");
        server->Run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

#### 多服务部署场景

在微服务架构中，通常需要部署多个不同的服务：

```cpp
// ==================== auth_main.cpp ====================
// 认证服务 - 处理登录、注册、Token管理
int main() {
    auto server = user_service::ServerBuilder()
        .WithConfigFile("configs/config.yaml")
        .LoadFromEnvironment()
        .WithServiceName("auth-service")  // ← 服务名
        .WithPort(50051)                  // ← 端口
        .Build();
    
    server->Initialize();
    server->Run();
    // ZooKeeper 注册路径: /services/auth-service/192.168.1.10:50051
}

// ==================== user_main.cpp ====================
// 用户服务 - 处理用户信息管理
int main() {
    auto server = user_service::ServerBuilder()
        .WithConfigFile("configs/config.yaml")
        .LoadFromEnvironment()
        .WithServiceName("user-service")  // ← 不同的服务名
        .WithPort(50052)                  // ← 不同的端口
        .Build();
    
    server->Initialize();
    server->Run();
    // ZooKeeper 注册路径: /services/user-service/192.168.1.10:50052
}
```

#### 配置优先级

配置的加载和覆盖遵循以下优先级（从低到高）：

```
1. 配置文件默认值 (config.yaml)
      ↓
2. 环境变量覆盖 (LoadFromEnvironment)
      ↓
3. Builder 方法覆盖 (WithPort, WithServiceName 等)
```

**示例：**

```yaml
# config.yaml
server:
  grpc_port: 50051   # 默认值
```

```bash
# 环境变量
export GRPC_PORT=50052
```

```cpp
// 代码覆盖
builder.WithPort(50053);  // 最终使用 50053
```

#### Docker/K8s 环境配置

```yaml
# docker-compose.yml
services:
  auth-service:
    environment:
      - CONFIG_PATH=/app/configs/config.docker.yaml
      - SERVICE_NAME=auth-service
      - ZK_HOSTS=zookeeper:2181
      - ZK_ENABLED=true
      - ZK_REGISTER_SELF=true
      - MYSQL_HOST=mysql
      - REDIS_HOST=redis
```

```cpp
// 入口程序
int main() {
    const char* config_path = std::getenv("CONFIG_PATH");
    if (!config_path) {
        config_path = "configs/config.yaml";
    }
    
    auto server = user_service::ServerBuilder()
        .WithConfigFile(config_path)
        .LoadFromEnvironment()  // ← 环境变量会覆盖配置文件
        .Build();
    
    // ...
}
```

#### 支持的环境变量

| 环境变量 | 说明 | 示例 |
|---------|------|------|
| `CONFIG_PATH` | 配置文件路径 | `/app/configs/config.yaml` |
| `SERVICE_NAME` | 服务名称（启动脚本用） | `auth-service` |
| `MYSQL_HOST` | MySQL 主机 | `mysql` |
| `MYSQL_PASSWORD` | MySQL 密码 | `root123` |
| `REDIS_HOST` | Redis 主机 | `redis` |
| `ZK_HOSTS` | ZooKeeper 地址 | `zk1:2181,zk2:2181` |
| `ZK_ROOT_PATH` | 服务根路径 | `/services` |
| `ZK_SERVICE_NAME` | 服务注册名 | `user-service` |
| `ZK_ENABLED` | 启用服务发现 | `true` |
| `ZK_REGISTER_SELF` | 注册自身 | `true` |
| `JWT_SECRET` | JWT 密钥 | `your-secret-key` |

### 客户端使用

#### AuthClient 使用

```cpp
#include "client/auth_client.h"

int main() {
    // 创建客户端
    user_service::AuthClient auth_client("localhost:50051");
    
    // 1. 发送验证码
    auto code_result = auth_client.SendVerifyCode(
        "13800138000", 
        user_service::SmsScene::Register
    );
    if (code_result.IsOk()) {
        std::cout << "验证码已发送，请 " << code_result.Value() 
                  << " 秒后重试" << std::endl;
    }
    
    // 2. 用户注册
    auto reg_result = auth_client.Register(
        "13800138000",      // 手机号
        "123456",           // 验证码（测试环境固定）
        "MyPassword123",    // 密码
        "张三"              // 昵称
    );
    
    if (reg_result.IsOk()) {
        auto& auth = reg_result.Value();
        std::cout << "注册成功!" << std::endl;
        std::cout << "User ID: " << auth.user.uuid << std::endl;
        std::cout << "Access Token: " << auth.tokens.access_token << std::endl;
        std::cout << "Refresh Token: " << auth.tokens.refresh_token << std::endl;
    } else {
        std::cerr << "注册失败: " << reg_result.message << std::endl;
    }
    
    // 3. 密码登录
    auto login_result = auth_client.LoginByPassword(
        "13800138000",
        "MyPassword123"
    );
    
    // 4. 刷新 Token
    if (login_result.IsOk()) {
        auto refresh_result = auth_client.RefreshToken(
            login_result.Value().tokens.refresh_token
        );
    }
    
    // 5. 登出
    auth_client.Logout(login_result.Value().tokens.refresh_token);
    
    return 0;
}
```

#### UserClient 使用

```cpp
#include "client/user_client.h"

int main() {
    user_service::UserClient user_client("localhost:50052");
    
    // ★ 必须设置 Access Token（从登录获取）
    user_client.SetAccessToken("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...");
    
    // 1. 获取当前用户信息
    auto user_result = user_client.GetCurrentUser();
    if (user_result.IsOk()) {
        auto& user = user_result.Value();
        std::cout << "欢迎, " << user.display_name << std::endl;
        std::cout << "手机号: " << user.mobile << std::endl;
    }
    
    // 2. 更新用户信息
    auto update_result = user_client.UpdateUser("新昵称");
    
    // 3. 修改密码
    auto pwd_result = user_client.ChangePassword(
        "OldPassword123",
        "NewPassword456"
    );
    
    // 4. 管理员：获取用户列表
    auto list_result = user_client.ListUsers(
        std::nullopt,     // mobile_filter
        std::nullopt,     // disabled_filter
        1,                // page
        20                // page_size
    );
    
    if (list_result.IsOk()) {
        auto& [users, page_info] = list_result.Value();
        std::cout << "总用户数: " << page_info.total_records << std::endl;
        for (const auto& u : users) {
            std::cout << "  - " << u.display_name << " (" << u.mobile << ")" << std::endl;
        }
    }
    
    return 0;
}
```

#### 使用服务发现的客户端

```cpp
#include "client/auth_client.h"
#include "discovery/service_discovery.h"
#include "discovery/zk_client.h"

int main() {
    // 1. 连接 ZooKeeper
    auto zk_client = std::make_shared<user_service::ZooKeeperClient>(
        "localhost:2181", 15000
    );
    zk_client->Connect();
    
    // 2. 创建服务发现器
    auto discovery = std::make_shared<user_service::ServiceDiscovery>(
        zk_client, "/services"
    );
    
    // 3. 订阅服务变化
    discovery->Subscribe("auth-service", [](const std::string& name) {
        std::cout << "服务 " << name << " 实例发生变化" << std::endl;
    });
    
    // 4. 选择一个实例（负载均衡）
    auto instance = discovery->SelectInstance("auth-service");
    if (!instance) {
        std::cerr << "没有可用的 auth-service 实例" << std::endl;
        return 1;
    }
    
    // 5. 创建客户端
    user_service::AuthClient auth_client(instance->GetAddress());
    
    // 6. 使用客户端...
    auto result = auth_client.LoginByPassword("13800138000", "password");
    
    return 0;
}
```

---

## 配置说明

### 完整配置示例

```yaml
# config.yaml

# ==================== 服务器配置 ====================
server:
  host: "0.0.0.0"          # 监听地址（Docker中必须为0.0.0.0）
  grpc_port: 50051         # gRPC 端口
  metrics_port: 9090       # 监控指标端口

# ==================== MySQL 配置 ====================
mysql:
  host: "localhost"
  port: 3306
  database: "user_service"
  username: "root"
  password: "your_password"
  pool_size: 10                    # 连接池大小
  connection_timeout_ms: 5000      # 连接超时
  read_timeout_ms: 30000           # 读取超时
  write_timeout_ms: 30000          # 写入超时
  max_retries: 3                   # 最大重试次数
  auto_reconnect: true             # 自动重连
  charset: "utf8mb4"               # 字符集

# ==================== Redis 配置 ====================
redis:
  host: "localhost"
  port: 6379
  password: ""
  db: 0
  pool_size: 5
  connect_timeout_ms: 3000
  socket_timeout_ms: 3000

# ==================== ZooKeeper 配置 ====================
zookeeper:
  hosts: "localhost:2181"          # ZK地址，多个用逗号分隔
  session_timeout_ms: 30000        # 会话超时
  connect_timeout_ms: 10000        # 连接超时
  root_path: "/services"           # 服务根路径
  service_name: "user-service"     # 当前服务名
  enabled: true                    # 是否启用
  register_self: true              # 是否注册自身
  weight: 100                      # 负载均衡权重
  region: ""                       # 区域标识
  zone: ""                         # 可用区
  version: "1.0.0"                 # 服务版本

# ==================== 安全配置 ====================
security:
  jwt_secret: "your-super-secret-key-at-least-32-bytes!"
  jwt_issuer: "user-service"
  access_token_ttl_seconds: 900    # 15分钟
  refresh_token_ttl_seconds: 604800 # 7天

# ==================== 验证码配置 ====================
sms:
  code_len: 6                      # 验证码长度
  code_ttl_seconds: 300            # 有效期5分钟
  send_interval_seconds: 60        # 发送间隔
  max_retry_count: 5               # 最大验证错误次数
  retry_ttl_seconds: 300           # 错误计数窗口
  lock_seconds: 1800               # 锁定时长30分钟

# ==================== 登录安全配置 ====================
login:
  max_failed_attempts: 5           # 最大失败次数
  failed_attempts_window: 900      # 失败计数窗口15分钟
  lock_duration_seconds: 1800      # 锁定30分钟
  max_sessions_per_user: 5         # 最大同时登录数
  kick_oldest_session: true        # 超出时踢掉旧会话

# ==================== 密码策略 ====================
password:
  min_length: 8
  max_length: 32
  require_uppercase: false
  require_lowercase: false
  require_digit: true
  require_special_char: false

# ==================== 日志配置 ====================
log:
  level: "info"                    # trace/debug/info/warn/error
  path: "./logs"
  filename: "user-service.log"
  max_size: 10485760               # 10MB
  max_files: 5
  console_output: true
```

---

## API 参考

### 认证服务 (AuthService)

| 方法 | 描述 | 认证 |
|------|------|------|
| `SendVerifyCode` | 发送短信验证码 | 无 |
| `Register` | 用户注册 | 无 |
| `LoginByPassword` | 密码登录 | 无 |
| `LoginByCode` | 验证码登录 | 无 |
| `RefreshToken` | 刷新令牌 | 无 |
| `Logout` | 登出 | 无 |
| `ResetPassword` | 重置密码 | 无 |
| `ValidateToken` | 验证令牌（内部） | 无 |

### 用户服务 (UserService)

| 方法 | 描述 | 认证 |
|------|------|------|
| `GetCurrentUser` | 获取当前用户信息 | ✅ |
| `UpdateUser` | 更新用户信息 | ✅ |
| `ChangePassword` | 修改密码 | ✅ |
| `DeleteUser` | 注销账号 | ✅ |
| `GetUser` | 获取指定用户 | ✅ Admin |
| `ListUsers` | 获取用户列表 | ✅ Admin |

### 错误码

| 错误码 | 值 | 描述 |
|--------|-----|------|
| `Ok` | 0 | 成功 |
| `InvalidArgument` | 200 | 参数无效 |
| `Unauthenticated` | 1000 | 未认证 |
| `TokenExpired` | 1003 | Token过期 |
| `WrongPassword` | 1011 | 密码错误 |
| `AccountLocked` | 1012 | 账号锁定 |
| `CaptchaWrong` | 1021 | 验证码错误 |
| `UserNotFound` | 2000 | 用户不存在 |
| `MobileTaken` | 2013 | 手机号已注册 |
| `UserDisabled` | 2020 | 用户已禁用 |
| `AdminRequired` | 3001 | 需要管理员权限 |

---

## 部署指南

### Docker Compose 部署

```bash
# 构建并启动
docker-compose up -d --build

# 查看状态
docker-compose ps

# 查看日志
docker-compose logs -f

# 停止服务
docker-compose down

# 停止并删除数据卷
docker-compose down -v
```

### 生产环境建议

1. **配置管理**
   - 敏感信息使用环境变量或密钥管理服务
   - `jwt_secret` 至少 32 字节
   - MySQL/Redis 密码不要硬编码

2. **高可用**
   - 部署多个服务实例
   - MySQL 主从复制
   - Redis 哨兵模式或集群
   - ZooKeeper 3 节点集群

3. **监控**
   - 接入 Prometheus + Grafana
   - 配置告警规则
   - 日志集中收集 (ELK)

4. **安全**
   - 启用 TLS/SSL
   - 配置防火墙规则
   - 定期更新依赖

---

## 待实现与优化

### 🔴 高优先级

| 功能 | 当前状态 | 说明 |
|------|---------|------|
| **短信服务对接** | Mock实现 | 当前 `SmsService::DoSend()` 仅打印日志，验证码固定为 `123456`，需对接阿里云/腾讯云短信服务 |
| **ZooKeeper 重连机制** | 未实现 | `ZooKeeperClient` 连接断开后不会自动重连，需要实现会话过期后的重新注册 |
| **连接池健康检查** | 未实现 | `TemplateConnectionPool` 未实现后台健康检查和连接淘汰机制 |

### 🟡 中优先级

| 功能 | 当前状态 | 说明 |
|------|---------|------|
| **TLS/SSL 支持** | 未实现 | gRPC 服务端/客户端未配置 TLS，生产环境必须启用 |
| **图形验证码** | 未实现 | `login.require_captcha` 配置项存在但功能未实现 |
| **Metrics 监控** | 部分实现 | 端口已配置，但具体指标收集未实现 |
| **限流中间件** | 部分实现 | 仅在验证码场景有限流，缺少全局限流 |
| **OAuth 登录** | 未实现 | 配置项 `enable_oauth_login` 存在但功能未实现 |

### 🟢 低优先级

| 功能 | 当前状态 | 说明 |
|------|---------|------|
| **双因素认证 (2FA)** | 未实现 | 配置项 `require_2fa` 存在但功能未实现 |
| **密码历史检查** | 未实现 | 配置项 `password.history_count` 存在但功能未实现 |
| **密码过期提醒** | 未实现 | 配置项 `password.expire_days` 存在但功能未实现 |
| **设备管理** | 未实现 | 当前仅支持踢出最旧会话，无法查看/管理登录设备 |
| **审计日志** | 未实现 | 敏感操作（登录/改密/注销）未记录审计日志 |

### 代码优化建议

```cpp
// 1. SmsService - 需要实现真实短信发送
// src/auth/sms_service.cpp
Result<void> SmsService::DoSend(const std::string& mobile, 
                                 const std::string& code, 
                                 SmsScene scene) {
    // TODO: 对接实际短信服务商
    // 阿里云示例：
    // AliSmsClient client(config_.aliyun);
    // return client.Send(mobile, code, GetTemplateId(scene));
    
    // 当前实现（仅用于开发测试）
    LOG_INFO("[DEV SMS] mobile={}, code={}", mobile, code);
    return Result<void>::Ok();
}

// 2. ZooKeeperClient - 需要实现重连机制
// src/discovery/zk_client.cpp
void ZooKeeperClient::HandleSessionEvent(int state) {
    if (state == ZOO_EXPIRED_SESSION_STATE) {
        connected_ = false;
        LOG_WARN("ZooKeeper session expired");
        
        // TODO: 实现自动重连
        // std::thread([this]() {
        //     std::this_thread::sleep_for(std::chrono::seconds(5));
        //     if (!closing_.load()) {
        //         Connect(config_.connect_timeout_ms);
        //         // 重新注册服务
        //     }
        // }).detach();
    }
}

// 3. 连接池 - 需要实现健康检查
// include/pool/connection_pool.h
// TODO: 添加后台线程定期检查连接有效性
// void HealthCheck() {
//     while (running_) {
//         std::this_thread::sleep_for(std::chrono::seconds(30));
//         std::lock_guard<std::mutex> lock(mutex_);
//         for (auto it = pool_.begin(); it != pool_.end(); ) {
//             if (!(*it)->Valid()) {
//                 *it = createConnFunc_(*config_);
//             }
//             ++it;
//         }
//     }
// }
```

### 测试覆盖

当前测试状态：

- ✅ 单元测试框架已搭建 (Google Test)
- ⚠️ 部分模块缺少测试用例
- ❌ 缺少集成测试
- ❌ 缺少压力测试

建议补充：

```bash
# 运行现有测试
cd build
ctest --output-on-failure

# 测试覆盖率（需要配置 gcov）
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON
make
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

---

## 贡献指南

### 开发流程

1. Fork 项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

### 代码规范

- 遵循 Google C++ Style Guide
- 使用 `clang-format` 格式化代码
- 所有公共 API 必须有注释
- 新功能必须有对应测试

### 提交信息格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Type:**
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档更新
- `refactor`: 代码重构
- `test`: 测试相关
- `chore`: 构建/工具相关

**示例:**
```
feat(auth): 添加微信OAuth登录支持

- 新增 WechatOAuthService
- 新增 /auth/wechat/callback 接口
- 配置文件添加 wechat 相关配置项

Closes #123
```

---

## 联系方式
- 项目地址: [GitHub](https://github.com/your-repo/user-service)

