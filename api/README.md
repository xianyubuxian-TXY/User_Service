# API/Proto 模块设计文档

## 目录结构

```
api/proto/
├── CMakeLists.txt           # Proto 编译配置
├── pb_common/
│   └── result.proto         # 通用结果定义（错误码、Result）
├── pb_auth/
│   └── auth.proto           # 认证服务定义（登录、注册、Token管理）
└── pb_user/
    └── user.proto           # 用户服务定义（用户管理、CRUD操作）
```

---

## 1. result.proto 设计

### 1.1 文件概述

`result.proto` 定义了整个系统的**统一错误码**和**通用结果结构**，是所有 gRPC 响应的基础组件。

```protobuf
// pb_common/result.proto
message Result {
    ErrorCode code = 1;    // 错误码
    string    msg  = 2;    // 错误描述
}
```

### 1.2 ErrorCode 设计

#### 1.2.1 错误码分层设计

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          错误码分层架构                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   0           成功                                                      │
│   ├──────────────────────────────────────────────────────────────────   │
│   100-999     通用错误                                                  │
│   │   ├── 1xx  系统错误（Unknown, Internal, ServiceUnavailable...）    │
│   │   ├── 2xx  参数错误（InvalidArgument, InvalidPage...）             │
│   │   └── 3xx  限流错误（RateLimited, QuotaExceeded）                  │
│   ├──────────────────────────────────────────────────────────────────   │
│   1000-1999   认证错误                                                  │
│   │   ├── 100x Token错误（Missing, Invalid, Expired, Revoked）         │
│   │   ├── 101x 登录错误（LoginFailed, WrongPassword, AccountLocked）   │
│   │   └── 102x 验证码错误（CaptchaWrong, CaptchaExpired）              │
│   ├──────────────────────────────────────────────────────────────────   │
│   2000-2999   用户错误                                                  │
│   │   ├── 200x 查询错误（UserNotFound, UserDeleted）                   │
│   │   ├── 201x 创建错误（UserAlreadyExists, MobileTaken）              │
│   │   └── 202x 状态错误（UserDisabled, UserNotVerified）               │
│   ├──────────────────────────────────────────────────────────────────   │
│   3000-3999   权限错误                                                  │
│       ├── PermissionDenied                                              │
│       ├── AdminRequired                                                 │
│       └── OwnerRequired                                                 │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 1.2.2 与 C++ ErrorCode 保持一致的设计原因

Proto 中的 `ErrorCode` 与 C++ 中的 `ErrorCode` 枚举**数值完全一致**：

```cpp
// C++ (include/common/error_codes.h)
enum class ErrorCode {
    Ok = 0,
    Unknown = 100,
    Internal = 101,
    // ...
    UserNotFound = 2000,
    // ...
};
```

```protobuf
// Proto (pb_common/result.proto)
enum ErrorCode {
    OK = 0;
    UNKNOWN = 100;
    INTERNAL = 101;
    // ...
    USER_NOT_FOUND = 2000;
    // ...
}
```

**设计原因：**

| 原因 | 说明 |
|------|------|
| **零成本转换** | `static_cast` 即可完成转换，无需查表或映射逻辑 |
| **减少出错概率** | 新增错误码只需在一处定义，复制到另一处即可 |
| **便于调试** | 日志中的数字错误码可直接对应含义，无需翻译 |
| **跨语言一致性** | 其他语言客户端（Go/Java/Python）也能使用相同的错误码 |

**转换示例：**

```cpp
// C++ ErrorCode → Proto ErrorCode
inline pb_common::ErrorCode ToProtoErrorCode(ErrorCode code) {
    return static_cast<pb_common::ErrorCode>(static_cast<int>(code));
}

// Proto ErrorCode → C++ ErrorCode
inline ErrorCode FromProtoErrorCode(pb_common::ErrorCode code) {
    return static_cast<ErrorCode>(static_cast<int>(code));
}
```

### 1.3 为什么使用自定义 Result 而不是 gRPC Status

