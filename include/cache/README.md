# Cache 模块设计文档

## 目录结构

```
include/cache/
└── redis_client.h      # Redis 客户端封装

src/cache/
├── CMakeLists.txt
└── redis_client.cpp    # Redis 客户端实现
```

---

## 1. 模块概述

### 1.1 设计目标

`RedisClient` 是对 `redis++` 库的业务层封装，提供：

- **统一的错误处理**：使用 `Result<T>` 区分"执行失败"与"数据不存在"
- **类型安全**：强类型返回值，避免空指针和异常
- **业务友好**：接口命名和参数设计贴合业务场景
- **异常安全**：所有方法都是 `noexcept`，通过 Result 返回错误

### 1.2 架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Cache 模块架构                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      业务层                                      │   │
│  │                                                                  │   │
│  │  SmsService          AuthService           其他服务...           │   │
│  │  (验证码存储)        (登录失败计数)                               │   │
│  └──────────────────────────┬──────────────────────────────────────┘   │
│                             │                                          │
│                             ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    RedisClient                                   │   │
│  │                                                                  │   │
│  │  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐             │   │
│  │  │ String 操作  │ │  Hash 操作   │ │  原子操作    │             │   │
│  │  │              │ │              │ │              │             │   │
│  │  │ Set/Get      │ │ HSet/HGet    │ │ Incr/Decr    │             │   │
│  │  │ SetPx/SetNx  │ │ HGetAll      │ │ IncrBy       │             │   │
│  │  └──────────────┘ └──────────────┘ └──────────────┘             │   │
│  │                                                                  │   │
│  │  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐             │   │
│  │  │ 通用操作     │ │  扫描操作    │ │  健康检查    │             │   │
│  │  │              │ │              │ │              │             │   │
│  │  │ Exists/Del   │ │ Keys/Scan    │ │ Ping         │             │   │
│  │  │ PExpire/PTTL │ │              │ │              │             │   │
│  │  └──────────────┘ └──────────────┘ └──────────────┘             │   │
│  │                                                                  │   │
│  │  统一返回：Result<T>    统一异常处理：noexcept + catch           │   │
│  └──────────────────────────┬──────────────────────────────────────┘   │
│                             │                                          │
│                             ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    sw::redis::Redis                              │   │
│  │                    (redis++ 库)                                  │   │
│  └──────────────────────────┬──────────────────────────────────────┘   │
│                             │                                          │
│                             ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    Redis Server                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心设计原则

### 2.1 统一返回 Result<T> 的设计

#### 2.1.1 设计动机

```
┌─────────────────────────────────────────────────────────────────────────┐
│                  传统设计 vs Result 设计                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ❌ 传统设计（抛异常 + 返回空值）                                        │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  std::optional<std::string> Get(const std::string& key) {        │   │
│  │      return redis_->get(key);  // 抛异常 or 返回 nullopt         │   │
│  │  }                                                               │   │
│  │                                                                  │   │
│  │  问题：                                                          │   │
│  │  • nullopt 是"key不存在"还是"Redis挂了"？无法区分                │   │
│  │  • 调用者需要 try-catch，代码冗长                                │   │
│  │  • 异常可能被遗漏，导致程序崩溃                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ✓ Result 设计（统一错误处理）                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Result<std::optional<std::string>> Get(const std::string& key)  │   │
│  │                                                                  │   │
│  │  返回值语义：                                                    │   │
│  │  • Result 失败        → Redis 执行出错（网络/超时等）            │   │
│  │  • Result 成功 + 有值 → Key 存在，获取到值                       │   │
│  │  • Result 成功 + 无值 → Key 不存在（正常业务情况）               │   │
│  │                                                                  │   │
│  │  优势：                                                          │   │
│  │  • 错误语义清晰                                                  │   │
│  │  • 编译期强制处理错误（Result 需要检查）                         │   │
│  │  • 无需 try-catch，代码简洁                                      │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 2.1.2 返回值类型设计

| 操作类型 | 返回类型 | 说明 |
|----------|----------|------|
| 写操作（Set） | `Result<void>` | 成功/失败 |
| 读操作（Get） | `Result<std::optional<std::string>>` | 成功+有值/成功+无值/失败 |
| 存在性检查 | `Result<bool>` | 存在/不存在/执行失败 |
| 数值操作 | `Result<int64_t>` | 操作后的值/执行失败 |
| 删除操作 | `Result<bool>` | 是否真的删了/执行失败 |

#### 2.1.3 错误处理宏

```cpp
// 统一的异常捕获宏
#define REDIS_CATCH_RETURN(ResultType, operation, key)                         \
    catch (const sw::redis::Error& e) {                                        \
        LOG_WARN("Redis {} failed: key={}, err={}", operation, key, e.what()); \
        return ResultType::Fail(ErrorCode::ServiceUnavailable, e.what());      \
    }

