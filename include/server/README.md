# Server 模块使用指南

## 📖 概述

Server 模块提供了一个**开箱即用**的 gRPC 服务器封装，内置：

- ✅ MySQL 连接池管理
- ✅ Redis 客户端
- ✅ JWT 认证服务
- ✅ 短信验证码服务
- ✅ ZooKeeper 服务注册与发现
- ✅ 优雅关闭
- ✅ 健康检查

你只需要 **3 行代码** 就能启动一个完整的用户服务：

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .Build();

server->Initialize();
server->Run();
```

---

## 🚀 快速开始

### 最小示例

```cpp
// main.cpp
#include "server/server_builder.h"

int main() {
    using namespace user_service;
    
    // 1. 构建服务器
    auto server = ServerBuilder()
        .WithConfigFile("config.yaml")
        .Build();
    
    // 2. 初始化所有组件（MySQL、Redis、ZooKeeper 等）
    if (!server->Initialize()) {
        std::cerr << "初始化失败" << std::endl;
        return 1;
    }
    
    // 3. 启动服务器（阻塞，直到收到 SIGINT/SIGTERM）
    server->Run();
    
    return 0;
}
```

### 配置文件示例（config.yaml）

```yaml
# 服务器配置
server:
  host: "0.0.0.0"
  grpc_port: 50051

# MySQL 配置
mysql:
  host: "localhost"
  port: 3306
  database: "user_service"
  username: "root"
  password: "your-password"
  pool_size: 10

# Redis 配置
redis:
  host: "localhost"
  port: 6379
  password: ""
  db: 0
  pool_size: 5

# JWT 配置
security:
  jwt_secret: "your-super-secret-key-at-least-32-bytes!"
  jwt_issuer: "user-service"
  access_token_ttl_seconds: 900      # 15 分钟
  refresh_token_ttl_seconds: 604800  # 7 天

# 日志配置
log:
  level: "info"
  path: "./logs"
  filename: "user-service.log"
```

---

## 📚 ServerBuilder API

ServerBuilder 使用 **链式调用** 风格，让配置更加直观：

### 配置加载

| 方法 | 说明 | 示例 |
|------|------|------|
| `WithConfigFile(path)` | 从 YAML 文件加载配置 | `.WithConfigFile("config.yaml")` |
| `WithConfig(config)` | 使用已有配置对象 | `.WithConfig(configPtr)` |
| `LoadFromEnvironment()` | 从环境变量覆盖配置 | `.LoadFromEnvironment()` |

### 服务器配置

| 方法 | 说明 | 示例 |
|------|------|------|
| `WithHost(host)` | 设置监听地址 | `.WithHost("0.0.0.0")` |
| `WithPort(port)` | 设置监听端口 | `.WithPort(50051)` |

### 服务发现配置

| 方法 | 说明 | 示例 |
|------|------|------|
| `EnableServiceDiscovery(bool)` | 启用/禁用 ZooKeeper | `.EnableServiceDiscovery(true)` |
| `WithServiceName(name)` | 设置服务名称 | `.WithServiceName("auth-service")` |

### 生命周期

| 方法 | 说明 | 示例 |
|------|------|------|
| `OnShutdown(callback)` | 设置关闭回调 | `.OnShutdown([](){ ... })` |
| `Build()` | 构建服务器实例 | `.Build()` |

---

## 🎯 常见场景

### 场景 1：本地开发（禁用 ZooKeeper）

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .EnableServiceDiscovery(false)  // 不需要 ZooKeeper
    .Build();

server->Initialize();
server->Run();
```

### 场景 2：指定端口启动

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithPort(50052)  // 覆盖配置文件中的端口
    .Build();
```

### 场景 3：多服务部署（微服务架构）

启动 **认证服务**：

```cpp
// auth_service/main.cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithPort(50051)
    .WithServiceName("auth-service")  // 注册为 auth-service
    .EnableServiceDiscovery(true)
    .Build();
```

启动 **用户服务**：

```cpp
// user_service/main.cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithPort(50052)
    .WithServiceName("user-service")  // 注册为 user-service
    .EnableServiceDiscovery(true)
    .Build();
```

ZooKeeper 中的结构：

```
/services
├── auth-service
│   └── 192.168.1.10:50051   ← 认证服务实例
└── user-service
    └── 192.168.1.10:50052   ← 用户服务实例