#### 1.3.1 gRPC Status vs 自定义 Result 的职责划分

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      错误处理的双层架构                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                    gRPC Status (传输层)                         │   │
│   │                                                                 │   │
│   │  职责：表示 RPC 调用本身是否成功                                │   │
│   │                                                                 │   │
│   │  ┌─────────────────┬──────────────────────────────────────┐    │   │
│   │  │ Status::OK      │ RPC 调用成功完成（不代表业务成功）    │    │   │
│   │  │ UNAVAILABLE     │ 服务不可用、网络中断                  │    │   │
│   │  │ DEADLINE_EXCEEDED│ 请求超时                            │    │   │
│   │  │ CANCELLED       │ 客户端取消请求                        │    │   │
│   │  │ INTERNAL        │ 服务端代码 panic/崩溃                 │    │   │
│   │  └─────────────────┴──────────────────────────────────────┘    │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                    ↓                                    │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                    Result (业务层)                              │   │
│   │                                                                 │   │
│   │  职责：表示业务逻辑处理结果                                     │   │
│   │                                                                 │   │
│   │  ┌─────────────────┬──────────────────────────────────────┐    │   │
│   │  │ OK              │ 业务处理成功                          │    │   │
│   │  │ USER_NOT_FOUND  │ 用户不存在                            │    │   │
│   │  │ WRONG_PASSWORD  │ 密码错误                              │    │   │
│   │  │ MOBILE_TAKEN    │ 手机号已被注册                        │    │   │
│   │  │ TOKEN_EXPIRED   │ 登录已过期                            │    │   │
│   │  └─────────────────┴──────────────────────────────────────┘    │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 1.3.2 为什么不直接使用 gRPC Status 返回业务错误？

| 问题 | 说明 |
|------|------|
| **错误码不够细** | gRPC 只有 16 种状态码，无法区分"用户不存在"和"手机号已占用" |
| **语义混淆** | `NOT_FOUND` 是"资源不存在"还是"服务未找到"？ |
| **无法携带额外信息** | Status 的 details 字段使用 Any 类型，解析复杂 |
| **客户端处理困难** | 需要从 Status 中解析错误类型，逻辑分散 |

#### 1.3.3 gRPC Status 使用原则

**返回 `grpc::Status::OK` 的场景（99% 的情况）：**

```cpp
::grpc::Status AuthHandler::LoginByPassword(...) {
    // 无论业务成功还是失败，都返回 OK
    // 业务结果通过 response->result() 告知客户端
    
    auto result = auth_service_->LoginByPassword(mobile, password);
    
    if (result.IsOk()) {
        SetResultOk(response->mutable_result());
        // 填充用户信息和 Token
    } else {
        SetResultError(response->mutable_result(), result.code, result.message);
    }
    
    return ::grpc::Status::OK;  // 始终返回 OK
}
```

**返回非 OK Status 的场景（极少数情况）：**

```cpp
// 场景1：请求格式完全无法解析（通常由 gRPC 框架自动处理）
// 场景2：服务端 panic（不推荐主动返回）
// 场景3：需要触发 gRPC 重试机制时

// 示例：流式 RPC 中的异常处理
::grpc::Status StreamHandler::StreamData(...) {
    try {
        // 处理流
    } catch (const std::exception& e) {
        // 只有流式 RPC 中无法通过 response 返回时才用这种方式
        return ::grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}
```

### 1.4 Result 返回原则

#### 1.4.1 错误信息返回策略

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        错误信息返回策略                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  前端可见错误（用户友好型）                                      │   │
│  │                                                                  │   │
│  │  • 参数错误：明确告知哪个参数有问题                              │   │
│  │    - "手机号格式错误"                                            │   │
│  │    - "密码长度至少8位"                                           │   │
│  │    - "验证码已过期，请重新获取"                                  │   │
│  │                                                                  │   │
│  │  • 业务错误：告知结果但不暴露细节                                │   │
│  │    - "用户名或密码错误" (不区分用户不存在/密码错误)              │   │
│  │    - "该手机号已被注册"                                          │   │
│  │    - "操作过于频繁，请稍后再试"                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  内部错误（模糊化处理）                                          │   │
│  │                                                                  │   │
│  │  • 数据库异常 → "服务暂时不可用，请稍后重试"                     │   │
│  │  • Redis 异常 → "服务暂时不可用，请稍后重试"                     │   │
│  │  • 未知异常   → "服务器内部错误"                                 │   │
│  │                                                                  │   │
│  │  原因：                                                          │   │
│  │  1. 防止暴露系统架构信息                                         │   │
│  │  2. 防止被利用进行攻击                                           │   │
│  │  3. 详细错误记录在服务端日志中，便于排查                         │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 1.4.2 安全性考虑

```cpp
// ❌ 错误示例：暴露敏感信息
Result<UserEntity>::Fail(ErrorCode::UserNotFound, 
    "用户 ID 12345 在数据库 user_service.users 表中不存在");

// ✓ 正确示例：用户友好且安全
Result<UserEntity>::Fail(ErrorCode::UserNotFound, "用户不存在");

// ❌ 错误示例：区分用户不存在和密码错误（可被枚举攻击）
if (!user_exists) return Fail(UserNotFound);
if (!password_match) return Fail(WrongPassword);

// ✓ 正确示例：统一返回
return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "用户名或密码错误");
```

