# Exception 模块 - 异常处理体系

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

`exception` 模块定义了系统的**统一异常处理体系**，提供了针对不同场景的异常类型，支持错误分类、重试判断和错误码转换。

### 核心特性

| 特性 | 说明 |
|------|------|
| 🎯 **分类明确** | 按来源分类：MySQL 异常、业务异常 |
| 🔄 **重试支持** | 可判断异常是否值得重试 |
| 🔢 **错误码关联** | 与 ErrorCode 体系无缝集成 |
| 🔍 **信息丰富** | 携带详细错误信息（错误码、索引名等） |
| ⚡ **零开销** | Header-only 实现，编译期确定类型 |

### 模块组成

| 文件 | 说明 |
|------|------|
| `exception.h` | 统一入口头文件 |
| `mysql_exception.h` | MySQL 相关异常 |
| `client_exception.h` | 业务/客户端异常 |

---

## 架构设计

```
┌─────────────────────────────────────────────────────────────────────┐
│                        异常处理体系架构                               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│                       std::exception                                 │
│                             │                                        │
│               ┌─────────────┼─────────────┐                          │
│               │             │             │                          │
│               ▼             ▼             ▼                          │
│      std::runtime_error           std::logic_error                   │
│               │                         │                            │
│      ┌────────┴────────┐                │                            │
│      │                 │                │                            │
│      ▼                 ▼                ▼                            │
│  ┌────────────┐  ┌────────────┐  ┌────────────────┐                  │
│  │ MySQL      │  │ Client     │  │ MySQLBuild     │                  │
│  │ Exception  │  │ Exception  │  │ Exception      │                  │
│  │            │  │            │  │                │                  │
│  │ • errno_   │  │ • code_    │  │ • 参数不匹配   │                  │
│  │ • IsRetry  │  │ • detail_  │  │ • SQL语法错误  │                  │
│  └─────┬──────┘  └────────────┘  └────────────────┘                  │
│        │                                                             │
│        ├─────────────────────┐                                       │
│        ▼                     ▼                                       │
│  ┌────────────────┐   ┌────────────────┐                             │
│  │ MySQLDuplicate │   │ MySQLResult    │                             │
│  │ KeyException   │   │ Exception      │                             │
│  │                │   │                │                             │
│  │ • key_name_    │   │ • 结果集访问   │                             │
│  └────────────────┘   └────────────────┘                             │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 异常处理流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                          异常处理流程                                 │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  数据库层 (mysql_connection.cpp / user_db.cpp)                       │
│  ─────────────────────────────────────────────                       │
│                                                                      │
│  mysql_query() 返回错误                                              │
│        │                                                             │
│        ▼                                                             │
│  ThrowMySQLException(err_code, err_msg)                              │
│        │                                                             │
│        ├──► err_code == 1062 ──► throw MySQLDuplicateKeyException    │
│        │                                                             │
│        └──► 其他错误 ──────────► throw MySQLException                 │
│                                                                      │
│                                                                      │
│  业务层 (user_db.cpp / auth_service.cpp)                             │
│  ────────────────────────────────────────                            │
│                                                                      │
│  try {                                                               │
│      user_db->Create(user);                                          │
│  }                                                                   │
│  catch (MySQLDuplicateKeyException& e) {                             │
│      if (e.key_name() == "uk_mobile") {                              │
│          return Result::Fail(ErrorCode::MobileTaken);                │
│      }                                                               │
│  }                                                                   │
│  catch (MySQLException& e) {                                         │
│      if (e.IsRetryable()) {                                          │
│          // 可重试逻辑                                                │
│      }                                                               │
│      return Result::Fail(ErrorCode::ServiceUnavailable);             │
│  }                                                                   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. MySQLException - MySQL 异常基类

```cpp
// include/exception/mysql_exception.h

class MySQLException : public std::runtime_error {
public:
    MySQLException(int mysql_errno, const std::string& msg);
    
