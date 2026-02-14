# Common 模块设计文档

## 目录结构

```
include/common/
├── logger.h              # 日志系统（spdlog 封装）
├── error_codes.h         # 统一错误码定义
├── result.h              # 统一结果类型（Result<T>）
├── validator.h           # 参数校验工具
├── password_helper.h     # 密码哈希工具
├── proto_converter.h     # Proto ↔ 业务类型转换
├── time_utils.h          # 时间处理工具
├── uuid.h                # UUID 生成工具
└── auth_type.h           # 认证相关类型定义

src/common/
└── logger.cpp            # 日志系统实现
```

---

## 1. 模块架构总览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Common 模块架构                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        核心基础设施层                                │   │
│  │                                                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │   Logger     │  │ ErrorCodes   │  │   Result     │               │   │
│  │  │              │  │              │  │              │               │   │
│  │  │ • 异步日志   │  │ • 分层错误码 │  │ • 统一返回   │               │   │
│  │  │ • 文件轮转   │  │ • 用户友好   │  │ • 错误传递   │               │   │
│  │  │ • 多级别     │  │ • 映射转换   │  │ • 类型安全   │               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  │         │                  │                  │                      │   │
│  │         └──────────────────┼──────────────────┘                      │   │
│  │                            │                                         │   │
│  │                            ▼                                         │   │
│  │  ┌─────────────────────────────────────────────────────────────┐    │   │
│  │  │                    被所有层依赖                              │    │   │
│  │  │                                                              │    │   │
│  │  │  Handler → Service → Repository → DB/Cache                  │    │   │
│  │  └─────────────────────────────────────────────────────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                          工具层                                      │   │
│  │                                                                      │   │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐        │   │
│  │  │ Validator  │ │ Password   │ │ TimeUtils  │ │    UUID    │        │   │
│  │  │            │ │ Helper     │ │            │ │            │        │   │
│  │  │ • 手机号   │ │ • SHA256   │ │ • 格式转换 │ │ • 用户ID   │        │   │
│  │  │ • 密码强度 │ │ • 加盐     │ │ • 时间戳   │ │ • Token    │        │   │
│  │  │ • 验证码   │ │ • 常量比较 │ │ • Timestamp│ │ • Session  │        │   │
│  │  └────────────┘ └────────────┘ └────────────┘ └────────────┘        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        类型定义层                                    │   │
│  │                                                                      │   │
│  │  ┌─────────────────────────┐  ┌─────────────────────────┐           │   │
│  │  │      auth_type.h        │  │   proto_converter.h     │           │   │
│  │  │                         │  │                         │           │   │
│  │  │ • TokenPair             │  │ • ErrorCode 转换        │           │   │
│  │  │ • AccessTokenPayload    │  │ • UserEntity → Proto    │           │   │
│  │  │ • AuthResult            │  │ • SmsScene 转换         │           │   │
│  │  │ • SmsScene              │  │ • Timestamp 转换        │           │   │
│  │  └─────────────────────────┘  └─────────────────────────┘           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Logger 日志系统

### 2.1 设计概述