// 使用示例
Result<void> RedisClient::Set(const std::string& key, 
                               const std::string& value) noexcept {
    try {
        redis_->set(key, value);
        return Result<void>::Ok();
    }
    REDIS_CATCH_RETURN(Result<void>, "SET", key)
}
```

### 2.2 noexcept 设计原则

```cpp
// 所有公共方法都标记为 noexcept
Result<void> Set(const std::string& key, const std::string& value) noexcept;
Result<std::optional<std::string>> Get(const std::string& key) noexcept;
Result<bool> Exists(const std::string& key) noexcept;
// ...
```

**设计原因：**

| 原因 | 说明 |
|------|------|
| **异常安全** | 所有异常在内部捕获，转换为 Result |
| **性能优化** | 编译器可以进行更激进的优化 |
| **调用者友好** | 无需 try-catch，代码更简洁 |
| **容器兼容** | 可安全用于 STL 容器的移动操作 |

---

## 3. 类定义与接口

### 3.1 完整类定义

```cpp
class RedisClient {
public:
    // ==================== 构造函数 ====================
    RedisClient(const std::string& host, int port,
                const std::string& password, int db);
    explicit RedisClient(const RedisConfig& config);
    
    virtual ~RedisClient() = default;

    // ==================== 字符串操作 ====================
    virtual Result<void> Set(const std::string& key, 
                             const std::string& value) noexcept;
    
    virtual Result<void> SetPx(const std::string& key, 
                               const std::string& value,
                               std::chrono::milliseconds ttl) noexcept;
    
    virtual Result<std::optional<std::string>> Get(
                               const std::string& key) noexcept;
    
    virtual Result<bool> SetNx(const std::string& key, 
                               const std::string& value) noexcept;
    
    virtual Result<bool> SetNxPx(const std::string& key, 
                                 const std::string& value,
                                 std::chrono::milliseconds ttl) noexcept;

    // ==================== 通用操作 ====================
    virtual Result<bool> Exists(const std::string& key) noexcept;
    virtual Result<bool> Del(const std::string& key) noexcept;
    virtual Result<bool> PExpire(const std::string& key, 
                                 std::chrono::milliseconds ttl) noexcept;
    virtual Result<int64_t> PTTL(const std::string& key) noexcept;
    
    virtual Result<std::vector<std::string>> Keys(
                               const std::string& pattern) noexcept;
    virtual Result<std::vector<std::string>> Scan(
                               const std::string& pattern,
                               int64_t count = 100) noexcept;

    // ==================== Hash 操作 ====================
    virtual Result<void> HSet(const std::string& key,
                              const std::string& field,
                              const std::string& value) noexcept;
    
    virtual Result<void> HMSet(const std::string& key,
                               const std::vector<std::pair<std::string, std::string>>& fields) noexcept;
    
    virtual Result<std::optional<std::string>> HGet(
                               const std::string& key,
                               const std::string& field) noexcept;
    
    virtual Result<std::unordered_map<std::string, std::string>> HGetAll(
                               const std::string& key) noexcept;
    
    virtual Result<bool> HDel(const std::string& key, 
                              const std::string& field) noexcept;
    
    virtual Result<bool> HExists(const std::string& key, 
                                 const std::string& field) noexcept;