```

### 场景 4：Docker 环境

```cpp
// 环境变量会覆盖配置文件
auto server = ServerBuilder()
    .WithConfigFile("/app/config.yaml")
    .LoadFromEnvironment()  // 从环境变量读取敏感信息
    .Build();
```

对应的 `docker-compose.yml`：

```yaml
services:
  user-service:
    image: user-service:latest
    environment:
      - MYSQL_HOST=mysql
      - MYSQL_PASSWORD=secret
      - REDIS_HOST=redis
      - ZK_HOSTS=zookeeper:2181
      - ZK_SERVICE_NAME=user-service
    ports:
      - "50051:50051"
```

### 场景 5：优雅关闭

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .OnShutdown([]() {
        std::cout << "服务器正在关闭，清理资源..." << std::endl;
        // 发送告警通知
        // 完成进行中的请求
    })
    .Build();
```

### 场景 6：水平扩展（多实例）

同一服务启动多个实例：

```cpp
// 实例 1（机器 A）
auto server1 = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithHost("192.168.1.10")
    .WithServiceName("user-service")
    .Build();

// 实例 2（机器 B）
auto server2 = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithHost("192.168.1.11")
    .WithServiceName("user-service")
    .Build();

// ZooKeeper 中会有两个实例：
// /services/user-service/192.168.1.10:50051
// /services/user-service/192.168.1.11:50051
```

---

## 🔧 环境变量参考

| 环境变量 | 说明 | 示例 |
|---------|------|------|
| `MYSQL_HOST` | MySQL 主机地址 | `mysql` |
| `MYSQL_PASSWORD` | MySQL 密码 | `secret` |
| `REDIS_HOST` | Redis 主机地址 | `redis` |
| `ZK_HOSTS` | ZooKeeper 地址 | `zk1:2181,zk2:2181` |
| `ZK_SERVICE_NAME` | 服务名称 | `user-service` |
| `ZK_ENABLED` | 启用服务发现 | `true` |
| `ZK_REGISTER_SELF` | 注册到 ZK | `true` |
| `JWT_SECRET` | JWT 密钥 | `your-secret-key` |

---

## 📊 配置优先级

配置从低到高优先级：

```
配置文件默认值 < 环境变量 < Builder 方法
```

示例：

```cpp
// config.yaml 中 grpc_port: 50051
// 环境变量 GRPC_PORT=50052
// Builder 方法 .WithPort(50053)

auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .LoadFromEnvironment()
    .WithPort(50053)  // 最终使用 50053
    .Build();
```

---

## 🏥 健康检查

服务器自动启用 gRPC 健康检查，可通过以下方式验证：

```bash
# 使用 grpc_health_probe
grpc_health_probe -addr=localhost:50051

# 或使用 grpcurl
grpcurl -plaintext localhost:50051 grpc.health.v1.Health/Check
```

---

## 🔄 完整生命周期

```
┌─────────────────────────────────────────────────────────────┐
│                    服务器生命周期                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ServerBuilder()                                           │
│        │                                                    │
│        ▼                                                    │
│   .WithConfigFile()  ──→  加载配置文件                       │
│        │                                                    │
│        ▼                                                    │
│   .LoadFromEnvironment()  ──→  环境变量覆盖                  │
│        │                                                    │
│        ▼                                                    │
│   .WithPort() / .WithServiceName()  ──→  Builder 覆盖       │
│        │                                                    │
│        ▼                                                    │
│   .Build()  ──→  创建 GrpcServer 实例                        │
│        │                                                    │
│        ▼                                                    │
│   server->Initialize()                                      │
│        ├──→  初始化 MySQL 连接池                             │
│        ├──→  初始化 Redis 客户端                             │
│        ├──→  初始化 Repository 层                            │
│        ├──→  初始化 Service 层                               │
│        ├──→  初始化 Handler 层                               │
│        ├──→  连接 ZooKeeper（可选）                          │
│        └──→  启动 Token 清理任务                             │
│        │                                                    │
│        ▼                                                    │
│   server->Run()                                             │
│        ├──→  启动 gRPC 服务器                                │
│        ├──→  注册到 ZooKeeper（可选）                        │
│        └──→  阻塞等待                                        │
│        │                                                    │
│   ─────┼───── 收到 SIGINT/SIGTERM ─────                     │
│        │                                                    │
│        ▼                                                    │
│   Shutdown()                                                │
│        ├──→  从 ZooKeeper 注销                               │
│        ├──→  停止 Token 清理任务                             │
│        ├──→  触发 OnShutdown 回调                            │
│        └──→  关闭 gRPC 服务器                                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## ❓ 常见问题

### Q1: 如何在不修改代码的情况下切换配置？

使用环境变量：

```bash
# 开发环境
export MYSQL_HOST=localhost
./user_server