    int mysql_errno() const;   // 获取 MySQL 错误码
    bool IsRetryable() const;  // 是否可重试
};
```

**可重试的错误码：**

| 错误码 | 说明 | 场景 |
|--------|------|------|
| 1213 | 死锁（Deadlock） | 事务并发冲突 |
| 2002 | Socket 错误 | 临时网络问题 |
| 2003 | 无法连接主机 | 服务器可能在重启 |
| 2006 | 服务断开 | 连接空闲超时 |
| 2013 | 连接丢失 | 网络瞬断 |

### 2. MySQLDuplicateKeyException - 唯一键冲突

```cpp
class MySQLDuplicateKeyException : public MySQLException {
public:
    MySQLDuplicateKeyException(int mysql_errno, const std::string& msg);
    
    const std::string& key_name() const;  // 获取冲突的索引名
};
```

**错误信息解析：**

```
MySQL 错误格式: "Duplicate entry '13800138000' for key 'users.uk_mobile'"
                                                             ↑
                                                   key_name() 返回 "uk_mobile"
```

### 3. MySQLBuildException - SQL 构建异常

```cpp
class MySQLBuildException : public std::logic_error {
public:
    explicit MySQLBuildException(const std::string& msg);
};
```

**触发场景：**
- 参数数量与 `?` 占位符不匹配
- SQL 模板构建错误

### 4. MySQLResultException - 结果集异常

```cpp
class MySQLResultException : public std::runtime_error {
public:
    explicit MySQLResultException(const std::string& msg);
};
```

**触发场景：**
- 未调用 `Next()` 就访问数据
- 列索引越界
- 列名不存在

### 5. ClientException - 客户端/业务异常

```cpp
// include/exception/client_exception.h

class ClientException : public std::runtime_error {
public:
    explicit ClientException(ErrorCode code);
    ClientException(ErrorCode code, const std::string& detail);
    
    ErrorCode code() const;            // 获取错误码
    const std::string& detail() const; // 获取详细信息
};
```

### 6. 辅助函数与宏

```cpp
// 根据错误码自动抛出对应异常类型
inline void ThrowMySQLException(unsigned int err_code, const std::string& err_msg);

// 便捷抛出宏
#define THROW_CLIENT_ERROR(code)           throw ClientException(code)
#define THROW_CLIENT_ERROR_MSG(code, msg)  throw ClientException(code, msg)
```

---

## 使用指南

### MySQL 异常处理

#### 基本用法

```cpp
#include "exception/mysql_exception.h"

Result<UserEntity> UserDB::Create(const UserEntity& user) {
    try {
        auto conn = pool_->CreateConnection();
        
        conn->Execute(
            "INSERT INTO users (uuid, mobile, password_hash) VALUES (?, ?, ?)",
            {user.uuid, user.mobile, user.password_hash}
        );
        
        return FindByUUID(user.uuid);
        
    } catch (const MySQLDuplicateKeyException& e) {
        // 唯一键冲突：根据索引名返回具体错误码
        LOG_WARN("Duplicate key: {}, key={}", e.what(), e.key_name());
        
        if (e.key_name() == "uk_mobile") {
            return Result<UserEntity>::Fail(ErrorCode::MobileTaken);
        }
        if (e.key_name() == "uk_uuid") {
            return Result<UserEntity>::Fail(ErrorCode::UserAlreadyExists);
        }
        return Result<UserEntity>::Fail(ErrorCode::UserAlreadyExists);
        
    } catch (const MySQLException& e) {
        // 通用 MySQL 错误
        LOG_ERROR("MySQL error: errno={}, msg={}", e.mysql_errno(), e.what());
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable);
    }
}
```

#### 重试机制

```cpp
#include "exception/mysql_exception.h"

template<typename Func>
auto ExecuteWithRetry(Func&& func, int max_retries = 3) {
    int attempts = 0;
    
    while (true) {
        try {
            return func();
            
        } catch (const MySQLException& e) {
            ++attempts;
            
            // 判断是否可重试
            if (!e.IsRetryable() || attempts >= max_retries) {
                throw;  // 不可重试或次数耗尽
            }
            
            LOG_WARN("MySQL error (retryable), attempt {}/{}: {}", 
                     attempts, max_retries, e.what());
            
            // 指数退避
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100 * (1 << attempts))
            );
        }
    }
}

// 使用示例
auto result = ExecuteWithRetry([&]() {
    return conn->Query("SELECT * FROM users WHERE id = ?", {user_id});
});
```

#### 结果集错误处理

```cpp
#include "exception/mysql_exception.h"