    // ==================== 原子操作 ====================
    virtual Result<int64_t> Incr(const std::string& key) noexcept;
    virtual Result<int64_t> IncrBy(const std::string& key, 
                                   int64_t increment) noexcept;
    virtual Result<int64_t> Decr(const std::string& key) noexcept;

    // ==================== 健康检查 ====================
    virtual Result<void> Ping() noexcept;

private:
    std::unique_ptr<sw::redis::Redis> redis_;
};
```

### 3.2 为什么使用 virtual

```cpp
// 所有公共方法都是 virtual 的
virtual Result<void> Set(...) noexcept;
virtual Result<std::optional<std::string>> Get(...) noexcept;
```

**原因：支持单元测试 Mock**

```cpp
// 测试代码中可以创建 Mock 类
class MockRedisClient : public RedisClient {
public:
    MockRedisClient() : RedisClient("", 0, "", 0) {}
    
    // 重写方法返回预设值
    Result<std::optional<std::string>> Get(const std::string& key) noexcept override {
        if (key == "test_key") {
            return Result<std::optional<std::string>>::Ok("test_value");
        }
        return Result<std::optional<std::string>>::Ok(std::nullopt);
    }
};
```

---

## 4. 使用方法详解

### 4.1 初始化

```cpp
#include "cache/redis_client.h"

// 方式1：详细参数构造
auto redis = std::make_shared<RedisClient>(
    "localhost",  // host
    6379,         // port
    "",           // password（空表示无密码）
    0             // db index
);

// 方式2：配置结构体构造（推荐）
RedisConfig config;
config.host = "localhost";
config.port = 6379;
config.password = "";
config.db = 0;
config.pool_size = 5;
config.connect_timeout_ms = 3000;
config.socket_timeout_ms = 3000;

auto redis = std::make_shared<RedisClient>(config);
```

### 4.2 字符串操作

```cpp
// ==================== SET - 设置值 ====================
auto set_result = redis->Set("user:1001:name", "张三");
if (!set_result.IsOk()) {
    LOG_ERROR("SET failed: {}", set_result.message);
    return;
}

// ==================== SET with TTL - 设置值+过期时间 ====================
auto setpx_result = redis->SetPx(
    "session:abc123",
    "user_data_json",
    std::chrono::milliseconds(30 * 60 * 1000)  // 30分钟
);

// ==================== GET - 获取值 ====================
auto get_result = redis->Get("user:1001:name");

if (!get_result.IsOk()) {
    // Redis 执行错误（网络问题等）
    LOG_ERROR("GET failed: {}", get_result.message);
    return;
}

if (get_result.Value().has_value()) {
    // Key 存在，获取到值
    std::string name = get_result.Value().value();
    LOG_INFO("User name: {}", name);
} else {
    // Key 不存在（正常业务情况）
    LOG_INFO("User not found");
}

// ==================== SETNX - 仅当 Key 不存在时设置 ====================
// 常用于分布式锁
auto setnx_result = redis->SetNx("lock:order:1001", "worker_1");
if (setnx_result.IsOk() && setnx_result.Value()) {
    // 获取锁成功
    // ... 执行业务逻辑 ...
    redis->Del("lock:order:1001");  // 释放锁
} else {
    // 锁已被占用
}

// ==================== SETNX with TTL - 分布式锁推荐方式 ====================
auto lock_result = redis->SetNxPx(
    "lock:order:1001",
    "worker_1",
    std::chrono::milliseconds(10000)  // 10秒自动过期，防止死锁
);
```

### 4.3 通用操作

```cpp
// ==================== EXISTS - 检查 Key 是否存在 ====================
auto exists_result = redis->Exists("user:1001:name");
if (exists_result.IsOk() && exists_result.Value()) {
    LOG_INFO("Key exists");
}

// ==================== DEL - 删除 Key ====================
auto del_result = redis->Del("user:1001:name");
if (del_result.IsOk()) {
    if (del_result.Value()) {
        LOG_INFO("Key deleted");
    } else {
        LOG_INFO("Key not found (already deleted)");
    }
}