# 生产环境
export MYSQL_HOST=mysql-prod.internal
./user_server
```

### Q2: 如何在测试中使用？

```cpp
// 创建 mock 配置
auto config = std::make_shared<Config>();
config->mysql.host = "localhost";
config->mysql.database = "test_db";
config->zookeeper.enabled = false;

// 使用 mock 配置构建
auto server = ServerBuilder()
    .WithConfig(config)
    .Build();
```

### Q3: 如何只启动服务器不阻塞？

```cpp
// 使用 Start() 而不是 Run()
if (!server->Start()) {
    return 1;
}

// 做其他事情...

// 手动等待
server->Wait();
```

### Q4: 如何手动关闭服务器？

```cpp
// 在另一个线程中
server->Shutdown(std::chrono::milliseconds(5000));  // 5 秒超时
```

---

## 📁 相关文件

```
include/server/
├── grpc_server.h       # 服务器核心类
└── server_builder.h    # 构建器

src/server/
├── grpc_server.cpp     # 服务器实现
└── server_builder.cpp  # 构建器实现

configs/
├── config.yaml         # 开发配置
└── config.docker.yaml  # Docker 配置
```

---

## 🎉 就是这么简单！

只需要：

1. **准备配置文件**（或使用环境变量）
2. **3 行代码启动服务**

```cpp
auto server = ServerBuilder().WithConfigFile("config.yaml").Build();
server->Initialize();
server->Run();
```

 <think>

用户要求我为他们的 gRPC 服务器模块编写一个 README.md 文档。这个服务器是一个用户服务系统，包含：

1. **GrpcServer** - 核心服务器类
2. **ServerBuilder** - 构建器模式，用于配置和创建服务器

我需要写一个详细的 README，让使用者不需要了解底层实现就能快速上手。

主要内容应该包括：
1. 快速开始（最简单的使用方式）
2. 配置说明（配置文件格式）
3. ServerBuilder API 详解
4. 常见场景示例（单服务、多服务、Docker、开发模式等）
5. 服务发现说明（ZooKeeper）
6. 最佳实践
7. 常见问题

让我根据代码来写这个文档...
</think>

# Server 模块使用指南

## 📖 概述

Server 模块提供了一个**开箱即用**的 gRPC 服务器封装，内置：

- ✅ MySQL 连接池管理
- ✅ Redis 客户端
- ✅ JWT 认证服务
- ✅ 短信验证码服务
- ✅ ZooKeeper 服务注册与发现
- ✅ 优雅关闭
- ✅ 健康检查

你只需要 **3 行代码** 就能启动一个完整的用户服务：

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .Build();

server->Initialize();
server->Run();
```

---

## 🚀 快速开始

### 最小示例

```cpp
// main.cpp
#include "server/server_builder.h"

int main() {
    using namespace user_service;
    
    // 1. 构建服务器
    auto server = ServerBuilder()
        .WithConfigFile("config.yaml")
        .Build();
    
    // 2. 初始化所有组件（MySQL、Redis、ZooKeeper 等）
    if (!server->Initialize()) {
        std::cerr << "初始化失败" << std::endl;
        return 1;
    }
    
    // 3. 启动服务器（阻塞，直到收到 SIGINT/SIGTERM）
    server->Run();
    
    return 0;
}
```

### 配置文件示例（config.yaml）

```yaml
# 服务器配置
server:
  host: "0.0.0.0"
  grpc_port: 50051

# MySQL 配置
mysql:
  host: "localhost"
  port: 3306
  database: "user_service"
  username: "root"
  password: "your-password"
  pool_size: 10

# Redis 配置
redis:
  host: "localhost"
  port: 6379
  password: ""
  db: 0
  pool_size: 5

# JWT 配置
security:
  jwt_secret: "your-super-secret-key-at-least-32-bytes!"
  jwt_issuer: "user-service"
  access_token_ttl_seconds: 900      # 15 分钟
  refresh_token_ttl_seconds: 604800  # 7 天

# 日志配置
log:
  level: "info"
  path: "./logs"
  filename: "user-service.log"
```