void ProcessResult(MySQLResult& res) {
    // 错误示例：未调用 Next() 就访问数据
    try {
        auto id = res.GetInt("id");  // 抛出 MySQLResultException
    } catch (const MySQLResultException& e) {
        LOG_ERROR("Result access error: {}", e.what());
    }
    
    // 正确用法
    while (res.Next()) {
        auto id = res.GetInt("id").value_or(0);
        auto name = res.GetString("name").value_or("");
    }
}
```

---

### ClientException 使用

#### 基本用法

```cpp
#include "exception/client_exception.h"

void ValidateUser(const UserEntity& user) {
    if (user.mobile.empty()) {
        throw ClientException(ErrorCode::InvalidArgument, "手机号不能为空");
    }
    
    if (user.disabled) {
        THROW_CLIENT_ERROR(ErrorCode::UserDisabled);
    }
}

// 调用方
try {
    ValidateUser(user);
} catch (const ClientException& e) {
    LOG_WARN("Validation failed: code={}, detail={}", 
             static_cast<int>(e.code()), e.detail());
    return Result<void>::Fail(e.code(), e.what());
}
```

#### 在 Service 层使用

```cpp
#include "exception/client_exception.h"

Result<AuthResult> AuthService::LoginByPassword(const std::string& mobile,
                                                 const std::string& password) {
    try {
        // 参数校验
        if (mobile.empty()) {
            THROW_CLIENT_ERROR_MSG(ErrorCode::InvalidArgument, "手机号不能为空");
        }
        
        // 查询用户
        auto user_res = user_db_->FindByMobile(mobile);
        if (!user_res.IsOk()) {
            THROW_CLIENT_ERROR(ErrorCode::UserNotFound);
        }
        
        auto& user = user_res.Value();
        
        // 检查状态
        if (user.disabled) {
            THROW_CLIENT_ERROR(ErrorCode::UserDisabled);
        }
        
        // 验证密码
        if (!PasswordHelper::Verify(password, user.password_hash)) {
            THROW_CLIENT_ERROR(ErrorCode::WrongPassword);
        }
        
        // 生成 Token
        auto tokens = jwt_srv_->GenerateTokenPair(user);
        
        return Result<AuthResult>::Ok({user, tokens});
        
    } catch (const ClientException& e) {
        return Result<AuthResult>::Fail(e.code(), e.what());
    }
}
```

---

## API 参考

### MySQLException

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `mysql_errno()` | `int` | MySQL 原生错误码 |
| `IsRetryable()` | `bool` | 是否可重试 |
| `what()` | `const char*` | 错误消息（继承） |

### MySQLDuplicateKeyException

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `key_name()` | `const string&` | 冲突的索引名 |
| （继承 MySQLException 所有方法） | | |

### MySQLBuildException

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `what()` | `const char*` | 错误消息（继承） |

### MySQLResultException

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `what()` | `const char*` | 错误消息（继承） |

### ClientException

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `code()` | `ErrorCode` | 业务错误码 |
| `detail()` | `const string&` | 详细错误信息 |
| `what()` | `const char*` | 完整错误消息（继承） |

### 辅助函数

| 函数 | 说明 |
|------|------|
| `ThrowMySQLException(code, msg)` | 根据错误码抛出对应 MySQL 异常 |

### 便捷宏

| 宏 | 展开 |
|----|------|
| `THROW_CLIENT_ERROR(code)` | `throw ClientException(code)` |
| `THROW_CLIENT_ERROR_MSG(code, msg)` | `throw ClientException(code, msg)` |

---

## 设计原理

### 1. 异常继承体系选择

```
┌─────────────────────────────────────────────────────────────────────┐
│                       异常基类选择原则                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   std::runtime_error                  std::logic_error               │
│   ─────────────────                   ────────────────               │
│   • 运行时错误                         • 编程逻辑错误                  │
│   • 外部因素导致                       • 代码缺陷导致                  │
│   • 可恢复/可重试                      • 应在开发阶段修复              │
│                                                                      │
│   继承 runtime_error:                 继承 logic_error:               │
│   ─────────────────────               ────────────────────           │
│   • MySQLException                    • MySQLBuildException          │
│     (网络/服务器故障)                   (SQL参数不匹配)                │
│   • MySQLResultException                                             │
│     (结果集访问错误)                                                  │
│   • ClientException                                                  │
│     (业务逻辑错误)                                                    │
│                                                                      │
│   为什么 MySQLBuildException 用 logic_error?                         │
│   ─────────────────────────────────────────                          │
│   SQL 参数数量不匹配是编程错误，不是运行时环境问题：                    │
│   • 应该在开发/测试阶段发现并修复                                     │
│   • 生产环境不应该出现                                                │
│   • 不应该被重试                                                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 2. 异常 vs Result 使用场景