#### 1.4.3 Response 结构统一规范

所有 Response 的**第一个字段**必须是 `Result`：

```protobuf
message LoginByPasswordResponse {
    pb_common.Result result = 1;  // 必须是第一个字段
    UserInfo user = 2;
    TokenPair tokens = 3;
}
```

**客户端处理流程：**

```cpp
// 伪代码
auto response = stub->LoginByPassword(request);

// 第一步：检查业务结果
if (response.result().code() != pb_common::OK) {
    ShowError(response.result().msg());
    return;
}

// 第二步：使用业务数据
auto user = response.user();
auto tokens = response.tokens();
```

---

## 2. auth.proto 设计

### 2.1 服务概述

`AuthService` 提供完整的用户认证功能，包括：

- 验证码发送与校验
- 用户注册
- 多种登录方式（密码/验证码）
- Token 管理（刷新/注销）
- 密码重置
- Token 验证（内部服务调用）

### 2.2 枚举定义

#### 2.2.1 验证码场景 (SmsScene)

```protobuf
enum SmsScene {
    SMS_SCENE_UNKNOWN = 0;        // 未知场景（禁止使用）
    SMS_SCENE_REGISTER = 1;       // 注册
    SMS_SCENE_LOGIN = 2;          // 登录
    SMS_SCENE_RESET_PASSWORD = 3; // 重置密码
    SMS_SCENE_DELETE_USER = 4;    // 注销账号
}
```

**设计说明：**

| 场景 | 业务校验 | Redis Key 隔离 |
|------|----------|----------------|
| REGISTER | 手机号必须**未注册** | `sms:code:register:{mobile}` |
| LOGIN | 手机号必须**已注册** | `sms:code:login:{mobile}` |
| RESET_PASSWORD | 手机号必须**已注册** | `sms:code:reset_password:{mobile}` |
| DELETE_USER | 手机号必须**已注册** | `sms:code:delete_user:{mobile}` |

#### 2.2.2 用户角色 (UserRole)

```protobuf
enum UserRole {
    USER_ROLE_USER = 0;         // 普通用户（默认）
    USER_ROLE_ADMIN = 1;        // 管理员
    USER_ROLE_SUPER_ADMIN = 2;  // 超级管理员
}
```

### 2.3 服务定义

```protobuf
service AuthService {
    // ==================== 验证码 ====================
    rpc SendVerifyCode(SendVerifyCodeRequest) returns (SendVerifyCodeResponse);
    
    // ==================== 注册 ====================
    rpc Register(RegisterRequest) returns (RegisterResponse);
    
    // ==================== 登录 ====================
    rpc LoginByPassword(LoginByPasswordRequest) returns (LoginByPasswordResponse);
    rpc LoginByCode(LoginByCodeRequest) returns (LoginByCodeResponse);
    
    // ==================== Token 管理 ====================
    rpc RefreshToken(RefreshTokenRequest) returns (RefreshTokenResponse);
    rpc Logout(LogoutRequest) returns (LogoutResponse);
    
    // ==================== 密码管理 ====================
    rpc ResetPassword(ResetPasswordRequest) returns (ResetPasswordResponse);
    
    // ==================== 内部服务调用 ====================
    rpc ValidateToken(ValidateTokenRequest) returns (ValidateTokenResponse);
}
```

### 2.4 方法详解

#### 2.4.1 SendVerifyCode - 发送验证码

```protobuf
message SendVerifyCodeRequest {
    string mobile = 1;      // 手机号
    SmsScene scene = 2;     // 使用场景
}

message SendVerifyCodeResponse {
    pb_common.Result result = 1;
    int32 retry_after = 2;  // 可重发时间（秒）
}
```

**处理流程：**