---

## 📚 ServerBuilder API

ServerBuilder 使用 **链式调用** 风格，让配置更加直观：

### 配置加载

| 方法 | 说明 | 示例 |
|------|------|------|
| `WithConfigFile(path)` | 从 YAML 文件加载配置 | `.WithConfigFile("config.yaml")` |
| `WithConfig(config)` | 使用已有配置对象 | `.WithConfig(configPtr)` |
| `LoadFromEnvironment()` | 从环境变量覆盖配置 | `.LoadFromEnvironment()` |

### 服务器配置

| 方法 | 说明 | 示例 |
|------|------|------|
| `WithHost(host)` | 设置监听地址 | `.WithHost("0.0.0.0")` |
| `WithPort(port)` | 设置监听端口 | `.WithPort(50051)` |

### 服务发现配置

| 方法 | 说明 | 示例 |
|------|------|------|
| `EnableServiceDiscovery(bool)` | 启用/禁用 ZooKeeper | `.EnableServiceDiscovery(true)` |
| `WithServiceName(name)` | 设置服务名称 | `.WithServiceName("auth-service")` |

### 生命周期

| 方法 | 说明 | 示例 |
|------|------|------|
| `OnShutdown(callback)` | 设置关闭回调 | `.OnShutdown([](){ ... })` |
| `Build()` | 构建服务器实例 | `.Build()` |

---

## 🎯 常见场景

### 场景 1：本地开发（禁用 ZooKeeper）

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .EnableServiceDiscovery(false)  // 不需要 ZooKeeper
    .Build();

server->Initialize();
server->Run();
```

### 场景 2：指定端口启动

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithPort(50052)  // 覆盖配置文件中的端口
    .Build();
```

### 场景 3：多服务部署（微服务架构）

启动 **认证服务**：

```cpp
// auth_service/main.cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithPort(50051)
    .WithServiceName("auth-service")  // 注册为 auth-service
    .EnableServiceDiscovery(true)
    .Build();
```

启动 **用户服务**：

```cpp
// user_service/main.cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithPort(50052)
    .WithServiceName("user-service")  // 注册为 user-service
    .EnableServiceDiscovery(true)
    .Build();
```

ZooKeeper 中的结构：

```
/services
├── auth-service
│   └── 192.168.1.10:50051   ← 认证服务实例
└── user-service
    └── 192.168.1.10:50052   ← 用户服务实例
```

### 场景 4：Docker 环境

```cpp
// 环境变量会覆盖配置文件
auto server = ServerBuilder()
    .WithConfigFile("/app/config.yaml")
    .LoadFromEnvironment()  // 从环境变量读取敏感信息
    .Build();
```

对应的 `docker-compose.yml`：

```yaml
services:
  user-service:
    image: user-service:latest
    environment:
      - MYSQL_HOST=mysql
      - MYSQL_PASSWORD=secret
      - REDIS_HOST=redis
      - ZK_HOSTS=zookeeper:2181
      - ZK_SERVICE_NAME=user-service
    ports:
      - "50051:50051"
```

### 场景 5：优雅关闭

```cpp
auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .OnShutdown([]() {
        std::cout << "服务器正在关闭，清理资源..." << std::endl;
        // 发送告警通知
        // 完成进行中的请求
    })
    .Build();
```

### 场景 6：水平扩展（多实例）

同一服务启动多个实例：

```cpp
// 实例 1（机器 A）
auto server1 = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithHost("192.168.1.10")
    .WithServiceName("user-service")
    .Build();

// 实例 2（机器 B）
auto server2 = ServerBuilder()
    .WithConfigFile("config.yaml")
    .WithHost("192.168.1.11")
    .WithServiceName("user-service")
    .Build();

// ZooKeeper 中会有两个实例：
// /services/user-service/192.168.1.10:50051
// /services/user-service/192.168.1.11:50051
```

---

## 🔧 环境变量参考

| 环境变量 | 说明 | 示例 |
|---------|------|------|
| `MYSQL_HOST` | MySQL 主机地址 | `mysql` |
| `MYSQL_PASSWORD` | MySQL 密码 | `secret` |
| `REDIS_HOST` | Redis 主机地址 | `redis` |
| `ZK_HOSTS` | ZooKeeper 地址 | `zk1:2181,zk2:2181` |
| `ZK_SERVICE_NAME` | 服务名称 | `user-service` |
| `ZK_ENABLED` | 启用服务发现 | `true` |
| `ZK_REGISTER_SELF` | 注册到 ZK | `true` |
| `JWT_SECRET` | JWT 密钥 | `your-secret-key` |