```
┌─────────────────────────────────────────────────────────────────────┐
│                    异常 vs Result 使用场景                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  使用异常:                           使用 Result:                     │
│  ─────────                           ────────────                    │
│  • 底层基础设施错误                   • 可预期的业务错误               │
│    (数据库连接、网络)                   (用户不存在、密码错误)         │
│  • 需要跨多层传播                     • 需要精细控制流程               │
│  • 不可恢复的严重错误                 • API 返回值                     │
│  • 携带详细上下文                     • 不想中断控制流                 │
│                                                                      │
│  本项目分层模式:                                                      │
│  ───────────────                                                     │
│                                                                      │
│    MySQLConnection                                                   │
│         │                                                            │
│         │  抛出 MySQLException / MySQLDuplicateKeyException          │
│         ▼                                                            │
│      UserDB                                                          │
│         │                                                            │
│         │  catch 异常 → 转换为 Result<T>                             │
│         ▼                                                            │
│    AuthService / UserService                                         │
│         │                                                            │
│         │  处理 Result，可能抛出 ClientException                     │
│         ▼                                                            │
│    AuthHandler / UserHandler                                         │
│         │                                                            │
│         │  处理 Result → 设置 gRPC Response                          │
│         ▼                                                            │
│      gRPC 响应                                                       │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 3. 唯一键索引名解析

```cpp
// MySQL 5.7+ 错误信息格式固定
// "Duplicate entry 'alice' for key 'database.index_name'"

static std::string ParseKeyName(const std::string& msg) {
    //                              查找最后的 '.'
    //                                    ↓
    // "Duplicate entry '13800138000' for key 'users.uk_mobile'"
    //                                              ↑        ↑
    //                                            dot       end
    
    auto dot = msg.rfind('.');
    auto end = msg.rfind('\'');
    
    if (dot == std::string::npos || 
        end == std::string::npos || 
        dot >= end) {
        return "";  // 格式异常，返回空
    }
    
    return msg.substr(dot + 1, end - dot - 1);  // 提取 "uk_mobile"
}
```

### 4. 重试判断逻辑

```cpp
bool MySQLException::IsRetryable() const {
    return errno_ == 1213 ||  // 死锁 - 事务冲突，重试可能成功
           errno_ == 2002 ||  // Socket 错误 - 临时网络问题
           errno_ == 2003 ||  // 无法连接 - 服务器可能重启中
           errno_ == 2006 ||  // 服务断开 - 连接空闲超时
           errno_ == 2013;    // 连接丢失 - 网络瞬断
}

// 不可重试的典型错误:
// 1045 - Access denied (密码错误)
// 1049 - Unknown database (数据库不存在)
// 1064 - Syntax error (SQL语法错误)
// 1062 - Duplicate entry (唯一键冲突，业务问题)
```

---

## 最佳实践

### ✅ 推荐做法

```cpp
// 1. 优先捕获具体异常，再捕获基类
try {
    conn->Execute(sql, params);
} catch (const MySQLDuplicateKeyException& e) {
    // ✅ 先处理具体异常
    return Result::Fail(ErrorCode::MobileTaken);
} catch (const MySQLException& e) {
    // ✅ 再处理通用异常
    return Result::Fail(ErrorCode::ServiceUnavailable);
}

// 2. 使用 IsRetryable() 决定是否重试
catch (const MySQLException& e) {
    if (e.IsRetryable()) {
        LOG_WARN("Retryable error: {}", e.what());
        // 实现重试逻辑
    }
}

