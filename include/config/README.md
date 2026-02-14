# Config 模块设计文档

## 目录结构

```
include/config/
└── config.h              # 配置结构体定义

src/config/
├── CMakeLists.txt        # 编译配置
└── config.cpp            # 配置加载、校验、序列化实现
```

---

## 1. 模块概述

Config 模块是整个系统的**配置中心**，负责：

- 从 YAML 文件加载配置
- 支持环境变量覆盖敏感信息
- 配置合法性校验
- 为其他模块提供类型安全的配置访问

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         配置加载流程                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐              │
│  │  YAML 文件   │    │   环境变量   │    │  命令行参数  │              │
│  │              │    │              │    │   (可选)     │              │
│  │ config.yaml  │    │ MYSQL_HOST   │    │ --port=8080  │              │
│  │              │    │ JWT_SECRET   │    │              │              │
│  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘              │
│         │                   │                   │                       │
│         │    优先级: 低     │    优先级: 高     │    优先级: 最高       │
│         │                   │                   │                       │
│         └─────────────────┬─┴───────────────────┘                       │
│                           │                                             │
│                           ▼                                             │
│                  ┌─────────────────┐                                    │
│                  │  Config 对象    │                                    │
│                  │                 │                                    │
│                  │ • LoadFromFile  │                                    │
│                  │ • LoadFromEnv   │                                    │
│                  │ • Validate      │                                    │
│                  └────────┬────────┘                                    │
│                           │                                             │
│                           ▼                                             │
│                  ┌─────────────────┐                                    │
│                  │   配置校验      │                                    │
│                  │                 │                                    │
│                  │ • 端口范围      │                                    │
│                  │ • 必填项检查    │                                    │
│                  │ • 逻辑关系      │                                    │
│                  └────────┬────────┘                                    │
│                           │                                             │
│                           ▼                                             │
│            ┌──────────────┴──────────────┐                              │
│            │                             │                              │
│            ▼                             ▼                              │
│     校验通过 ✓                    校验失败 ✗                            │
│     返回 Config                   抛出异常                              │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 配置结构体设计

### 2.1 总体结构

```cpp
struct Config {
    ServerConfig server;           // gRPC 服务配置
    MySQLConfig mysql;             // MySQL 数据库配置
    RedisConfig redis;             // Redis 缓存配置
    LogConfig log;                 // 日志配置
    ZooKeeperConfig zookeeper;     // 服务注册与发现
    
    // 业务配置
    SecurityConfig security;       // JWT/Token 配置
    SmsConfig sms;                 // 短信验证码配置
    LoginConfig login;             // 登录安全策略
    PasswordPolicyConfig password; // 密码强度策略
    
    // 静态方法
    static Config LoadFromFile(const std::string& path);
    void LoadFromEnv();
    std::string ToString() const;
};
```

### 2.2 配置结构关系图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Config 结构体组成                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Config                                                                 │
│  │                                                                      │
│  ├── ServerConfig                 ← gRPC 服务监听配置                   │
│  │   ├── host: string                                                   │
│  │   ├── grpc_port: int                                                 │
│  │   └── metrics_port: int                                              │
│  │                                                                      │
│  ├── MySQLConfig                  ← 数据库连接配置                      │
│  │   ├── host, port, database                                           │
│  │   ├── username, password                                             │
│  │   ├── pool_size                                                      │
│  │   ├── timeout 配置 (optional)                                        │
│  │   └── 重试配置                                                       │
│  │                                                                      │
│  ├── RedisConfig                  ← 缓存连接配置                        │
│  │   ├── host, port, password, db                                       │
│  │   ├── pool_size                                                      │
│  │   └── timeout 配置 (optional)                                        │
│  │                                                                      │
│  ├── ZooKeeperConfig              ← 服务注册与发现                      │
│  │   ├── hosts, session_timeout                                         │
│  │   ├── root_path, service_name                                        │
│  │   ├── enabled, register_self                                         │
│  │   └── 元数据 (weight, region, zone)                                  │
│  │                                                                      │
│  ├── SecurityConfig               ← 安全配置                            │
│  │   ├── jwt_secret, jwt_issuer                                         │
│  │   └── access_token_ttl, refresh_token_ttl                            │
│  │                                                                      │
│  ├── SmsConfig                    ← 验证码配置                          │
│  │   ├── code_len, code_ttl                                             │
│  │   ├── send_interval                                                  │
│  │   └── max_retry, lock_seconds                                        │
│  │                                                                      │
│  ├── LoginConfig                  ← 登录安全策略                        │
│  │   ├── max_failed_attempts                                            │
│  │   ├── lock_duration                                                  │
│  │   └── max_sessions_per_user                                          │
│  │                                                                      │
│  ├── PasswordPolicyConfig         ← 密码策略                            │
│  │   ├── min_length, max_length                                         │
│  │   └── require_xxx 系列                                               │
│  │                                                                      │
│  └── LogConfig                    ← 日志配置                            │
│      ├── level, path, filename                                          │
│      └── max_size, max_files                                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 各配置结构详解