基于 [spdlog](https://github.com/gabime/spdlog) 封装的异步日志系统，提供：

- **异步写入**：日志写入不阻塞业务线程
- **文件轮转**：按大小自动轮转，保留指定数量的备份
- **多输出**：同时输出到控制台和文件
- **多级别**：trace/debug/info/warn/error/critical

### 2.2 初始化

```cpp
#include "common/logger.h"

int main() {
    // 初始化日志系统（通常在 main 函数最开始调用）
    user_service::Logger::Init(
        "/app/logs",           // 日志目录
        "user-service.log",    // 日志文件名
        "info",                // 日志级别
        10 * 1024 * 1024,      // 单文件最大 10MB
        5,                     // 保留 5 个备份
        true                   // 同时输出到控制台
    );
    
    // ... 应用代码 ...
    
    // 程序退出前关闭日志（确保缓冲区写入）
    user_service::Logger::Shutdown();
    return 0;
}
```

### 2.3 使用示例

```cpp
#include "common/logger.h"

void SomeFunction() {
    // ==================== 基本用法 ====================
    
    LOG_TRACE("这是 trace 级别，最详细的调试信息");
    LOG_DEBUG("这是 debug 级别，调试信息");
    LOG_INFO("这是 info 级别，常规信息");
    LOG_WARN("这是 warn 级别，警告信息");
    LOG_ERROR("这是 error 级别，错误信息");
    
    // ==================== 带参数（fmt 格式化）====================
    
    std::string username = "alice";
    int user_id = 12345;
    
    LOG_INFO("User logged in: name={}, id={}", username, user_id);
    // 输出: [2024-01-15 10:30:00.123] [info] [12345] User logged in: name=alice, id=12345
    
    // ==================== 错误处理中使用 ====================
    
    try {
        // ... 可能抛异常的代码 ...
    } catch (const std::exception& e) {
        LOG_ERROR("Operation failed: {}", e.what());
    }
    
    // ==================== 性能敏感场景 ====================
    
    // 使用条件日志，避免在生产环境执行昂贵的字符串拼接
    if (spdlog::should_log(spdlog::level::debug)) {
        std::string expensive_data = ComputeExpensiveDebugInfo();
        LOG_DEBUG("Debug data: {}", expensive_data);
    }
}
```

### 2.4 日志级别说明

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        日志级别金字塔                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│                          ▲ 数量少                                       │
│                         ╱ ╲                                             │
│                        ╱   ╲                                            │
│                       ╱     ╲                                           │
│                      ╱ ERROR ╲   致命错误，需要立即处理                  │
│                     ╱─────────╲                                         │
│                    ╱   WARN    ╲  潜在问题，可能需要关注                 │
│                   ╱─────────────╲                                       │
│                  ╱     INFO      ╲ 关键业务节点（登录、注册等）          │
│                 ╱─────────────────╲                                     │
│                ╱      DEBUG        ╲ 开发调试信息                        │
│               ╱─────────────────────╲                                   │
│              ╱        TRACE          ╲ 最详细的跟踪信息                  │
│             ╱─────────────────────────╲                                 │
│                          ▼ 数量多                                       │
│                                                                         │
│  生产环境建议级别：INFO                                                  │
│  开发环境建议级别：DEBUG                                                 │
│  问题排查时：     TRACE                                                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.5 日志格式

```
[时间戳] [级别] [线程ID] [文件:行号] 消息内容

示例：
[2024-01-15 10:30:00.123] [info] [12345] [auth_service.cpp:42] User login success: mobile=138****1234
```

### 2.6 注意事项

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Logger 使用注意事项                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ✓ 正确做法                                                             │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  1. 初始化检查                                                          │
│     // Logger 宏内部会检查是否已初始化，未初始化时不会崩溃               │
│     LOG_INFO("...");  // 如果未初始化，静默忽略                          │
│                                                                         │
│  2. 敏感信息脱敏                                                        │
│     // ❌ 错误：记录完整手机号                                          │
│     LOG_INFO("User: {}", mobile);                                       │
│                                                                         │
│     // ✓ 正确：脱敏处理                                                 │
│     LOG_INFO("User: {}****{}", mobile.substr(0,3), mobile.substr(7));   │
│                                                                         │
│  3. 避免在日志中记录密码、Token 等敏感数据                              │
│     // ❌ 错误                                                          │
│     LOG_DEBUG("Password: {}", password);                                │
│     LOG_DEBUG("Token: {}", access_token);                               │
│                                                                         │
│     // ✓ 正确                                                           │
│     LOG_DEBUG("Password length: {}", password.length());                │
│     LOG_DEBUG("Token prefix: {}...", token.substr(0, 10));              │
│                                                                         │
│  4. 程序退出前调用 Shutdown                                             │
│     // 确保异步日志缓冲区全部写入磁盘                                    │
│     Logger::Shutdown();                                                 │
│                                                                         │
│  ✗ 错误做法                                                             │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  1. 在循环中大量打日志                                                  │
│     for (int i = 0; i < 1000000; i++) {                                │
│         LOG_DEBUG("Processing {}", i);  // ❌ 会严重影响性能             │
│     }                                                                   │
│                                                                         │
│  2. 在日志中拼接大对象                                                  │
│     LOG_DEBUG("Data: {}", huge_object.ToString());  // ❌ 每次都构造    │
│                                                                         │
│  3. 忘记关闭日志                                                        │
│     // 程序崩溃时，最后几条日志可能丢失                                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.7 配置文件中的日志配置

```yaml
# config.yaml
log:
  level: "info"                    # 日志级别
  path: "/app/logs"                # 日志目录
  filename: "user-service.log"     # 日志文件名
  max_size: 10485760               # 单文件最大 10MB
  max_files: 5                     # 保留 5 个文件
  console_output: true             # 输出到控制台
```

---

## 3. ErrorCode 错误码系统

### 3.1 设计原因

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      为什么需要统一错误码？                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  问题场景                                                               │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  ❌ 没有统一错误码时：                                                  │
│                                                                         │
│  // UserDB.cpp                                                          │
│  throw std::runtime_error("user not found");                           │
│                                                                         │
│  // AuthService.cpp                                                     │
│  return {false, "用户不存在"};                                          │
│                                                                         │
│  // Handler.cpp                                                         │
│  response.set_error("User does not exist");                            │
│                                                                         │
│  问题：                                                                 │
│  1. 错误信息不一致，难以维护                                            │
│  2. 客户端无法根据错误类型做差异化处理                                  │
│  3. 多语言支持困难                                                      │
│  4. 监控告警难以分类统计                                                │
│                                                                         │
│  ✓ 使用统一错误码后：                                                   │
│                                                                         │
│  // 所有层使用相同的错误码                                              │
│  return Result<User>::Fail(ErrorCode::UserNotFound);                   │
│                                                                         │
│  // 客户端可以这样处理                                                  │
│  switch (response.result().code()) {                                   │
│      case USER_NOT_FOUND:                                               │
│          ShowRegisterPrompt();  // 跳转注册                             │
│          break;                                                         │
│      case WRONG_PASSWORD:                                               │
│          ShowPasswordError();   // 显示密码错误                         │
│          break;                                                         │
│  }                                                                      │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 错误码分层设计

```cpp
enum class ErrorCode {
    // ========== 成功 (0) ==========
    Ok = 0,

    // ==================== 通用错误 (100-999) ====================
    // 系统错误 (1xx)
    Unknown             = 100,  // 未知错误
    Internal            = 101,  // 内部服务器异常
    NotImplemented      = 102,  // 功能未实现
    ServiceUnavailable  = 103,  // 服务不可用
    Timeout             = 104,  // 请求超时

    // 参数错误 (2xx)
    InvalidArgument     = 200,  // 参数无效
    InvalidPage         = 210,  // 无效的分页参数

    // 限流 (3xx)
    RateLimited         = 300,  // 请求过于频繁

    // ==================== 认证错误 (1000-1999) ====================
    // Token 相关 (100x)
    Unauthenticated     = 1000, // 未认证
    TokenMissing        = 1001, // Token 缺失
    TokenInvalid        = 1002, // Token 无效
    TokenExpired        = 1003, // Token 已过期
    TokenRevoked        = 1004, // Token 已注销

    // 登录相关 (101x)
    LoginFailed         = 1010, // 登录失败
    WrongPassword       = 1011, // 密码错误
    AccountLocked       = 1012, // 账号已锁定

    // 验证码 (102x)
    CaptchaWrong        = 1021, // 验证码错误
    CaptchaExpired      = 1022, // 验证码已过期

    // ==================== 用户错误 (2000-2999) ====================
    UserNotFound        = 2000, // 用户不存在
    UserAlreadyExists   = 2010, // 用户已存在
    MobileTaken         = 2013, // 手机号已被占用
    UserDisabled        = 2020, // 用户已禁用

    // ==================== 权限错误 (3000-3999) ====================
    PermissionDenied    = 3000, // 无权限
    AdminRequired       = 3001, // 需要管理员权限
};
```

### 3.3 设计原则

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        错误码设计原则                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. 分段设计                                                            │
│  ─────────────────────────────────────────────────────────────────────  │
│     0           : 成功                                                  │
│     100-999     : 通用错误（系统级、参数级）                            │
│     1000-1999   : 认证错误（登录、Token、验证码）                       │
│     2000-2999   : 用户错误（CRUD 相关）                                 │
│     3000-3999   : 权限错误                                              │
│                                                                         │
│     好处：                                                              │
│     • 一眼就能看出错误类别                                              │
│     • 便于扩展新的错误码                                                │
│     • 监控系统可以按段统计                                              │
│                                                                         │
│  2. 与 Proto 一致                                                       │
│  ─────────────────────────────────────────────────────────────────────  │
│     C++ ErrorCode 和 Proto ErrorCode 数值完全相同                       │
│                                                                         │
│     // 零成本转换                                                       │
│     pb_common::ErrorCode proto_code =                                   │
│         static_cast<pb_common::ErrorCode>(static_cast<int>(cpp_code));  │
│                                                                         │
│  3. 用户友好消息                                                        │
│  ─────────────────────────────────────────────────────────────────────  │
│     每个错误码都有对应的中文消息，通过 GetErrorMessage() 获取           │
│                                                                         │
│     GetErrorMessage(ErrorCode::UserNotFound)  // "用户不存在"           │
│     GetErrorMessage(ErrorCode::WrongPassword) // "用户名或密码错误"     │
│                                                                         │
│  4. 安全性考虑                                                          │
│  ─────────────────────────────────────────────────────────────────────  │
│     某些错误故意模糊化，防止信息泄露：                                   │
│                                                                         │
│     • WrongPassword: "用户名或密码错误"                                 │
│       （不区分用户不存在还是密码错误，防止枚举用户）                    │
│                                                                         │
│     • Internal: "服务器内部错误"                                        │
│       （不暴露具体的数据库/Redis 错误）                                 │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.4 映射转换

```cpp
// ErrorCode → gRPC StatusCode
grpc::StatusCode ToGrpcStatus(ErrorCode code);

// ErrorCode → HTTP StatusCode（API 网关使用）
int ToHttpStatus(ErrorCode code);

// 使用示例
ErrorCode code = ErrorCode::UserNotFound;
grpc::StatusCode grpc_code = ToGrpcStatus(code);  // NOT_FOUND
int http_code = ToHttpStatus(code);                // 404
```

---

## 4. Result<T> 统一结果类型

### 4.1 设计原因

```
┌─────────────────────────────────────────────────────────────────────────┐
│                   为什么需要 Result<T>？                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  传统方式的问题                                                         │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  方式 1: 异常                                                           │
│  ┌───────────────────────────────────────────────────────────────┐     │
│  │  User GetUser(int id) {                                        │     │
│  │      if (!exists) throw UserNotFoundException();               │     │
│  │      return user;                                              │     │
│  │  }                                                             │     │
│  │                                                                │     │
│  │  问题：                                                        │     │
│  │  • 异常性能开销大                                              │     │
│  │  • 调用者容易忘记 try-catch                                    │     │
│  │  • 不适合"用户不存在"这种预期内的错误                          │     │
│  └───────────────────────────────────────────────────────────────┘     │
│                                                                         │
│  方式 2: 返回指针/optional                                              │
│  ┌───────────────────────────────────────────────────────────────┐     │
│  │  std::optional<User> GetUser(int id) {                         │     │
│  │      if (!exists) return std::nullopt;                         │     │
│  │      return user;                                              │     │
│  │  }                                                             │     │
│  │                                                                │     │
│  │  问题：                                                        │     │
│  │  • 无法携带错误原因（是"不存在"还是"数据库挂了"？）            │     │
│  │  • 调用者无法做差异化处理                                      │     │
│  └───────────────────────────────────────────────────────────────┘     │
│                                                                         │
│  方式 3: 返回 tuple                                                     │
│  ┌───────────────────────────────────────────────────────────────┐     │
│  │  std::tuple<User, int, std::string> GetUser(int id);           │     │
│  │                                                                │     │
│  │  问题：                                                        │     │
│  │  • 返回值语义不清晰                                            │     │
│  │  • 每个函数的 tuple 结构可能不同                               │     │
│  └───────────────────────────────────────────────────────────────┘     │
│                                                                         │
│  Result<T> 解决方案                                                     │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────┐     │
│  │  Result<User> GetUser(int id) {                                │     │
│  │      if (db_error)                                             │     │
│  │          return Result<User>::Fail(ErrorCode::ServiceUnavailable);│  │
│  │      if (!exists)                                              │     │
│  │          return Result<User>::Fail(ErrorCode::UserNotFound);   │     │
│  │      return Result<User>::Ok(user);                            │     │
│  │  }                                                             │     │
│  │                                                                │     │
│  │  // 调用方                                                     │     │
│  │  auto result = GetUser(123);                                   │     │
│  │  if (!result.IsOk()) {                                         │     │
│  │      if (result.code == ErrorCode::UserNotFound) {             │     │
│  │          // 用户不存在的处理                                   │     │
│  │      } else {                                                  │     │
│  │          // 其他错误的处理                                     │     │
│  │      }                                                         │     │
│  │  }                                                             │     │
│  │  auto& user = result.Value();                                  │     │
│  └───────────────────────────────────────────────────────────────┘     │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Result<T> 定义

```cpp
template<typename T>
struct Result {
    ErrorCode code;                // 错误码
    std::string message;           // 错误消息
    std::optional<T> data;         // 返回数据（成功时有值）

    // ==================== 构造方法 ====================
    
    static Result Ok(const T& value);       // 成功
    static Result Ok(T&& value);            // 成功（移动）
    static Result Fail(ErrorCode c, const std::string& msg = "");  // 失败

    // ==================== 状态检查 ====================
    
    bool IsOk() const;              // 是否成功
    bool IsErr() const;             // 是否失败
    explicit operator bool() const; // 支持 if (result) 语法

    // ==================== 数据访问 ====================
    
    const T& Value() const&;        // 获取值（引用）
    T&& Value() &&;                 // 获取值（移动）
    T ValueOr(const T& default_value) const;  // 获取值或默认值
};

// void 特化版本（无返回值的操作）
template<>
struct Result<void> {
    ErrorCode code;
    std::string message;

    static Result Ok();
    static Result Fail(ErrorCode c, const std::string& msg = "");
    
    bool IsOk() const;
    bool IsErr() const;
};
```

### 4.3 使用示例

```cpp
// ==================== 基本用法 ====================

Result<UserEntity> UserDB::FindById(int64_t id) {
    try {
        auto conn = pool_->CreateConnection();
        auto res = conn->Query("SELECT * FROM users WHERE id = ?", {id});
        
        if (res.Next()) {
            return Result<UserEntity>::Ok(ParseRow(res));
        }
        
        // 未找到用户
        return Result<UserEntity>::Fail(ErrorCode::UserNotFound);
        
    } catch (const std::exception& e) {
        LOG_ERROR("FindById failed: {}", e.what());
        // 数据库错误返回模糊信息
        return Result<UserEntity>::Fail(
            ErrorCode::ServiceUnavailable,
            "服务暂时不可用"
        );
    }
}

// ==================== 调用方使用 ====================

void SomeService::DoSomething(int64_t user_id) {
    auto result = user_db_->FindById(user_id);
    
    // 方式 1: if 检查
    if (!result.IsOk()) {
        LOG_WARN("User not found: {}", user_id);
        return;
    }
    auto& user = result.Value();
    
    // 方式 2: bool 转换
    if (result) {
        // 成功
    }
    
    // 方式 3: 错误码分支
    if (result.code == ErrorCode::UserNotFound) {
        // 特定错误处理
    } else if (result.code == ErrorCode::ServiceUnavailable) {
        // 服务不可用处理
    }
}

// ==================== 链式调用 ====================

Result<AuthResult> AuthService::Login(const std::string& mobile, 
                                       const std::string& password) {
    // 查询用户
    auto user_result = user_db_->FindByMobile(mobile);
    if (!user_result.IsOk()) {
        // 直接转发错误（统一返回"密码错误"防止枚举攻击）
        return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "用户名或密码错误");
    }
    
    auto& user = user_result.Value();
    
    // 验证密码
    if (!PasswordHelper::Verify(password, user.password_hash)) {
        return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "用户名或密码错误");
    }
    
    // 生成 Token
    auto tokens = jwt_srv_->GenerateTokenPair(user);
    
    // 存储 Token
    auto store_result = StoreRefreshToken(user.id, tokens.refresh_token);
    if (!store_result.IsOk()) {
        LOG_WARN("Store token failed, but login continues");
        // Token 存储失败不影响登录
    }
    
    AuthResult auth_result{user, tokens};
    return Result<AuthResult>::Ok(std::move(auth_result));
}