---

## 📊 配置优先级

配置从低到高优先级：

```
配置文件默认值 < 环境变量 < Builder 方法
```

示例：

```cpp
// config.yaml 中 grpc_port: 50051
// 环境变量 GRPC_PORT=50052
// Builder 方法 .WithPort(50053)

auto server = ServerBuilder()
    .WithConfigFile("config.yaml")
    .LoadFromEnvironment()
    .WithPort(50053)  // 最终使用 50053
    .Build();
```

---

## 🏥 健康检查

服务器自动启用 gRPC 健康检查，可通过以下方式验证：

```bash
# 使用 grpc_health_probe
grpc_health_probe -addr=localhost:50051

# 或使用 grpcurl
grpcurl -plaintext localhost:50051 grpc.health.v1.Health/Check
```

---

## 🔄 完整生命周期

```
┌─────────────────────────────────────────────────────────────┐
│                    服务器生命周期                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ServerBuilder()                                           │
│        │                                                    │
│        ▼                                                    │
│   .WithConfigFile()  ──→  加载配置文件                       │
│        │                                                    │
│        ▼                                                    │
│   .LoadFromEnvironment()  ──→  环境变量覆盖                  │
│        │                                                    │
│        ▼                                                    │
│   .WithPort() / .WithServiceName()  ──→  Builder 覆盖       │
│        │                                                    │
│        ▼                                                    │
│   .Build()  ──→  创建 GrpcServer 实例                        │
│        │                                                    │
│        ▼                                                    │
│   server->Initialize()                                      │
│        ├──→  初始化 MySQL 连接池                             │
│        ├──→  初始化 Redis 客户端                             │
│        ├──→  初始化 Repository 层                            │
│        ├──→  初始化 Service 层                               │
│        ├──→  初始化 Handler 层                               │
│        ├──→  连接 ZooKeeper（可选）                          │
│        └──→  启动 Token 清理任务                             │
│        │                                                    │
│        ▼                                                    │
│   server->Run()                                             │
│        ├──→  启动 gRPC 服务器                                │
│        ├──→  注册到 ZooKeeper（可选）                        │
│        └──→  阻塞等待                                        │
│        │                                                    │
│   ─────┼───── 收到 SIGINT/SIGTERM ─────                     │
│        │                                                    │
│        ▼                                                    │
│   Shutdown()                                                │
│        ├──→  从 ZooKeeper 注销                               │
│        ├──→  停止 Token 清理任务                             │
│        ├──→  触发 OnShutdown 回调                            │
│        └──→  关闭 gRPC 服务器                                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## ❓ 常见问题

### Q1: 如何在不修改代码的情况下切换配置？

使用环境变量：

```bash
# 开发环境
export MYSQL_HOST=localhost
./user_server

# 生产环境
export MYSQL_HOST=mysql-prod.internal
./user_server
```

### Q2: 如何在测试中使用？

```cpp
// 创建 mock 配置
auto config = std::make_shared<Config>();
config->mysql.host = "localhost";
config->mysql.database = "test_db";
config->zookeeper.enabled = false;

// 使用 mock 配置构建
auto server = ServerBuilder()
    .WithConfig(config)
    .Build();
```

### Q3: 如何只启动服务器不阻塞？

```cpp
// 使用 Start() 而不是 Run()
if (!server->Start()) {
    return 1;
}

// 做其他事情...

// 手动等待
server->Wait();
```

### Q4: 如何手动关闭服务器？

```cpp
// 在另一个线程中
server->Shutdown(std::chrono::milliseconds(5000));  // 5 秒超时
```

---

## 📁 相关文件

```
include/server/
├── grpc_server.h       # 服务器核心类
└── server_builder.h    # 构建器

src/server/
├── grpc_server.cpp     # 服务器实现
└── server_builder.cpp  # 构建器实现

configs/
├── config.yaml         # 开发配置
└── config.docker.yaml  # Docker 配置
```

---

## 🎉 就是这么简单！

只需要：

1. **准备配置文件**（或使用环境变量）
2. **3 行代码启动服务**

```cpp
auto server = ServerBuilder().WithConfigFile("config.yaml").Build();
server->Initialize();
server->Run();
```