### 3.1 ServerConfig - 服务配置

```cpp
struct ServerConfig {
    std::string host = "0.0.0.0";  // 监听地址
    int grpc_port = 50051;          // gRPC 端口
    int metrics_port = 9090;        // Prometheus 指标端口
};
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `host` | string | "0.0.0.0" | 监听地址，容器内必须用 0.0.0.0 |
| `grpc_port` | int | 50051 | gRPC 服务端口 |
| `metrics_port` | int | 9090 | Prometheus 监控指标端口 |

### 3.2 MySQLConfig - 数据库配置

```cpp
struct MySQLConfig {
    // 基本连接信息
    std::string host = "localhost";
    int port = 3306;
    std::string database = "user_service";
    std::string username = "root";
    std::string password = "";
    int pool_size = 10;

    // 超时配置（毫秒）
    std::optional<unsigned int> connection_timeout_ms;
    std::optional<unsigned int> read_timeout_ms;
    std::optional<unsigned int> write_timeout_ms;
    
    // 重试配置
    unsigned int max_retries = 3;
    unsigned int retry_interval_ms = 1000;

    // 其他配置
    std::optional<bool> auto_reconnect;
    std::string charset = "utf8mb4";
    
    int GetPoolSize() const { return pool_size; }
};
```

**设计说明：**

| 设计点 | 说明 |
|--------|------|
| `std::optional` 超时 | 未设置时使用 MySQL 客户端默认值 |
| `GetPoolSize()` | 统一接口，供连接池模板调用 |
| `charset` | 默认 utf8mb4 支持 emoji |

### 3.3 RedisConfig - 缓存配置

```cpp
struct RedisConfig {
    std::string host = "localhost";
    int port = 6379;
    std::string password = "";
    int db = 0;
    int pool_size = 5;

    std::optional<unsigned int> connect_timeout_ms;
    std::optional<unsigned int> socket_timeout_ms;
    unsigned int wait_timeout_ms = 100;
    
    int GetPoolSize() const { return pool_size; }
};
```

### 3.4 ZooKeeperConfig - 服务发现配置

```cpp
struct ZooKeeperConfig {
    // 连接配置
    std::string hosts = "127.0.0.1:2181";
    int session_timeout_ms = 15000;
    int connect_timeout_ms = 10000;
    
    // 服务注册配置
    std::string root_path = "/services";
    std::string service_name = "user-service";
    
    // 开关
    bool enabled = true;
    bool register_self = true;
    
    // 元数据（负载均衡用）
    int weight = 100;
    std::string region = "";
    std::string zone = "";
    std::string version = "1.0.0";
    