```
┌─────────────────────────────────────────────────────────────────────┐
│                     SendVerifyCode 流程                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Client Request                                                     │
│       │                                                             │
│       ▼                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 1. 参数校验                                                  │   │
│  │    • 手机号格式：1[3-9]\d{9}                                 │   │
│  │    • 场景不能为 UNKNOWN                                      │   │
│  └─────────────────────────────────────────────────────────────┘   │
│       │                                                             │
│       ▼                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 2. 业务校验                                                  │   │
│  │    • REGISTER: 手机号必须未注册                              │   │
│  │    • LOGIN/RESET/DELETE: 手机号必须已注册                    │   │
│  └─────────────────────────────────────────────────────────────┘   │
│       │                                                             │
│       ▼                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 3. 限流检查（Redis）                                         │   │
│  │    • 是否被锁定：sms:lock:{scene}:{mobile}                   │   │
│  │    • 是否在发送间隔内：sms:interval:{mobile}                 │   │
│  └─────────────────────────────────────────────────────────────┘   │
│       │                                                             │
│       ▼                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 4. 生成并存储验证码                                          │   │
│  │    • 生成 6 位随机数字                                       │   │
│  │    • 存储：sms:code:{scene}:{mobile} = code (TTL: 5min)     │   │
│  │    • 设置发送间隔：sms:interval:{mobile} = 1 (TTL: 60s)     │   │
│  └─────────────────────────────────────────────────────────────┘   │
│       │                                                             │
│       ▼                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ 5. 发送短信（对接短信服务商）                                │   │
│  └─────────────────────────────────────────────────────────────┘   │
│       │                                                             │
│       ▼                                                             │
│  Response: { result: OK, retry_after: 60 }                         │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

#### 2.4.2 Register - 用户注册

```protobuf
message RegisterRequest {
    string mobile = 1;       // 手机号
    string verify_code = 2;  // 验证码
    string password = 3;     // 密码
    string display_name = 4; // 昵称（可选）
}

message RegisterResponse {
    pb_common.Result result = 1;
    UserInfo user = 2;       // 用户信息
    TokenPair tokens = 3;    // 双令牌
}
```

**注册成功后自动登录**，返回 Token 对，无需再调用登录接口。

#### 2.4.3 LoginByPassword / LoginByCode - 登录

```protobuf
// 密码登录
message LoginByPasswordRequest {
    string mobile = 1;
    string password = 2;
}

// 验证码登录
message LoginByCodeRequest {
    string mobile = 1;
    string verify_code = 2;
}