// ==================== PEXPIRE - 设置过期时间 ====================
auto expire_result = redis->PExpire(
    "session:abc123",
    std::chrono::milliseconds(60000)  // 60秒
);

// ==================== PTTL - 获取剩余过期时间 ====================
auto ttl_result = redis->PTTL("session:abc123");
if (ttl_result.IsOk()) {
    int64_t ttl_ms = ttl_result.Value();
    if (ttl_ms > 0) {
        LOG_INFO("TTL: {}ms", ttl_ms);
    } else if (ttl_ms == -1) {
        LOG_INFO("Key exists but no TTL");
    } else if (ttl_ms == -2) {
        LOG_INFO("Key not found");
    }
}

// ==================== SCAN - 增量扫描（推荐用于生产） ====================
auto scan_result = redis->Scan("user:*", 100);
if (scan_result.IsOk()) {
    for (const auto& key : scan_result.Value()) {
        LOG_INFO("Found key: {}", key);
    }
}

// ==================== KEYS - 模式匹配（仅用于开发调试） ====================
// ⚠️ 警告：生产环境禁用，会阻塞 Redis
auto keys_result = redis->Keys("user:*");
```

### 4.4 Hash 操作

```cpp
// ==================== HSET - 设置 Hash 字段 ====================
auto hset_result = redis->HSet("user:1001", "name", "张三");
auto hset_result2 = redis->HSet("user:1001", "age", "25");

// ==================== HMSET - 批量设置 Hash 字段 ====================
auto hmset_result = redis->HMSet("user:1002", {
    {"name", "李四"},
    {"age", "30"},
    {"city", "北京"}
});

// ==================== HGET - 获取 Hash 字段 ====================
auto hget_result = redis->HGet("user:1001", "name");
if (hget_result.IsOk() && hget_result.Value().has_value()) {
    std::string name = hget_result.Value().value();
}

// ==================== HGETALL - 获取所有字段 ====================
auto hgetall_result = redis->HGetAll("user:1001");
if (hgetall_result.IsOk()) {
    for (const auto& [field, value] : hgetall_result.Value()) {
        LOG_INFO("  {}: {}", field, value);
    }
}

// ==================== HEXISTS - 检查字段是否存在 ====================
auto hexists_result = redis->HExists("user:1001", "email");

// ==================== HDEL - 删除 Hash 字段 ====================
auto hdel_result = redis->HDel("user:1001", "age");
```

### 4.5 原子操作

```cpp
// ==================== INCR - 自增 1 ====================
// 常用于计数器、限流等场景
auto incr_result = redis->Incr("counter:page_view");
if (incr_result.IsOk()) {
    int64_t current = incr_result.Value();
    LOG_INFO("Page views: {}", current);
}

// ==================== INCRBY - 自增指定值 ====================
auto incrby_result = redis->IncrBy("user:1001:points", 100);

// ==================== DECR - 自减 1 ====================
auto decr_result = redis->Decr("inventory:product:1001");

// ==================== 限流示例 ====================
std::string key = "rate_limit:user:1001";
auto count_result = redis->Incr(key);

if (count_result.IsOk()) {
    int64_t count = count_result.Value();
    
    if (count == 1) {
        // 第一次访问，设置过期时间
        redis->PExpire(key, std::chrono::milliseconds(60000));  // 1分钟窗口
    }
    
    if (count > 100) {
        // 超过限制
        return Result<void>::Fail(ErrorCode::RateLimited, "请求过于频繁");
    }
}
```

### 4.6 健康检查

```cpp
// ==================== PING - 检查 Redis 连接 ====================
auto ping_result = redis->Ping();
if (ping_result.IsOk()) {
    LOG_INFO("Redis is healthy");
} else {
    LOG_ERROR("Redis is down: {}", ping_result.message);
}
```

---

## 5. 业务场景示例

### 5.1 验证码存储（SmsService）

```cpp
class SmsService {
public:
    SmsService(std::shared_ptr<RedisClient> redis, const SmsConfig& config)
        : redis_(redis), config_(config) {}
    