    std::string GetServicePath() const {
        return root_path + "/" + service_name;
    }
};
```

**使用场景：**

| 场景 | enabled | register_self | 说明 |
|------|---------|---------------|------|
| gRPC 服务端 | true | true | 注册自己，供其他服务发现 |
| gRPC 客户端 | true | false | 只发现服务，不注册自己 |
| 单机测试 | false | false | 禁用服务发现 |

### 3.5 SecurityConfig - 安全配置

```cpp
struct SecurityConfig {
    std::string jwt_secret = "your-secret-key";
    std::string jwt_issuer = "user-service";
    int access_token_ttl_seconds = 900;      // 15 分钟
    int refresh_token_ttl_seconds = 604800;  // 7 天
};
```

**Token TTL 设计原则：**

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     Token 有效期设计                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Access Token (900s = 15分钟)                                           │
│  ├── 短有效期 → 即使泄露，影响时间有限                                   │
│  ├── 无状态验证 → 不查数据库，高性能                                     │
│  └── 频繁刷新 → 可及时发现异常并阻断                                     │
│                                                                         │
│  Refresh Token (604800s = 7天)                                          │
│  ├── 长有效期 → 用户无需频繁登录                                         │
│  ├── 服务端存储 → 可主动失效（如改密码后）                               │
│  └── 仅用于刷新 → 不携带敏感信息                                         │
│                                                                         │
│  关键约束：refresh_token_ttl > access_token_ttl                          │
│  （由 ValidateConfig 强制检查）                                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.6 SmsConfig - 验证码配置

```cpp
struct SmsConfig {
    int code_len = 6;                  // 验证码长度
    int code_ttl_seconds = 300;        // 有效期 5 分钟
    int send_interval_seconds = 60;    // 发送间隔 60 秒
    int max_retry_count = 5;           // 最大验证错误次数
    int retry_ttl_seconds = 300;       // 错误计数窗口 5 分钟
    int lock_seconds = 1800;           // 锁定时长 30 分钟
};
```

**防攻击机制：**

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     验证码防攻击配置                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  send_interval_seconds = 60                                             │
│  └── 防止频繁发送：同一手机号 60 秒内只能发一次                          │
│                                                                         │
│  max_retry_count = 5 + retry_ttl_seconds = 300                          │
│  └── 防止暴力破解：5 分钟内最多验证错误 5 次                             │
│                                                                         │
│  lock_seconds = 1800                                                    │
│  └── 错误次数超限后锁定 30 分钟                                          │
│                                                                         │
│  校验规则：lock_seconds >= code_ttl_seconds                              │
│  └── 锁定时间必须 >= 验证码有效期，否则锁定无意义                         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.7 LoginConfig - 登录安全配置

```cpp
struct LoginConfig {
    // 登录失败锁定策略
    int max_failed_attempts = 5;
    int failed_attempts_window = 900;   // 15 分钟
    int lock_duration_seconds = 1800;   // 30 分钟
    
    // 会话管理
    int max_sessions_per_user = 5;
    bool kick_oldest_session = true;
    
    // 登录方式开关
    bool enable_password_login = true;
    bool enable_sms_login = true;
    