// 响应结构相同
message LoginByXxxResponse {
    pb_common.Result result = 1;
    UserInfo user = 2;
    TokenPair tokens = 3;
}
```

**安全机制：**

- 密码登录失败 5 次后锁定 30 分钟
- 验证码登录失败 5 次后锁定 30 分钟
- 登录成功后清除失败计数

### 2.5 双令牌机制

#### 2.5.1 机制概述

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           双令牌机制                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Access Token                                                     │  │
│  │                                                                   │  │
│  │  用途：业务接口鉴权                                               │  │
│  │  有效期：15 分钟（短）                                            │  │
│  │  存储：客户端内存                                                 │  │
│  │  传递：Authorization: Bearer <access_token>                       │  │
│  │                                                                   │  │
│  │  Payload 内容：                                                   │  │
│  │  {                                                                │  │
│  │    "iss": "user-service",     // 签发者                          │  │
│  │    "sub": "access",           // 令牌类型                         │  │
│  │    "id": "12345",             // 数据库用户ID                     │  │
│  │    "uid": "uuid-xxx",         // 用户UUID（对外）                 │  │
│  │    "mobile": "138xxxx1234",   // 手机号                          │  │
│  │    "role": "0",               // 用户角色                         │  │
│  │    "exp": 1704074400,         // 过期时间                         │  │
│  │    "jti": "582943"            // 唯一标识                         │  │
│  │  }                                                                │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Refresh Token                                                    │  │
│  │                                                                   │  │
│  │  用途：刷新 Access Token                                          │  │
│  │  有效期：7 天（长）                                               │  │
│  │  存储：客户端安全存储 + 服务端数据库（哈希存储）                   │  │
│  │  传递：仅在 RefreshToken 接口中使用                               │  │
│  │                                                                   │  │
│  │  Payload 内容（最小化）：                                         │  │
│  │  {                                                                │  │
│  │    "iss": "user-service",                                        │  │
│  │    "sub": "refresh",          // 令牌类型                         │  │
│  │    "uid": "12345",            // 用户ID（仅此一个业务字段）       │  │
│  │    "exp": 1704672000                                             │  │
│  │  }                                                                │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  设计优势：                                                             │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 1. Access Token 有效期短 → 即使泄露，影响有限                    │   │
│  │ 2. Refresh Token 存储在服务端 → 可主动失效（如修改密码后）       │   │
│  │ 3. Refresh Token Payload 最小化 → 即使泄露，信息暴露少           │   │
│  │ 4. 无状态验证 → Access Token 无需查数据库，高性能                 │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 2.5.2 TokenPair 消息结构

```protobuf
message TokenPair {
    string access_token = 1;   // 访问令牌
    string refresh_token = 2;  // 刷新令牌
    int64 expires_in = 3;      // access_token 有效期（秒）
}
```

#### 2.5.3 返回 TokenPair 的接口

| 接口 | 说明 |
|------|------|
| `Register` | 注册成功后自动登录，返回 Token |
| `LoginByPassword` | 密码登录成功，返回 Token |
| `LoginByCode` | 验证码登录成功，返回 Token |
| `RefreshToken` | 刷新 Token，返回新的 Token 对 |

#### 2.5.4 Token 使用流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Token 使用生命周期                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  用户登录                                                               │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  1. 获取 TokenPair                                               │   │
│  │     access_token  = "eyJhbGciOi..."                              │   │
│  │     refresh_token = "eyJhbGciOi..."                              │   │
│  │     expires_in    = 900 (15分钟)                                 │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  2. 存储 Token                                                   │   │
│  │     • access_token  → 内存（或 sessionStorage）                  │   │
│  │     • refresh_token → 安全存储（如 HttpOnly Cookie）             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  3. 调用业务接口（携带 access_token）                            │   │
│  │                                                                  │   │
│  │     metadata: { "authorization": "Bearer eyJhbGciOi..." }       │   │
│  │                                                                  │   │
│  │     ┌──────────────────────────────────────────────────────┐    │   │
│  │     │  循环调用业务接口                                     │    │   │
│  │     │                                                       │    │   │
│  │     │  GetCurrentUser()   →  成功                           │    │   │
│  │     │  UpdateUser()       →  成功                           │    │   │
│  │     │  ...                                                  │    │   │
│  │     │  GetCurrentUser()   →  TOKEN_EXPIRED (15分钟后)       │    │   │
│  │     └──────────────────────────────────────────────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      ▼  TOKEN_EXPIRED                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  4. 刷新 Token                                                   │   │
│  │                                                                  │   │
│  │     RefreshToken(refresh_token) → 新的 TokenPair                │   │
│  │                                                                  │   │
│  │     • 成功：更新存储的 Token，继续使用                          │   │
│  │     • 失败（TOKEN_REVOKED）：跳转登录页                         │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│      │                                                                  │
│      ▼  重复步骤 3-4，直到 refresh_token 过期（7天）                    │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  5. 用户登出 或 refresh_token 过期                               │   │
│  │                                                                  │   │
│  │     Logout(refresh_token)  或  TOKEN_REVOKED                    │   │
│  │                                                                  │   │
│  │     → 清除本地存储                                               │   │
│  │     → 跳转登录页                                                 │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.6 ValidateToken - 内部服务调用

```protobuf
message ValidateTokenRequest {
    string access_token = 1;
}

message ValidateTokenResponse {
    pb_common.Result result = 1;
    string user_id = 2;      // 数据库ID
    string user_uuid = 3;    // UUID
    string mobile = 4;       // 手机号
    UserRole role = 5;       // 用户角色
    google.protobuf.Timestamp expires_at = 6;
}
```

**使用场景：**

- API 网关验证 Token
- 其他微服务验证调用者身份
- **注意：不应暴露给客户端直接调用**

### 2.7 客户端使用流程示例

```cpp
// C++ 客户端伪代码

class AuthClient {
public:
    // 1. 登录
    bool Login(const std::string& mobile, const std::string& password) {
        LoginByPasswordRequest request;
        request.set_mobile(mobile);
        request.set_password(password);
        
        LoginByPasswordResponse response;
        grpc::Status status = stub_->LoginByPassword(&context, request, &response);
        
        if (!status.ok()) {
            // gRPC 层面错误（网络问题等）
            return false;
        }
        
        if (response.result().code() != pb_common::OK) {
            // 业务层面错误
            ShowError(response.result().msg());
            return false;
        }
        
        // 登录成功，保存 Token
        access_token_ = response.tokens().access_token();
        refresh_token_ = response.tokens().refresh_token();
        expires_at_ = Now() + response.tokens().expires_in();
        
        return true;
    }
    
    // 2. 调用业务接口（带 Token）
    std::optional<User> GetCurrentUser() {
        // 检查 Token 是否即将过期，提前刷新
        if (IsTokenExpiringSoon()) {
            RefreshTokenIfNeeded();
        }
        
        grpc::ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token_);
        
        GetCurrentUserRequest request;
        GetCurrentUserResponse response;
        
