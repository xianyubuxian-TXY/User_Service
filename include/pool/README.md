# Pool 模块 - 通用连接池

## 📋 目录

- [概述](#概述)
- [架构设计](#架构设计)
- [核心组件](#核心组件)
- [使用指南](#使用指南)
- [API 参考](#api-参考)
- [设计原理](#设计原理)
- [最佳实践](#最佳实践)

---

## 概述

`pool` 模块提供了一个**泛型数据库连接池**实现，采用 C++17 模板元编程技术，支持多种数据库连接类型（MySQL、Redis 等）的统一管理。

### 核心特性

| 特性 | 说明 |
|------|------|
| 🔒 **线程安全** | 所有公共方法均可在多线程环境下安全调用 |
| 🎯 **泛型设计** | 一套代码支持多种连接类型 |
| 🧹 **RAII 模式** | ConnectionGuard 自动管理连接生命周期 |
| ⚡ **编译期推导** | 自动匹配连接类型与配置类型 |
| 🔄 **自动重建** | 检测无效连接并自动重建 |
| ⏱️ **超时控制** | 获取连接支持超时等待 |

---

## 架构设计

```
┌─────────────────────────────────────────────────────────────────────┐
│                        TemplateConnectionPool<T>                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    编译期类型推导                              │   │
│  │                                                               │   │
│  │   ConnectionConfig<MySQLConnection> → MySQLConfig            │   │
│  │   ConnectionConfig<RedisConnection> → RedisConfig            │   │
│  │                                                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                       连接池核心                               │   │
│  │                                                               │   │
│  │   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐        │   │
│  │   │  Conn1  │  │  Conn2  │  │  Conn3  │  │  Conn4  │  ...   │   │
│  │   └─────────┘  └─────────┘  └─────────┘  └─────────┘        │   │
│  │                                                               │   │
│  │   std::deque<ConnectionPtr> + mutex + condition_variable     │   │
│  │                                                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                    ConnectionGuard (RAII)                     │   │
│  │                                                               │   │
│  │   构造 ──► Acquire() ──► 使用连接 ──► 析构 ──► Release()     │   │
│  │                                                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 连接生命周期

```
┌─────────────────────────────────────────────────────────────────────┐
│                          连接生命周期                                 │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  初始化阶段                                                          │
│  ┌─────────┐                                                        │
│  │ Config  │ ──► createConnFunc_() ──► pool_.push_back() × N       │
│  └─────────┘                                                        │
│                                                                      │
│  使用阶段                                                            │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                                                              │    │
│  │   pool                     业务代码                          │    │
│  │   ┌─────┐                  ┌─────────────────┐               │    │
│  │   │ Conn│ ◄──── Acquire ───│ ConnectionGuard │               │    │
│  │   │     │                  │                 │               │    │
│  │   │     │ ────► Release ──►│   ~Guard()      │               │    │
│  │   └─────┘                  └─────────────────┘               │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  重建机制                                                            │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                                                              │    │
│  │   conn->Valid() == false                                    │    │
│  │         │                                                    │    │
│  │         ▼                                                    │    │
│  │   createConnFunc_(*config_) ──► 返回新连接                   │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. ConnectionConfig - 类型映射

```cpp
// 主模板（未特化时触发编译错误）
template<typename>
struct ConnectionConfig;

// MySQLConnection → MySQLConfig
template<>
struct ConnectionConfig<MySQLConnection> { 
    using type = MySQLConfig; 
};

// RedisConnection → RedisConfig
template<>
struct ConnectionConfig<RedisConnection> { 
    using type = RedisConfig; 
};

// 类型别名，简化使用
template<typename T>
using Config_t = typename ConnectionConfig<T>::type;
```

### 2. TemplateConnectionPool - 连接池模板类

```cpp
template<typename T>
class TemplateConnectionPool {
public:
    using ConnectionPtr = std::unique_ptr<T>;
    using Config = Config_t<T>;              // 编译期自动推导
    using ConfigPtr = std::shared_ptr<const Config>;
    using CreateFunc = std::function<ConnectionPtr(const Config&)>;

    // 构造函数
    TemplateConnectionPool(std::shared_ptr<Config> global_config, CreateFunc func);

    // 获取连接（返回 RAII 守卫）
    ConnectionGuard CreateConnection();

private:
    ConnectionPtr Acquire();                 // 从池中获取
    void Release(ConnectionPtr conn);        // 归还到池中
};
```

### 3. ConnectionGuard - RAII 守卫类

```cpp
class ConnectionGuard {
public:
    ConnectionGuard(TemplateConnectionPool& pool);  // 构造时获取连接
    ~ConnectionGuard();                              // 析构时归还连接

    T* operator->();    // 智能指针风格访问
    T& operator*();     // 解引用访问
    T* get() const;     // 获取原始指针

    // 禁止拷贝，允许移动
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard(ConnectionGuard&&) noexcept;
};
```

---

## 使用指南

### 基础用法

```cpp
#include "pool/connection_pool.h"
#include "db/mysql_connection.h"
#include "config/config.h"

using MySQLPool = TemplateConnectionPool<MySQLConnection>;

// 1. 创建连接池
auto config = Config::LoadFromFile("config.yaml");
auto pool = std::make_shared<MySQLPool>(
    std::make_shared<Config>(config),
    [](const MySQLConfig& cfg) {
        return std::make_unique<MySQLConnection>(cfg);
    }
);

// 2. 使用连接（RAII 自动归还）
{
    auto conn = pool->CreateConnection();
    
    // 执行查询
    auto result = conn->Query("SELECT * FROM users WHERE id = ?", {123});
    
    // 执行更新
    conn->Execute("UPDATE users SET name = ? WHERE id = ?", {"Alice", 123});
    
} // conn 析构时自动归还到池中
```

### 在 Repository 层使用

```cpp
class UserDB {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    explicit UserDB(std::shared_ptr<MySQLPool> pool) 
        : pool_(std::move(pool)) {}

    Result<UserEntity> FindById(int64_t id) {
        try {
            // 获取连接（RAII 守卫）
            auto conn = pool_->CreateConnection();
            
            // 检查连接有效性
            if (!conn->Valid()) {
                return Result<UserEntity>::Fail(
                    ErrorCode::ServiceUnavailable, 
                    "数据库连接不可用"
                );
            }
            
            // 执行查询
            auto res = conn->Query(
                "SELECT * FROM users WHERE id = ?", 
                {std::to_string(id)}
            );
            
            if (res.Next()) {
                return Result<UserEntity>::Ok(ParseRow(res));
            }
            
            return Result<UserEntity>::Fail(ErrorCode::UserNotFound);
            
        } catch (const std::exception& e) {
            LOG_ERROR("FindById failed: {}", e.what());
            return Result<UserEntity>::Fail(ErrorCode::Internal);
        }
    }
    
private:
    std::shared_ptr<MySQLPool> pool_;
};
```

### 多数据源场景

```cpp
class DataAccessLayer {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    using RedisPool = TemplateConnectionPool<RedisConnection>;
    
    DataAccessLayer(std::shared_ptr<Config> config) {
        // 创建 MySQL 连接池
        mysql_pool_ = std::make_shared<MySQLPool>(
            config,
            [](const MySQLConfig& cfg) {
                return std::make_unique<MySQLConnection>(cfg);
            }
        );
        
        // 创建 Redis 连接池
        redis_pool_ = std::make_shared<RedisPool>(
            config,
            [](const RedisConfig& cfg) {
                return std::make_unique<RedisConnection>(cfg);
            }
        );
    }
    
    // 先查缓存，再查数据库
    std::optional<std::string> GetUserName(int64_t user_id) {
        // 1. 尝试从 Redis 获取
        {
            auto redis = redis_pool_->CreateConnection();
            auto cached = redis->Get("user:" + std::to_string(user_id));
            if (cached.has_value()) {
                return cached;
            }
        }
        
        // 2. 从 MySQL 获取
        {
            auto mysql = mysql_pool_->CreateConnection();
            auto res = mysql->Query(
                "SELECT name FROM users WHERE id = ?", 
                {user_id}
            );
            if (res.Next()) {
                auto name = res.GetString("name").value_or("");
                
                // 3. 写入缓存
                auto redis = redis_pool_->CreateConnection();
                redis->SetEx("user:" + std::to_string(user_id), name, 3600);
                
                return name;
            }
        }
        
        return std::nullopt;
    }
    
private:
    std::shared_ptr<MySQLPool> mysql_pool_;
    std::shared_ptr<RedisPool> redis_pool_;
};
```

---

## API 参考

### TemplateConnectionPool\<T\>

#### 构造函数

```cpp
TemplateConnectionPool(
    std::shared_ptr<user_service::Config> global_config,  // 全局配置
    CreateFunc func                                        // 连接创建函数
);
```

| 参数 | 说明 |
|------|------|
| `global_config` | 全局配置对象，包含各数据库的子配置 |
| `func` | 连接创建回调，签名 `ConnectionPtr(const Config&)` |

**异常**：
- `std::invalid_argument` - 参数为空
- `std::runtime_error` - 初始化连接失败

#### CreateConnection

```cpp
ConnectionGuard CreateConnection();
```

获取一个连接守卫对象。

**返回值**：`ConnectionGuard` RAII 守卫对象

**异常**：`std::runtime_error` - 获取连接超时（5秒）

---

### ConnectionGuard

#### 构造函数

```cpp
ConnectionGuard(TemplateConnectionPool& pool);
```

从池中获取连接。

#### 操作符

| 操作符 | 说明 |
|--------|------|
| `operator->()` | 箭头运算符，访问连接方法 |
| `operator*()` | 解引用，获取连接引用 |

#### 方法

| 方法 | 说明 |
|------|------|
| `T* get() const` | 获取原始连接指针 |

---

### 类型别名

```cpp
// 连接类型 → 配置类型
Config_t<MySQLConnection>  // = MySQLConfig
Config_t<RedisConnection>  // = RedisConfig
```

---

## 设计原理

### 1. 编译期类型推导

使用模板特化实现连接类型到配置类型的映射，避免运行时类型判断：

```cpp
// ❌ 运行时判断（低效、易出错）
if (typeid(T) == typeid(MySQLConnection)) {
    config = global_config->mysql;
}

// ✅ 编译期推导（高效、类型安全）
template<>
struct ConnectionConfig<MySQLConnection> { 
    using type = MySQLConfig; 
};
```

### 2. if constexpr 分支

使用 C++17 的 `if constexpr` 实现编译期分支，只编译匹配的代码：

```cpp
void ExtractSubConfig(const std::shared_ptr<Config> global_config) {
    if constexpr (std::is_same_v<T, MySQLConnection>) {
        config_ = std::make_shared<const Config>(global_config->mysql);
    } else if constexpr (std::is_same_v<T, RedisConnection>) {
        config_ = std::make_shared<const Config>(global_config->redis);
    } else {
        // 不支持的类型在编译期报错
        static_assert(always_false_v<T>, "Unsupported connection type");
    }
}
```

### 3. RAII 资源管理

```
┌─────────────────────────────────────────────────────────────────────┐
│                         RAII 保证资源不泄漏                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   正常流程                          异常流程                         │
│   ─────────                        ─────────                        │
│                                                                      │
│   {                                 {                                │
│       auto guard = pool.Create();       auto guard = pool.Create(); │
│       guard->Query(...);                guard->Query(...);          │
│       guard->Execute(...);              throw SomeException();      │
│   }  ◄── 析构，自动归还               }  ◄── 析构，自动归还          │
│                                                                      │
│   ✅ 连接正常归还                    ✅ 异常时也能正确归还           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 4. 线程安全设计

```cpp
ConnectionPtr Acquire() {
    std::unique_lock<std::mutex> lk(mutex_);  // 加锁
    
    // 带超时的条件等待
    bool ret = cond_.wait_for(lk, std::chrono::seconds(5), [this]() {
        return !pool_.empty();
    });
    
    // ... 获取连接 ...
}

void Release(ConnectionPtr conn) {
    std::unique_lock<std::mutex> lk(mutex_);  // 加锁
    pool_.push_back(std::move(conn));
    cond_.notify_one();                        // 唤醒等待线程
}
```

---

## 最佳实践

### ✅ 推荐做法

```cpp
// 1. 使用 RAII 守卫，不要手动管理连接
{
    auto conn = pool->CreateConnection();
    conn->Query(...);
} // 自动归还

// 2. 连接池作为共享指针传递
class Service {
    std::shared_ptr<MySQLPool> pool_;  // ✅ 共享所有权
};

// 3. 检查连接有效性
auto conn = pool->CreateConnection();
if (!conn->Valid()) {
    return Result::Fail(ErrorCode::ServiceUnavailable);
}

// 4. 限制连接使用范围
Result<User> GetUser(int64_t id) {
    auto conn = pool->CreateConnection();  // 函数开始获取
    // ... 使用 conn ...
    return result;
}  // 函数结束归还
```

### ❌ 避免的做法

```cpp
// 1. 不要长时间持有连接
class BadService {
    ConnectionGuard conn_;  // ❌ 成员变量持有连接
};

// 2. 不要手动调用 Acquire/Release
auto conn = pool->Acquire();  // ❌ 私有方法，不应直接调用
pool->Release(std::move(conn));

// 3. 不要在循环中重复获取连接
for (auto& item : items) {
    auto conn = pool->CreateConnection();  // ❌ 每次循环都获取
    conn->Execute(...);
}

// ✅ 正确做法：循环外获取一次
auto conn = pool->CreateConnection();
for (auto& item : items) {
    conn->Execute(...);
}

// 4. 不要忽略异常
try {
    auto conn = pool->CreateConnection();
} catch (const std::runtime_error& e) {
    // ❌ 空 catch，吞掉异常
}
```

---

## 扩展指南

### 添加新的连接类型

```cpp
// 1. 定义连接类
class PostgresConnection {
public:
    explicit PostgresConnection(const PostgresConfig& config);
    bool Valid() const;
    // ... 其他方法 ...
};

// 2. 添加配置类型映射
template<>
struct ConnectionConfig<PostgresConnection> { 
    using type = PostgresConfig; 
};

// 3. 在 ExtractSubConfig 中添加分支
if constexpr (std::is_same_v<T, PostgresConnection>) {
    config_ = std::make_shared<const Config>(global_config->postgres);
}

// 4. 使用
using PostgresPool = TemplateConnectionPool<PostgresConnection>;
auto pool = std::make_shared<PostgresPool>(
    config,
    [](const PostgresConfig& cfg) {
        return std::make_unique<PostgresConnection>(cfg);
    }
);
```

---

## 文件结构

```
include/pool/
└── connection_pool.h     # 连接池模板实现（Header-only）
```

---

## 依赖关系

```
connection_pool.h
├── config/config.h       # 配置结构体
├── common/logger.h       # 日志
├── <memory>              # std::unique_ptr, std::shared_ptr
├── <deque>               # 连接队列
├── <mutex>               # 互斥锁
├── <condition_variable>  # 条件变量
└── <functional>          # std::function
```