// ==================== Result<void> 使用 ====================

Result<void> UserDB::Delete(int64_t id) {
    try {
        auto conn = pool_->CreateConnection();
        auto affected = conn->Execute("DELETE FROM users WHERE id = ?", {id});
        
        if (affected == 0) {
            return Result<void>::Fail(ErrorCode::UserNotFound);
        }
        
        return Result<void>::Ok();
        
    } catch (...) {
        return Result<void>::Fail(ErrorCode::ServiceUnavailable);
    }
}
```

### 4.4 与 Proto 的配合

```cpp
// Handler 中将 Result 转换为 Proto Response

::grpc::Status AuthHandler::LoginByPassword(
    ::grpc::ServerContext* context,
    const pb_auth::LoginByPasswordRequest* request,
    pb_auth::LoginByPasswordResponse* response) 
{
    // 调用 Service 层
    auto result = auth_service_->LoginByPassword(
        request->mobile(),
        request->password()
    );

    // 设置 Response 的 Result 字段
    if (result.IsOk()) {
        SetResultOk(response->mutable_result());
        
        const auto& auth_result = result.Value();
        ToProtoUserInfo(auth_result.user, response->mutable_user());
        ToProtoTokenPair(auth_result.tokens, response->mutable_tokens());
    } else {
        SetResultError(response->mutable_result(), result.code, result.message);
    }

    // gRPC Status 始终返回 OK（业务结果通过 Result 字段传递）
    return ::grpc::Status::OK;
}
```

---

## 5. 其他工具

### 5.1 Validator 参数校验

```cpp
#include "common/validator.h"