        auto status = user_stub_->GetCurrentUser(&context, request, &response);
        
        if (response.result().code() == pb_common::TOKEN_EXPIRED) {
            // Token 已过期，尝试刷新
            if (RefreshTokenIfNeeded()) {
                return GetCurrentUser();  // 重试
            }
            return std::nullopt;  // 刷新失败，需要重新登录
        }
        
        return response.user();
    }
    
    // 3. 刷新 Token
    bool RefreshTokenIfNeeded() {
        RefreshTokenRequest request;
        request.set_refresh_token(refresh_token_);
        
        RefreshTokenResponse response;
        auto status = auth_stub_->RefreshToken(&context, request, &response);
        
        if (response.result().code() != pb_common::OK) {
            // 刷新失败，需要重新登录
            return false;
        }
        
        // 更新 Token
        access_token_ = response.tokens().access_token();
        refresh_token_ = response.tokens().refresh_token();
        expires_at_ = Now() + response.tokens().expires_in();
        
        return true;
    }
    
    // 4. 登出
    void Logout() {
        LogoutRequest request;
        request.set_refresh_token(refresh_token_);
        
        LogoutResponse response;
        auth_stub_->Logout(&context, request, &response);
        
        // 无论成功失败，清除本地 Token
        access_token_.clear();
        refresh_token_.clear();
    }
    
private:
    std::string access_token_;
    std::string refresh_token_;
    time_t expires_at_;
};
```

---

## 3. user.proto 设计

### 3.1 User 消息字段解析

```protobuf
message User {
    string id = 1;                              // UUID（对外暴露）
    string mobile = 2;                          // 手机号
    pb_auth.UserRole role = 3;                  // 用户角色
    string display_name = 4;                    // 昵称
    bool disabled = 5;                          // 是否禁用
    google.protobuf.Timestamp created_at = 6;  // 创建时间
    google.protobuf.Timestamp updated_at = 7;  // 更新时间
}
```

#### 3.1.1 字段详解

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | **UUID**，对外暴露的用户标识。数据库内部使用自增 ID，但对外统一用 UUID，避免暴露用户数量和注册顺序 |
| `mobile` | string | 手机号，登录凭证之一 |
| `role` | UserRole | 用户角色枚举：`USER(0)` / `ADMIN(1)` / `SUPER_ADMIN(2)` |
| `display_name` | string | 用户昵称，可选字段 |
| `disabled` | bool | 账号状态，`true` 表示被禁用，无法登录 |
| `created_at` | Timestamp | 注册时间 |
| `updated_at` | Timestamp | 最后更新时间 |

#### 3.1.2 为什么用 UUID 而不是数字 ID

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      ID 设计：数据库 ID vs UUID                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  数据库存储                          API 暴露                            │
│  ┌─────────────────────┐            ┌─────────────────────────────┐    │
│  │ id (BIGINT)         │            │ id (UUID string)            │    │
│  │ = 12345             │  ──────▶   │ = "550e8400-e29b-41d4..."   │    │
│  │                     │            │                             │    │
│  │ 用途：              │            │ 用途：                       │    │
│  │ • 主键索引          │            │ • 对外标识                   │    │
│  │ • 外键关联          │            │ • URL 参数                   │    │
│  │ • 内部查询          │            │ • 日志追踪                   │    │
│  └─────────────────────┘            └─────────────────────────────┘    │
│                                                                         │
│  为什么不直接暴露数字 ID？                                               │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 1. 信息泄露：id=12345 → 系统约有 1.2 万用户                      │   │
│  │ 2. 枚举攻击：攻击者可遍历 id=1,2,3... 获取所有用户信息           │   │
│  │ 3. 竞品分析：通过注册获取 ID 差值，可分析竞品增长数据            │   │
│  │ 4. 分库分表：UUID 天然支持分布式生成，无需协调                   │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 服务定义

```protobuf
service UserService {
    // ==================== 当前用户操作 ====================
    rpc GetCurrentUser(GetCurrentUserRequest) returns (GetCurrentUserResponse);
    rpc UpdateUser(UpdateUserRequest) returns (UpdateUserResponse);
    rpc ChangePassword(ChangePasswordRequest) returns (ChangePasswordResponse);
    rpc DeleteUser(DeleteUserRequest) returns (DeleteUserResponse);
    
    // ==================== 管理员接口 ====================
    rpc GetUser(GetUserRequest) returns (GetUserResponse);
    rpc ListUsers(ListUsersRequest) returns (ListUsersResponse);
}
```

### 3.3 方法详解

#### 3.3.1 GetCurrentUser - 获取当前用户信息

```protobuf
message GetCurrentUserRequest {
    // 无参数，从 Token 中获取 user_id
}