    // 图形验证码
    bool require_captcha = false;
    int captcha_after_failed_attempts = 3;
};
```

### 3.8 PasswordPolicyConfig - 密码策略

```cpp
struct PasswordPolicyConfig {
    int min_length = 8;
    int max_length = 32;
    bool require_uppercase = false;
    bool require_lowercase = false;
    bool require_digit = true;
    bool require_special_char = false;
    int expire_days = 0;          // 0 = 不过期
    int history_count = 0;        // 0 = 不检查历史
};
```

### 3.9 LogConfig - 日志配置

```cpp
struct LogConfig {
    std::string level = "info";
    std::string path = "./logs";
    std::string filename = "user-service.log";
    size_t max_size = 10 * 1024 * 1024;  // 10MB
    int max_files = 5;
    bool console_output = true;
};
```

---

## 4. 配置加载实现

### 4.1 从 YAML 文件加载

```cpp
Config Config::LoadFromFile(const std::string& path) {
    Config config;
    try {
        YAML::Node yaml = YAML::LoadFile(path);

        // Server 配置
        if (yaml["server"]) {
            const auto& s = yaml["server"];
            if (s["host"]) config.server.host = s["host"].as<std::string>();
            if (s["grpc_port"]) config.server.grpc_port = s["grpc_port"].as<int>();
            // ...
        }

        // MySQL 配置
        if (yaml["mysql"]) {
            const auto& m = yaml["mysql"];
            if (m["host"]) config.mysql.host = m["host"].as<std::string>();
            if (m["port"]) config.mysql.port = m["port"].as<int>();
            // ...
        }
        
        // ... 其他配置模块
        
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load config: " + std::string(e.what()));
    }

    // 校验配置
    ValidateConfig(config);
    return config;
}
```

### 4.2 环境变量覆盖

```cpp
void Config::LoadFromEnv() {
    // MySQL
    if (const char* env = std::getenv("MYSQL_HOST")) mysql.host = env;
    if (const char* env = std::getenv("MYSQL_PASSWORD")) mysql.password = env;
    
    // Redis
    if (const char* env = std::getenv("REDIS_HOST")) redis.host = env;
    
    // ZooKeeper
    if (const char* env = std::getenv("ZK_HOSTS")) zookeeper.hosts = env;
    if (const char* env = std::getenv("ZK_SERVICE_NAME")) zookeeper.service_name = env;
    if (const char* env = std::getenv("ZK_ENABLED")) {
        zookeeper.enabled = (std::string(env) == "true" || std::string(env) == "1");
    }
    
    // Security（敏感信息）
    if (const char* env = std::getenv("JWT_SECRET")) security.jwt_secret = env;
    
    // ... 其他环境变量
}
```

**支持的环境变量清单：**

| 环境变量 | 覆盖配置项 | 用途 |
|----------|------------|------|
| `MYSQL_HOST` | mysql.host | Docker 容器名 |
| `MYSQL_PASSWORD` | mysql.password | 敏感信息 |
| `REDIS_HOST` | redis.host | Docker 容器名 |
| `ZK_HOSTS` | zookeeper.hosts | ZK 地址 |
| `ZK_SERVICE_NAME` | zookeeper.service_name | 区分服务 |
| `ZK_ENABLED` | zookeeper.enabled | 开关 |
| `ZK_REGISTER_SELF` | zookeeper.register_self | 开关 |
| `JWT_SECRET` | security.jwt_secret | 敏感信息 |

---

## 5. 配置校验机制

### 5.1 校验函数

```cpp
void ValidateConfig(const Config& config) {
    // ==================== 端口校验 ====================
    if (config.server.grpc_port <= 0 || config.server.grpc_port > 65535) {
        throw std::runtime_error("Invalid gRPC port: " + 
            std::to_string(config.server.grpc_port));
    }
    
    // ==================== 连接池校验 ====================
    if (config.mysql.pool_size <= 0) {
        throw std::runtime_error("Invalid MySQL pool size");
    }
    
    // ==================== 必填项校验 ====================
    if (config.security.jwt_secret.empty()) {
        throw std::runtime_error("JWT secret is empty");
    }
    
    // ==================== 逻辑关系校验 ====================
    // Refresh Token TTL 必须大于 Access Token TTL
    if (config.security.refresh_token_ttl_seconds <= 
        config.security.access_token_ttl_seconds) {
        throw std::runtime_error(
            "Refresh token TTL should be greater than access token TTL");
    }
    
    // 锁定时间必须 >= 验证码有效期
    if (config.sms.lock_seconds < config.sms.code_ttl_seconds) {
        throw std::runtime_error(
            "SMS lock seconds should be >= code TTL");
    }
    
    // 密码长度范围
    if (config.password.min_length > config.password.max_length) {
        throw std::runtime_error(
            "Password min length should be <= max length");
    }
    
    // ZooKeeper 配置
    if (config.zookeeper.enabled) {
        if (config.zookeeper.hosts.empty()) {
            throw std::runtime_error("ZooKeeper hosts is empty");
        }
        if (config.zookeeper.root_path[0] != '/') {
            throw std::runtime_error("ZooKeeper root path must start with '/'");
        }
    }
}
```

### 5.2 校验规则汇总

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         配置校验规则                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  端口范围校验                                                           │
│  ├── grpc_port ∈ [1, 65535]                                            │
│  ├── metrics_port ∈ [1, 65535]                                         │
│  └── mysql.port ∈ [1, 65535]                                           │
│                                                                         │
│  正数校验                                                               │
│  ├── mysql.pool_size > 0                                               │
│  ├── redis.pool_size > 0                                               │
│  ├── sms.code_len ∈ [1, 10]                                            │
│  ├── sms.code_ttl_seconds > 0                                          │
│  ├── login.max_failed_attempts > 0                                     │
│  └── password.min_length > 0                                           │
│                                                                         │
│  非空校验                                                               │
│  ├── mysql.host ≠ ""                                                   │
│  ├── mysql.database ≠ ""                                               │
│  ├── security.jwt_secret ≠ ""                                          │
│  └── security.jwt_issuer ≠ ""                                          │
│                                                                         │
│  逻辑关系校验                                                           │
│  ├── refresh_token_ttl > access_token_ttl                              │
│  ├── sms.lock_seconds >= sms.code_ttl_seconds                          │
│  ├── password.min_length <= password.max_length                        │
│  └── login.captcha_after_failed <= login.max_failed_attempts           │
│                                                                         │
│  条件校验                                                               │
│  └── if zookeeper.enabled:                                             │
│      ├── hosts ≠ ""                                                    │
│      ├── root_path starts with '/'                                     │
│      └── if register_self: service_name ≠ ""                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 6. ToString 序列化

每个配置结构都实现了 `ToString()` 方法，用于日志输出和调试：

```cpp
std::string MySQLConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== MySQL Config ===" << std::endl;
    oss << "Host: " << host << std::endl;
    oss << "Port: " << port << std::endl;
    oss << "Database: " << database << std::endl;
    oss << "Username: " << username << std::endl;
    oss << "Password: " << (password.empty() ? "(empty)" : "******") << std::endl;
    oss << "Pool Size: " << pool_size << std::endl;
    // ...
    return oss.str();
}
```

**敏感信息处理：**

```cpp
// ❌ 错误：直接输出密码
oss << "Password: " << password << std::endl;