std::string error;

// 手机号校验
if (!IsValidMobile("13812345678", error)) {
    return Result<void>::Fail(ErrorCode::InvalidArgument, error);
}

// 密码校验（使用配置）
if (!IsValidPassword(password, error, config->password)) {
    return Result<void>::Fail(ErrorCode::InvalidArgument, error);
}

// 验证码校验
if (!IsValidVerifyCode(code, error, config->sms)) {
    return Result<void>::Fail(ErrorCode::InvalidArgument, error);
}
```

### 5.2 PasswordHelper 密码处理

```cpp
#include "common/password_helper.h"

// 生成密码哈希（注册时）
std::string hash = PasswordHelper::Hash("user_password");
// 结果: "$sha256$<salt>$<hash>"

// 验证密码（登录时）
bool valid = PasswordHelper::Verify("user_input", stored_hash);
```

### 5.3 UUID 生成

```cpp
#include "common/uuid.h"

// 用户 ID（带前缀）
std::string user_id = UUIDHelper::UserId();
// 结果: "usr_550e8400-e29b-41d4-a716-446655440000"

// Session ID
std::string session_id = UUIDHelper::SessionId();
// 结果: "sess_550e8400e29b41d4a716446655440000"

// 纯 UUID
std::string uuid = UUIDHelper::Generate();
// 结果: "550e8400-e29b-41d4-a716-446655440000"
```

### 5.4 TimeUtils 时间处理

```cpp
#include "common/time_utils.h"