message GetCurrentUserResponse {
    pb_common.Result result = 1;
    User user = 2;
}
```

**特点：**

- 请求体为空，用户 ID 从 Token 中解析
- 返回当前登录用户的完整信息

#### 3.3.2 UpdateUser - 更新用户信息

```protobuf
message UpdateUserRequest {
    google.protobuf.StringValue display_name = 1;  // 可选更新
    // 后续扩展：avatar_url, email 等
}

message UpdateUserResponse {
    pb_common.Result result = 1;
    User user = 2;  // 更新后的用户信息
}
```

**使用 Wrapper 类型的原因：**

```
┌─────────────────────────────────────────────────────────────────────────┐
│            为什么 display_name 用 StringValue 而不是 string？            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  场景：用户只想更新昵称，不想更新其他字段                                │
│                                                                         │
│  如果用 string：                                                        │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  message UpdateUserRequest {                                     │   │
│  │      string display_name = 1;  // 空字符串和"未设置"无法区分     │   │
│  │  }                                                               │   │
│  │                                                                  │   │
│  │  • display_name = ""    → 是想清空昵称？还是不想更新？           │   │
│  │  • 无法表达"不更新这个字段"的语义                                │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  使用 StringValue（Wrapper）：                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  message UpdateUserRequest {                                     │   │
│  │      google.protobuf.StringValue display_name = 1;               │   │
│  │  }                                                               │   │
│  │                                                                  │   │
│  │  • has_display_name() == false → 不更新此字段                    │   │
│  │  • has_display_name() == true && value == "" → 清空昵称          │   │
│  │  • has_display_name() == true && value == "Tom" → 更新为 Tom     │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 3.3.3 ChangePassword - 修改密码

```protobuf
message ChangePasswordRequest {
    string old_password = 1;  // 旧密码（验证身份）
    string new_password = 2;  // 新密码
}

message ChangePasswordResponse {
    pb_common.Result result = 1;
}
```

**与 ResetPassword 的区别：**

| 接口 | 场景 | 验证方式 | 所属服务 |
|------|------|----------|----------|
| ChangePassword | 已登录，主动修改 | 旧密码 | UserService |
| ResetPassword | 忘记密码 | 短信验证码 | AuthService |

#### 3.3.4 DeleteUser - 注销账号

```protobuf
message DeleteUserRequest {
    string verify_code = 1;  // 需要验证码确认
}

message DeleteUserResponse {
    pb_common.Result result = 1;
}
```

**安全设计：**

- 需要先调用 `SendVerifyCode(scene=DELETE_USER)` 获取验证码
- 验证码确认后才能执行删除
- 实际实现为**软删除**（设置 `disabled=true`，修改 `mobile`）

#### 3.3.5 管理员接口

```protobuf
// 获取指定用户
message GetUserRequest {
    string id = 1;  // 用户 UUID
}

// 用户列表（支持分页和筛选）
message ListUsersRequest {
    PageRequest page = 1;                      // 分页参数
    string mobile_filter = 2;                  // 手机号模糊搜索
    google.protobuf.BoolValue disabled_filter = 3;  // 禁用状态筛选
}

message ListUsersResponse {
    pb_common.Result result = 1;
    repeated User users = 2;
    PageResponse page_info = 3;
}
```

**权限控制：**

- 管理员接口在 Handler 层检查 `role`
- 只有 `ADMIN` 或 `SUPER_ADMIN` 才能调用

### 3.4 Result 作为第一字段的作用

所有 Response 的第一个字段都是 `pb_common.Result result = 1;`：

```protobuf
message GetCurrentUserResponse {
    pb_common.Result result = 1;  // 必须是第一个字段
    User user = 2;
}
```

**设计原因：**

1. **统一处理逻辑**：客户端可以先检查 `result.code()`，再处理业务数据
2. **错误优先**：即使反序列化不完整，`result` 字段也能被正确解析
3. **代码生成友好**：各语言 SDK 可以统一生成 `checkResult()` 方法