// ✓ 正确：脱敏处理
oss << "Password: " << (password.empty() ? "(empty)" : "******") << std::endl;
oss << "JWT Secret: " << (jwt_secret.empty() ? "(empty)" : "******") << std::endl;
```

---

## 7. 使用示例

### 7.1 基本使用

```cpp
#include "config/config.h"

int main() {
    // 1. 从文件加载配置
    auto config = Config::LoadFromFile("configs/config.yaml");
    
    // 2. 环境变量覆盖（生产环境）
    config.LoadFromEnv();
    
    // 3. 打印配置（调试用）
    std::cout << config.ToString() << std::endl;
    
    // 4. 使用配置
    auto mysql_pool = CreateMySQLPool(config.mysql);
    auto redis_client = CreateRedisClient(config.redis);
    auto jwt_service = CreateJwtService(config.security);
    
    return 0;
}
```

### 7.2 结合命令行参数

```cpp
int main(int argc, char* argv[]) {
    // 确定配置文件路径
    std::string config_path = "configs/config.yaml";  // 默认路径
    
    // 优先级1：环境变量
    if (const char* env = std::getenv("CONFIG_PATH")) {
        config_path = env;
    }
    // 优先级2：命令行参数
    else if (argc > 2 && std::string(argv[1]) == "--config") {
        config_path = argv[2];
    }
    
    // 加载配置
    auto config = std::make_shared<Config>(Config::LoadFromFile(config_path));
    config->LoadFromEnv();
    
    // ...
}
```

### 7.3 依赖注入模式

```cpp
// 创建共享配置
auto config = std::make_shared<Config>(Config::LoadFromFile(path));

// 注入到各个服务
auto auth_service = std::make_shared<AuthService>(
    config,           // 整个配置对象
    user_db,
    redis_client,
    token_repo,
    jwt_service,
    sms_service
);

// 服务内部使用需要的配置
class AuthService {
public:
    AuthService(std::shared_ptr<Config> config, ...) 
        : config_(config) {
        // 使用 config_->login.max_failed_attempts
        // 使用 config_->sms.code_ttl_seconds
    }
private:
    std::shared_ptr<Config> config_;
};
```

---

## 8. CMakeLists.txt

```cmake
add_library(user_config STATIC config.cpp)

target_include_directories(user_config PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(user_config PUBLIC
    yaml-cpp::yaml-cpp    # YAML 解析库
    user_common           # 日志等公共组件
)
```

---

## 9. 最佳实践

### 9.1 敏感信息管理

```yaml
# ❌ 错误：敏感信息写在配置文件中并提交到 Git
security:
  jwt_secret: "my-real-secret-key-12345"

mysql:
  password: "production-password"
```

```yaml
# ✓ 正确：配置文件使用占位值
security:
  jwt_secret: "change-me-in-production"

mysql:
  password: ""

# 实际值通过环境变量传入
# export JWT_SECRET="real-secret-from-vault"
# export MYSQL_PASSWORD="real-password-from-vault"
```

### 9.2 配置文件分离

```
configs/
├── config.yaml           # 本地开发（Git 跟踪）
├── config.docker.yaml    # Docker 部署（Git 跟踪）
├── config.local.yaml     # 个人本地配置（.gitignore）
└── config.prod.yaml      # 生产配置（不入库，用 ConfigMap/Vault）
```

### 9.3 启动时打印配置

```cpp
// 启动时打印配置，方便排查问题
LOG_INFO("========== Configuration ==========");
LOG_INFO("{}", config.server.ToString());
LOG_INFO("{}", config.mysql.ToString());
LOG_INFO("{}", config.zookeeper.ToString());
LOG_INFO("===================================");
```

---

## 10. 相关文件

| 文件 | 说明 |
|------|------|
| `include/config/config.h` | 配置结构体定义 |
| `src/config/config.cpp` | 配置加载、校验、序列化实现 |
| `src/config/CMakeLists.txt` | 编译配置 |
| `configs/config.yaml` | 本地开发配置文件 |
| `configs/config.docker.yaml` | 容器运行配置文件 |