    Result<int32_t> SendCaptcha(SmsScene scene, const std::string& mobile) {
        // 1. 检查锁定状态
        auto lock_key = LockKey(scene, mobile);
        auto lock_exists = redis_->Exists(lock_key);
        if (lock_exists.IsOk() && lock_exists.Value()) {
            auto ttl = redis_->PTTL(lock_key);
            int64_t seconds = ttl.IsOk() ? ttl.Value() / 1000 : 60;
            return Result<int32_t>::Fail(
                ErrorCode::RateLimited,
                fmt::format("请{}秒后再试", seconds)
            );
        }
        
        // 2. 检查发送间隔
        auto interval_key = IntervalKey(mobile);
        auto interval_exists = redis_->Exists(interval_key);
        if (interval_exists.IsOk() && interval_exists.Value()) {
            return Result<int32_t>::Fail(ErrorCode::RateLimited, "请稍后再试");
        }
        
        // 3. 生成并存储验证码
        std::string code = GenerateCode();
        auto code_key = CaptchaKey(scene, mobile);
        
        auto set_result = redis_->SetPx(
            code_key,
            code,
            std::chrono::milliseconds(config_.code_ttl_seconds * 1000)
        );
        if (!set_result.IsOk()) {
            return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable);
        }
        
        // 4. 设置发送间隔
        redis_->SetPx(
            interval_key,
            "1",
            std::chrono::milliseconds(config_.send_interval_seconds * 1000)
        );
        
        // 5. 发送短信...
        
        return Result<int32_t>::Ok(config_.send_interval_seconds);
    }
    
    Result<void> VerifyCaptcha(SmsScene scene, 
                               const std::string& mobile,
                               const std::string& input_code) {
        // 1. 获取正确的验证码
        auto code_result = redis_->Get(CaptchaKey(scene, mobile));
        if (!code_result.IsOk()) {
            return Result<void>::Fail(ErrorCode::ServiceUnavailable);
        }
        if (!code_result.Value().has_value()) {
            return Result<void>::Fail(ErrorCode::CaptchaExpired, "验证码已过期");
        }
        
        const std::string& stored_code = code_result.Value().value();
        
        // 2. 验证码比对
        if (input_code != stored_code) {
            // 增加错误计数
            auto count_key = VerifyCountKey(scene, mobile);
            auto count_result = redis_->Incr(count_key);
            
            if (count_result.IsOk()) {
                int64_t count = count_result.Value();
                
                // 设置计数器过期时间
                redis_->PExpire(count_key, 
                    std::chrono::milliseconds(config_.retry_ttl_seconds * 1000));
                
                if (count >= config_.max_retry_count) {
                    // 触发锁定
                    redis_->SetPx(
                        LockKey(scene, mobile),
                        "1",
                        std::chrono::milliseconds(config_.lock_seconds * 1000)
                    );
                    redis_->Del(CaptchaKey(scene, mobile));
                    redis_->Del(count_key);
                    
                    return Result<void>::Fail(ErrorCode::AccountLocked);
                }
            }
            
            return Result<void>::Fail(ErrorCode::CaptchaWrong);
        }
        
        // 3. 验证成功，清除错误计数
        redis_->Del(VerifyCountKey(scene, mobile));
        
        return Result<void>::Ok();
    }
    