### 3.5 客户端使用流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     UserService 客户端使用流程                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  前置条件：已通过 AuthService 登录，持有有效的 access_token             │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  步骤 1：设置 Authorization Header                               │   │
│  │                                                                  │   │
│  │  grpc::ClientContext context;                                    │   │
│  │  context.AddMetadata("authorization", "Bearer " + access_token); │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                          │                                              │
│                          ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  步骤 2：调用接口                                                │   │
│  │                                                                  │   │
│  │  GetCurrentUserRequest request;                                  │   │
│  │  GetCurrentUserResponse response;                                │   │
│  │  auto status = stub->GetCurrentUser(&context, request, &response);│  │
│  └─────────────────────────────────────────────────────────────────┘   │
│                          │                                              │
│                          ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  步骤 3：检查 gRPC Status                                        │   │
│  │                                                                  │   │
│  │  if (!status.ok()) {                                             │   │
│  │      // 网络错误、服务不可用等                                   │   │
│  │      HandleGrpcError(status);                                    │   │
│  │      return;                                                     │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                          │                                              │
│                          ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  步骤 4：检查业务 Result                                         │   │
│  │                                                                  │   │
│  │  switch (response.result().code()) {                             │   │
│  │      case pb_common::OK:                                         │   │
│  │          // 成功，处理 response.user()                           │   │
│  │          break;                                                  │   │
│  │                                                                  │   │
│  │      case pb_common::TOKEN_EXPIRED:                              │   │
│  │      case pb_common::TOKEN_INVALID:                              │   │
│  │          // Token 问题，尝试刷新                                 │   │
│  │          if (RefreshToken()) {                                   │   │
│  │              // 重试请求                                         │   │
│  │              return GetCurrentUser();                            │   │
│  │          } else {                                                │   │
│  │              // 刷新失败，跳转登录                                │   │
│  │              NavigateToLogin();                                  │   │
│  │          }                                                       │   │
│  │          break;                                                  │   │
│  │                                                                  │   │
│  │      case pb_common::USER_DISABLED:                              │   │
│  │          // 账号被禁用                                           │   │
│  │          ShowMessage("您的账号已被禁用");                        │   │
│  │          break;                                                  │   │
│  │                                                                  │   │
│  │      default:                                                    │   │
│  │          // 其他错误                                             │   │
│  │          ShowError(response.result().msg());                     │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.6 Token 过期处理策略

```cpp
// 推荐的客户端实现模式

class UserClient {
public:
    template<typename Request, typename Response, typename RpcFunc>
    Response CallWithRetry(const Request& request, RpcFunc rpc_func) {
        // 第一次尝试
        auto response = DoCall(request, rpc_func);
        
        // 检查是否 Token 过期
        if (response.result().code() == pb_common::TOKEN_EXPIRED ||
            response.result().code() == pb_common::TOKEN_INVALID) {
            
            // 尝试刷新 Token
            if (auth_client_.RefreshToken()) {
                // 刷新成功，重试一次
                response = DoCall(request, rpc_func);
            }
        }
        
        return response;
    }
    
private:
    template<typename Request, typename Response, typename RpcFunc>
    Response DoCall(const Request& request, RpcFunc rpc_func) {
        grpc::ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token_);
        
        Response response;
        rpc_func(&context, request, &response);
        return response;
    }
    
    AuthClient auth_client_;
    std::string access_token_;
};

// 使用示例
UserClient client;
auto response = client.CallWithRetry(
    GetCurrentUserRequest{},
    [&](auto* ctx, auto& req, auto* resp) {
        return stub_->GetCurrentUser(ctx, req, resp);
    }
);
```

---

## 4. 附录

### 4.1 Proto 编译命令

```bash
# 生成 C++ 代码
protoc --cpp_out=./generated \
       --grpc_out=./generated \
       --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
       -I. \
       pb_common/result.proto \
       pb_auth/auth.proto \
       pb_user/user.proto
```

### 4.2 错误码速查表

| 错误码 | 数值 | 含义 | HTTP 状态码 |
|--------|------|------|-------------|
| OK | 0 | 成功 | 200 |
| INVALID_ARGUMENT | 200 | 参数无效 | 400 |
| UNAUTHENTICATED | 1000 | 未认证 | 401 |
| TOKEN_EXPIRED | 1003 | Token 过期 | 401 |
| USER_NOT_FOUND | 2000 | 用户不存在 | 404 |
| MOBILE_TAKEN | 2013 | 手机号已占用 | 409 |
| PERMISSION_DENIED | 3000 | 无权限 | 403 |
| RATE_LIMITED | 300 | 请求过频 | 429 |
| INTERNAL | 101 | 内部错误 | 500 |

### 4.3 相关文件

- **Proto 定义**：`api/proto/`
- **C++ 错误码**：`include/common/error_codes.h`
- **Proto 转换器**：`include/common/proto_converter.h`
- **Handler 实现**：`src/handlers/`