// 当前时间字符串
std::string now = NowString();  // "2024-01-15 10:30:00"

// N 天后的时间
std::string future = FutureDaysString(7);

// 检查是否过期
bool expired = IsExpired("2024-01-01 00:00:00");

// 转换为 Protobuf Timestamp
google::protobuf::Timestamp ts;
StringToTimestamp("2024-01-15 10:30:00", &ts);
```

---

## 6. 依赖关系

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       Common 模块依赖关系                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  外部依赖                                                               │
│  ├── spdlog        → logger.h                                          │
│  ├── OpenSSL       → password_helper.h                                 │
│  ├── libuuid       → uuid.h                                            │
│  └── protobuf      → proto_converter.h, time_utils.h                   │
│                                                                         │
│  内部依赖                                                               │
│  ├── error_codes.h → result.h, validator.h                             │
│  ├── result.h      → (被其他所有模块使用)                               │
│  └── config.h      → validator.h (配置版校验)                          │
│                                                                         │
│  被依赖（使用 common 的模块）                                           │
│  ├── db/           → logger, result, error_codes                       │
│  ├── cache/        → logger, result, error_codes                       │
│  ├── auth/         → logger, result, password_helper, uuid             │
│  ├── service/      → logger, result, validator                         │
│  └── handlers/     → logger, proto_converter, validator                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 7. CMakeLists.txt

```cmake
# src/common/CMakeLists.txt

add_library(user_common STATIC logger.cpp)

target_include_directories(user_common PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(user_common PUBLIC
    spdlog::spdlog
)
```

---

## 8. 相关文件

| 文件 | 说明 |
|------|------|
| `include/common/logger.h` | 日志系统声明 |
| `src/common/logger.cpp` | 日志系统实现 |
| `include/common/error_codes.h` | 错误码定义 |
| `include/common/result.h` | 统一结果类型 |
| `include/common/validator.h` | 参数校验工具 |
| `include/common/password_helper.h` | 密码哈希工具 |
| `include/common/proto_converter.h` | Proto 转换器 |
| `include/common/time_utils.h` | 时间工具 |
| `include/common/uuid.h` | UUID 生成器 |
| `include/common/auth_type.h` | 认证类型定义 |