private:
    std::string CaptchaKey(SmsScene scene, const std::string& mobile) {
        return "sms:code:" + SceneName(scene) + ":" + mobile;
    }
    
    std::string IntervalKey(const std::string& mobile) {
        return "sms:interval:" + mobile;
    }
    
    std::string VerifyCountKey(SmsScene scene, const std::string& mobile) {
        return "sms:verify_count:" + SceneName(scene) + ":" + mobile;
    }
    
    std::string LockKey(SmsScene scene, const std::string& mobile) {
        return "sms:lock:" + SceneName(scene) + ":" + mobile;
    }
    
    std::shared_ptr<RedisClient> redis_;
    SmsConfig config_;
};
```

### 5.2 登录失败计数（AuthService）

```cpp
class AuthService {
public:
    Result<void> CheckLoginFailedAttempts(const std::string& mobile) {
        std::string key = "login:fail:" + mobile;
        
        auto count_res = redis_->Get(key);
        if (!count_res.IsOk()) {
            // Redis 故障，记录日志但不阻止登录
            LOG_WARN("Check login attempts failed: {}", count_res.message);
            return Result<void>::Ok();
        }
        
        if (count_res.Value().has_value()) {
            int count = std::stoi(count_res.Value().value());
            
            if (count >= config_->login.max_failed_attempts) {
                // 获取剩余锁定时间
                auto ttl_res = redis_->PTTL(key);
                int64_t minutes = 30;  // 默认值
                
                if (ttl_res.IsOk() && ttl_res.Value() > 0) {
                    minutes = (ttl_res.Value() + 59999) / 60000;
                }
                
                return Result<void>::Fail(
                    ErrorCode::AccountLocked,
                    fmt::format("登录失败次数过多，请{}分钟后再试", minutes)
                );
            }
        }
        
        return Result<void>::Ok();
    }
    
    void RecordLoginFailure(const std::string& mobile) {
        std::string key = "login:fail:" + mobile;
        
        auto incr_res = redis_->Incr(key);
        if (!incr_res.IsOk()) {
            LOG_WARN("Record login failure failed: {}", incr_res.message);
            return;
        }
        
        int64_t count = incr_res.Value();
        
        if (count == config_->login.max_failed_attempts) {
            // 刚好达到上限，设置锁定时间
            redis_->PExpire(key, 
                std::chrono::milliseconds(config_->login.lock_duration_seconds * 1000));
            LOG_WARN("Account locked: mobile={}", mobile);
        } else if (count < config_->login.max_failed_attempts) {
            // 未达上限，确保有窗口期过期时间
            auto ttl_res = redis_->PTTL(key);
            if (!ttl_res.IsOk() || ttl_res.Value() < 0) {
                redis_->PExpire(key,
                    std::chrono::milliseconds(config_->login.failed_attempts_window * 1000));
            }
        }
    }
    
    void ClearLoginFailure(const std::string& mobile) {
        redis_->Del("login:fail:" + mobile);
    }
    
private:
    std::shared_ptr<RedisClient> redis_;
    std::shared_ptr<Config> config_;
};
```

### 5.3 分布式锁

```cpp
class DistributedLock {
public:
    DistributedLock(std::shared_ptr<RedisClient> redis,
                    const std::string& lock_name,
                    std::chrono::milliseconds ttl = std::chrono::seconds(10))
        : redis_(redis)
        , key_("lock:" + lock_name)
        , value_(GenerateUniqueId())
        , ttl_(ttl)
        , acquired_(false) 
    {}
    
    ~DistributedLock() {
        Release();
    }
    
    bool Acquire() {
        auto result = redis_->SetNxPx(key_, value_, ttl_);
        if (result.IsOk() && result.Value()) {
            acquired_ = true;
            return true;
        }
        return false;
    }
    
    void Release() {
        if (!acquired_) return;
        
        // 只删除自己持有的锁（防止误删其他进程的锁）
        auto get_result = redis_->Get(key_);
        if (get_result.IsOk() && 
            get_result.Value().has_value() && 
            get_result.Value().value() == value_) {
            redis_->Del(key_);
        }
        
        acquired_ = false;
    }
    
    bool Extend(std::chrono::milliseconds new_ttl) {
        if (!acquired_) return false;
        
        // 检查是否还持有锁
        auto get_result = redis_->Get(key_);
        if (!get_result.IsOk() || 
            !get_result.Value().has_value() ||
            get_result.Value().value() != value_) {
            acquired_ = false;
            return false;
        }
        
        auto expire_result = redis_->PExpire(key_, new_ttl);
        return expire_result.IsOk() && expire_result.Value();
    }
    
private:
    std::string GenerateUniqueId() {
        // 生成唯一标识（UUID 或 时间戳+随机数）
        return std::to_string(std::chrono::system_clock::now()
            .time_since_epoch().count()) + "_" + std::to_string(rand());
    }
    