// 3. 使用 key_name() 区分冲突类型
catch (const MySQLDuplicateKeyException& e) {
    if (e.key_name() == "uk_mobile") {
        return Result::Fail(ErrorCode::MobileTaken);
    } else if (e.key_name() == "uk_email") {
        return Result::Fail(ErrorCode::EmailTaken);
    }
}

// 4. 在 DB/Service 层将异常转换为 Result
Result<UserEntity> UserDB::FindById(int64_t id) {
    try {
        // ... 数据库操作
    } catch (const MySQLException& e) {
        LOG_ERROR("DB error: {}", e.what());
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable);
    }
}

// 5. 使用便捷宏简化代码
if (user.disabled) {
    THROW_CLIENT_ERROR(ErrorCode::UserDisabled);
}

THROW_CLIENT_ERROR_MSG(ErrorCode::InvalidArgument, "手机号格式错误");
```

### ❌ 避免的做法

```cpp
// 1. 不要用 catch(...) 吞掉所有异常
try {
    // ...
} catch (...) {  // ❌ 隐藏问题
    // 什么都不做
}

// 2. 不要只捕获 std::exception
try {
    // ...
} catch (const std::exception& e) {  // ❌ 太宽泛，丢失类型信息
    LOG_ERROR("{}", e.what());
}

// 3. 不要在析构函数中抛出异常
~MyClass() {
    THROW_CLIENT_ERROR(ErrorCode::Internal);  // ❌ 可能导致 terminate
}

// 4. 不要使用魔数判断错误码
if (e.mysql_errno() == 1062) {  // ❌ 魔数
    // ...
}
// ✅ 使用类型判断
catch (const MySQLDuplicateKeyException& e) {
    // ...
}

// 5. 不要在循环内部重复 try-catch
for (auto& item : items) {
    try {  // ❌ 性能开销
        process(item);
    } catch (...) {}
}
// ✅ 将整个循环放在 try 中
try {
    for (auto& item : items) {
        process(item);
    }
} catch (...) {}
```

---

## MySQL 错误码速查表

| 错误码 | 说明 | 可重试 | 建议处理 |
|--------|------|:------:|----------|
| 1062 | 唯一键冲突 | ❌ | 抛 `MySQLDuplicateKeyException`，按 key_name 返回错误码 |
| 1213 | 死锁 | ✅ | 自动重试 |
| 2002 | Socket 错误 | ✅ | 自动重试 |
| 2003 | 无法连接 | ✅ | 自动重试 |
| 2006 | 服务断开 | ✅ | 自动重试 |
| 2013 | 连接丢失 | ✅ | 自动重试 |
| 1045 | 认证失败 | ❌ | 检查配置 |
| 1049 | 数据库不存在 | ❌ | 检查配置 |
| 1064 | SQL 语法错误 | ❌ | 修复代码 |
| 其他 | 通用错误 | ❌ | 返回 `ServiceUnavailable` |

---

## 文件结构

```
include/exception/
├── exception.h           # 统一入口（包含 mysql_exception.h）
├── mysql_exception.h     # MySQL 异常类定义
│   ├── MySQLException           - 基类
│   ├── MySQLDuplicateKeyException - 唯一键冲突
│   ├── MySQLBuildException      - SQL 构建错误
│   ├── MySQLResultException     - 结果集访问错误
│   └── ThrowMySQLException()    - 辅助抛出函数
│
└── client_exception.h    # 客户端/业务异常类
    ├── ClientException          - 业务异常类
    ├── THROW_CLIENT_ERROR       - 便捷宏
    └── THROW_CLIENT_ERROR_MSG   - 便捷宏
```

---

## 依赖关系

```
exception/
├── mysql_exception.h
│   ├── <stdexcept>
│   └── <string>
│
└── client_exception.h
    ├── <stdexcept>
    ├── <string>
    └── common/error_codes.h   # ErrorCode 枚举

被依赖:
├── db/mysql_connection.cpp    # 抛出 MySQL 异常
├── db/mysql_result.cpp        # 抛出结果集异常
├── db/user_db.cpp             # 捕获并转换为 Result
├── auth/token_repository.cpp  # 捕获并转换为 Result
└── service/*.cpp              # 可能抛出 ClientException
```