    std::shared_ptr<RedisClient> redis_;
    std::string key_;
    std::string value_;
    std::chrono::milliseconds ttl_;
    bool acquired_;
};

// 使用示例
void ProcessOrder(const std::string& order_id) {
    DistributedLock lock(redis, "order:" + order_id);
    
    if (!lock.Acquire()) {
        LOG_WARN("Failed to acquire lock for order: {}", order_id);
        return;
    }
    
    // 处理订单...
    // lock 析构时自动释放
}
```

---

## 6. 配置说明

### 6.1 RedisConfig

```yaml
redis:
  host: "localhost"
  port: 6379
  password: ""                    # 无密码留空
  db: 0                           # 数据库索引
  pool_size: 5                    # 连接池大小
  
  # 超时配置（毫秒）
  connect_timeout_ms: 3000        # 连接超时
  socket_timeout_ms: 3000         # 读写超时
  wait_timeout_ms: 100            # 等待可用连接超时
```

### 6.2 连接池配置

```cpp
RedisClient::RedisClient(const RedisConfig& config) {
    sw::redis::ConnectionOptions conn_opt;
    conn_opt.host = config.host;
    conn_opt.port = config.port;
    
    if (!config.password.empty()) {
        conn_opt.password = config.password;
    }
    
    conn_opt.db = config.db;
    
    // 超时配置
    if (config.connect_timeout_ms) {
        conn_opt.connect_timeout = 
            std::chrono::milliseconds(*config.connect_timeout_ms);
    }
    if (config.socket_timeout_ms) {
        conn_opt.socket_timeout = 
            std::chrono::milliseconds(*config.socket_timeout_ms);
    }
    
    // 连接池配置
    sw::redis::ConnectionPoolOptions pool_opt;
    pool_opt.size = config.pool_size;
    pool_opt.wait_timeout = std::chrono::milliseconds(config.wait_timeout_ms);
    
    redis_ = std::make_unique<sw::redis::Redis>(conn_opt, pool_opt);
}
```

---

## 7. 错误处理最佳实践

### 7.1 三层错误检查模式

```cpp
auto result = redis->Get("some_key");

// 第一层：检查执行是否成功
if (!result.IsOk()) {
    // Redis 故障（网络/超时等）
    LOG_ERROR("Redis error: {}", result.message);
    return Result<void>::Fail(ErrorCode::ServiceUnavailable);
}

// 第二层：检查数据是否存在
if (!result.Value().has_value()) {
    // Key 不存在（正常业务情况）
    return Result<void>::Fail(ErrorCode::NotFound, "数据不存在");
}

// 第三层：使用数据
const std::string& value = result.Value().value();
```

### 7.2 简化写法

```cpp
// 链式调用（C++17）
auto value = redis->Get("key")
    .and_then([](auto opt) -> Result<std::string> {
        if (opt.has_value()) {
            return Result<std::string>::Ok(opt.value());
        }
        return Result<std::string>::Fail(ErrorCode::NotFound);
    });

// 或使用辅助宏
#define REDIS_GET_OR_RETURN(result_var, redis_ptr, key, default_val)  \
    auto result_var##_res = (redis_ptr)->Get(key);                    \
    if (!result_var##_res.IsOk()) {                                   \
        LOG_ERROR("Redis GET failed: {}", result_var##_res.message);  \
        return default_val;                                            \
    }                                                                  \
    auto result_var = result_var##_res.Value().value_or(default_val);
```

---

## 8. 相关文件

| 文件 | 说明 |
|------|------|
| `include/cache/redis_client.h` | 客户端接口定义 |
| `src/cache/redis_client.cpp` | 客户端实现 |
| `include/config/config.h` | RedisConfig 结构体 |
| `include/common/result.h` | Result 模板类 |
| `include/common/error_codes.h` | 错误码定义 |

