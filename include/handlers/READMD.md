# Handlers 模块 - gRPC 接口层

gRPC 服务接口的实现层，负责处理客户端请求、参数校验、调用业务服务并返回响应。

---

## 📁 目录结构

```
include/handlers/
├── auth_handler.h        # 认证接口 Handler
└── user_handler.h        # 用户接口 Handler

src/handlers/
├── auth_handler.cpp
├── user_handler.cpp
└── CMakeLists.txt
```

---

## 🏗️ 架构设计

### 分层架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              gRPC Client                                    │
│                    (Mobile App / Web / 其他微服务)                           │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ gRPC Request
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Handler 层 (本模块)                                │
│                                                                             │
│   ┌─────────────────────────────┐    ┌─────────────────────────────┐       │
│   │        AuthHandler          │    │        UserHandler          │       │
│   ├─────────────────────────────┤    ├─────────────────────────────┤       │
│   │ • SendVerifyCode            │    │ • GetCurrentUser            │       │
│   │ • Register                  │    │ • UpdateUser                │       │
│   │ • LoginByPassword           │    │ • ChangePassword            │       │
│   │ • LoginByCode               │    │ • DeleteUser                │       │
│   │ • RefreshToken              │    │ • GetUser (Admin)           │       │
│   │ • Logout                    │    │ • ListUsers (Admin)         │       │
│   │ • ResetPassword             │    └──────────────┬──────────────┘       │
│   │ • ValidateToken             │                   │                      │
│   └──────────────┬──────────────┘                   │                      │
│                  │                                  │                      │
│                  │  ┌───────────────────────────────┘                      │
│                  │  │                                                      │
│                  ▼  ▼                                                      │
│           ┌─────────────────┐                                              │
│           │  Authenticator  │  ← Token 验证 (UserHandler 需要)              │
│           └─────────────────┘                                              │
│                                                                             │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │ 调用 Service 层
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             Service 层                                      │
│              ┌─────────────────┐    ┌─────────────────┐                     │
│              │   AuthService   │    │   UserService   │                     │
│              └─────────────────┘    └─────────────────┘                     │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Handler 职责

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Handler 层职责                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   1. 接收 gRPC 请求                                                         │
│      ↓                                                                      │
│   2. 认证校验（需要登录的接口）                                               │
│      ↓                                                                      │
│   3. 参数校验（格式、必填项）                                                 │
│      ↓                                                                      │
│   4. 调用 Service 层                                                        │
│      ↓                                                                      │
│   5. 转换响应格式（Entity → Proto）                                          │
│      ↓                                                                      │
│   6. 返回 gRPC 响应                                                         │
│                                                                             │
│   ⚠️ Handler 不包含业务逻辑，仅做请求/响应的转换和校验                         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🔐 AuthHandler（认证接口）

处理用户认证相关的所有 gRPC 请求。

### 接口列表

| RPC 方法 | 功能 | 需要认证 |
|----------|------|:--------:|
| `SendVerifyCode` | 发送短信验证码 | ❌ |
| `Register` | 用户注册 | ❌ |
| `LoginByPassword` | 密码登录 | ❌ |
| `LoginByCode` | 验证码登录 | ❌ |
| `RefreshToken` | 刷新令牌 | ❌ |
| `Logout` | 登出 | ❌ |
| `ResetPassword` | 重置密码 | ❌ |
| `ValidateToken` | 验证令牌（内部接口） | ❌ |

### 接口定义

```cpp
class AuthHandler final : public ::pb_auth::AuthService::Service {
public:
    explicit AuthHandler(std::shared_ptr<AuthService> auth_service);

    // 验证码
    ::grpc::Status SendVerifyCode(::grpc::ServerContext* context, 
                                  const ::pb_auth::SendVerifyCodeRequest* request, 
                                  ::pb_auth::SendVerifyCodeResponse* response) override;
    
    // 注册
    ::grpc::Status Register(::grpc::ServerContext* context, 
                            const ::pb_auth::RegisterRequest* request, 
                            ::pb_auth::RegisterResponse* response) override;
    
    // 登录
    ::grpc::Status LoginByPassword(::grpc::ServerContext* context, 
                                   const ::pb_auth::LoginByPasswordRequest* request, 
                                   ::pb_auth::LoginByPasswordResponse* response) override;
    
    ::grpc::Status LoginByCode(::grpc::ServerContext* context, 
                               const ::pb_auth::LoginByCodeRequest* request, 
                               ::pb_auth::LoginByCodeResponse* response) override;
    
    // Token 管理
    ::grpc::Status RefreshToken(::grpc::ServerContext* context, 
                                const ::pb_auth::RefreshTokenRequest* request, 
                                ::pb_auth::RefreshTokenResponse* response) override;
    
    ::grpc::Status Logout(::grpc::ServerContext* context, 
                          const ::pb_auth::LogoutRequest* request, 
                          ::pb_auth::LogoutResponse* response) override;
    
    // 密码
    ::grpc::Status ResetPassword(::grpc::ServerContext* context, 
                                 const ::pb_auth::ResetPasswordRequest* request, 
                                 ::pb_auth::ResetPasswordResponse* response) override;
    
    // 内部验证
    ::grpc::Status ValidateToken(::grpc::ServerContext* context, 
                                 const ::pb_auth::ValidateTokenRequest* request, 
                                 ::pb_auth::ValidateTokenResponse* response) override;

private:
    std::shared_ptr<AuthService> auth_service_;
};
```

### Proto 定义对应

```protobuf
// pb_auth/auth.proto

service AuthService {
    rpc SendVerifyCode(SendVerifyCodeRequest) returns (SendVerifyCodeResponse);
    rpc Register(RegisterRequest) returns (RegisterResponse);
    rpc LoginByPassword(LoginByPasswordRequest) returns (LoginByPasswordResponse);
    rpc LoginByCode(LoginByCodeRequest) returns (LoginByCodeResponse);
    rpc RefreshToken(RefreshTokenRequest) returns (RefreshTokenResponse);
    rpc Logout(LogoutRequest) returns (LogoutResponse);
    rpc ResetPassword(ResetPasswordRequest) returns (ResetPasswordResponse);
    rpc ValidateToken(ValidateTokenRequest) returns (ValidateTokenResponse);
}
```

### 请求/响应示例

#### 1. 发送验证码

```protobuf
// 请求
message SendVerifyCodeRequest {
    string mobile = 1;      // 手机号
    SmsScene scene = 2;     // 场景：REGISTER/LOGIN/RESET_PASSWORD/DELETE_USER
}

// 响应
message SendVerifyCodeResponse {
    Result result = 1;      // 结果码 + 消息
    int32 retry_after = 2;  // 重发等待秒数
}
```

**处理流程：**
```
Request                      Handler                         Service
   │                            │                               │
   │ SendVerifyCodeRequest      │                               │
   │ ─────────────────────────► │                               │
   │                            │  1. 校验手机号格式             │
   │                            │  2. 校验 scene 枚举            │
   │                            │                               │
   │                            │  SendVerifyCode(mobile,scene) │
   │                            │ ─────────────────────────────►│
   │                            │                               │
   │                            │  Result<int32_t>              │
   │                            │ ◄─────────────────────────────│
   │                            │                               │
   │                            │  3. 设置响应                   │
   │ SendVerifyCodeResponse     │                               │
   │ ◄───────────────────────── │                               │
```

#### 2. 用户注册

```protobuf
// 请求
message RegisterRequest {
    string mobile = 1;
    string verify_code = 2;
    string password = 3;
    string display_name = 4;  // 可选
}

// 响应
message RegisterResponse {
    Result result = 1;
    UserInfo user = 2;
    TokenPair tokens = 3;
}
```

#### 3. 密码登录

```protobuf
// 请求
message LoginByPasswordRequest {
    string mobile = 1;
    string password = 2;
}

// 响应
message LoginByPasswordResponse {
    Result result = 1;
    UserInfo user = 2;
    TokenPair tokens = 3;
}
```

---

## 👤 UserHandler（用户接口）

处理用户信息管理相关的 gRPC 请求。

### 接口列表

| RPC 方法 | 功能 | 需要认证 | 权限要求 |
|----------|------|:--------:|----------|
| `GetCurrentUser` | 获取当前用户 | ✅ | 登录用户 |
| `UpdateUser` | 更新用户信息 | ✅ | 登录用户 |
| `ChangePassword` | 修改密码 | ✅ | 登录用户 |
| `DeleteUser` | 注销账号 | ✅ | 登录用户 |
| `GetUser` | 获取指定用户 | ✅ | 管理员 |
| `ListUsers` | 用户列表 | ✅ | 管理员 |

### 接口定义

```cpp
class UserHandler final : public ::pb_user::UserService::Service {
public:
    UserHandler(std::shared_ptr<UserService> user_service,
                std::shared_ptr<Authenticator> authenticator);

    // 当前用户操作
    ::grpc::Status GetCurrentUser(::grpc::ServerContext* context, 
                                  const ::pb_user::GetCurrentUserRequest* request, 
                                  ::pb_user::GetCurrentUserResponse* response) override;
    
    ::grpc::Status UpdateUser(::grpc::ServerContext* context, 
                              const ::pb_user::UpdateUserRequest* request, 
                              ::pb_user::UpdateUserResponse* response) override;
    
    ::grpc::Status ChangePassword(::grpc::ServerContext* context, 
                                  const ::pb_user::ChangePasswordRequest* request, 
                                  ::pb_user::ChangePasswordResponse* response) override;
    
    ::grpc::Status DeleteUser(::grpc::ServerContext* context, 
                              const ::pb_user::DeleteUserRequest* request, 
                              ::pb_user::DeleteUserResponse* response) override;

    // 管理员操作
    ::grpc::Status GetUser(::grpc::ServerContext* context, 
                           const ::pb_user::GetUserRequest* request, 
                           ::pb_user::GetUserResponse* response) override;
    
    ::grpc::Status ListUsers(::grpc::ServerContext* context, 
                             const ::pb_user::ListUsersRequest* request, 
                             ::pb_user::ListUsersResponse* response) override;

private:
    std::shared_ptr<UserService> user_service_;
    std::shared_ptr<Authenticator> authenticator_;  // 认证器
};
```

### Proto 定义对应

```protobuf
// pb_user/user.proto

service UserService {
    // 当前用户
    rpc GetCurrentUser(GetCurrentUserRequest) returns (GetCurrentUserResponse);
    rpc UpdateUser(UpdateUserRequest) returns (UpdateUserResponse);
    rpc ChangePassword(ChangePasswordRequest) returns (ChangePasswordResponse);
    rpc DeleteUser(DeleteUserRequest) returns (DeleteUserResponse);
    
    // 管理员接口
    rpc GetUser(GetUserRequest) returns (GetUserResponse);
    rpc ListUsers(ListUsersRequest) returns (ListUsersResponse);
}
```

### 认证流程详解

UserHandler 的所有接口都需要认证，认证流程如下：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           gRPC 请求认证流程                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   Client Request                                                            │
│        │                                                                    │
│        ▼                                                                    │
│   ┌─────────────────────────────────────────┐                               │
│   │ HTTP/2 Headers (gRPC Metadata)          │                               │
│   │ ─────────────────────────────────────── │                               │
│   │ authorization: Bearer eyJhbGciOi...     │  ← Access Token               │
│   │ content-type: application/grpc          │                               │
│   └─────────────────────────────────────────┘                               │
│        │                                                                    │
│        ▼                                                                    │
│   ┌─────────────────────────────────────────┐                               │
│   │ Authenticator.Authenticate(context)     │                               │
│   │                                         │                               │
│   │ 1. 从 metadata 提取 "authorization"     │                               │
│   │ 2. 校验 "Bearer " 前缀                   │                               │
│   │ 3. 提取 Token 部分                       │                               │
│   │ 4. 调用 JwtService.VerifyAccessToken    │                               │
│   │ 5. 返回 AuthContext                     │                               │
│   └─────────────────────────────────────────┘                               │
│        │                                                                    │
│        ├──────────────────┬─────────────────────────────────┐               │
│        │                  │                                 │               │
│        ▼ 成功              ▼ Token 缺失                      ▼ Token 无效    │
│   ┌──────────┐       ┌──────────────┐                 ┌──────────────┐      │
│   │AuthContext│       │ ErrorCode::  │                 │ ErrorCode::  │      │
│   │ user_id  │       │ Unauthenticated│                │ TokenInvalid │      │
│   │ user_uuid│       └──────────────┘                 │ TokenExpired │      │
│   │ mobile   │                                        └──────────────┘      │
│   │ role     │                                                              │
│   └──────────┘                                                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 代码实现示例

```cpp
::grpc::Status UserHandler::GetCurrentUser(
    ::grpc::ServerContext* context,
    const ::pb_user::GetCurrentUserRequest* request,
    ::pb_user::GetCurrentUserResponse* response) {
    
    LOG_DEBUG("GetCurrentUser requested");

    // ==================== 1. 认证 ====================
    auto auth_ctx = authenticator_->Authenticate(context);
    if (!auth_ctx.IsOk()) {
        // 认证失败，返回错误
        SetResultError(response->mutable_result(), auth_ctx.code, auth_ctx.message);
        return ::grpc::Status::OK;  // gRPC 状态始终返回 OK，业务错误放在 response 中
    }

    // ==================== 2. 调用业务逻辑 ====================
    auto result = user_service_->GetCurrentUser(auth_ctx.Value().user_uuid);

    // ==================== 3. 设置响应 ====================
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        // Entity → Proto 转换
        ToProtoUser(result.Value(), response->mutable_user());
    }

    return ::grpc::Status::OK;
}
```

### 管理员权限检查

```cpp
// 检查是否是管理员
inline Result<void> RequireAdmin(const AuthContext& auth) {
    if (auth.role != UserRole::Admin && auth.role != UserRole::SuperAdmin) {
        return Result<void>::Fail(ErrorCode::AdminRequired, "需要管理员权限");
    }
    return Result<void>::Ok();
}

// 在 Handler 中使用
::grpc::Status UserHandler::ListUsers(...) {
    // 1. 认证
    auto auth_ctx = authenticator_->Authenticate(context);
    if (!auth_ctx.IsOk()) {
        SetResultError(response->mutable_result(), auth_ctx.code, auth_ctx.message);
        return ::grpc::Status::OK;
    }

    // 2. 检查管理员权限
    auto admin_check = RequireAdmin(auth_ctx.Value());
    if (!admin_check.IsOk()) {
        SetResultError(response->mutable_result(), admin_check.code, admin_check.message);
        return ::grpc::Status::OK;
    }

    // 3. 业务逻辑...
}
```

---

## 🔄 数据转换

### Proto ↔ Entity 转换函数

所有转换函数定义在 `common/proto_converter.h`：

```cpp
// ErrorCode 转换
pb_common::ErrorCode ToProtoErrorCode(ErrorCode code);
ErrorCode FromProtoErrorCode(pb_common::ErrorCode code);

// Result 设置
void SetResultOk(pb_common::Result* result, const std::string& msg = "成功");
void SetResultError(pb_common::Result* result, ErrorCode code);
void SetResultError(pb_common::Result* result, ErrorCode code, const std::string& msg);

// SmsScene 枚举转换
pb_auth::SmsScene ToProtoSmsScene(SmsScene scene);
SmsScene FromProtoSmsScene(pb_auth::SmsScene scene);

// UserRole 枚举转换
pb_auth::UserRole ToProtoUserRole(UserRole role);
UserRole FromProtoUserRole(pb_auth::UserRole role);

// TokenPair 转换
void ToProtoTokenPair(const TokenPair& src, pb_auth::TokenPair* dst);

// UserEntity 转换
void ToProtoUserInfo(const UserEntity& src, pb_auth::UserInfo* dst);  // 登录/注册用
void ToProtoUser(const UserEntity& src, pb_user::User* dst);          // 完整信息

// 时间转换
google::protobuf::Timestamp ToProtoTimestamp(const std::string& datetime_str);
```

### 转换流程图

```
┌─────────────────┐                           ┌─────────────────┐
│  gRPC Request   │                           │  gRPC Response  │
│  (Proto Types)  │                           │  (Proto Types)  │
└────────┬────────┘                           └────────▲────────┘
         │                                             │
         │ FromProto*                                  │ ToProto*
         ▼                                             │
┌─────────────────┐                           ┌────────┴────────┐
│ Handler 层      │ ────────────────────────► │ Handler 层      │
│ 参数校验        │                           │ 设置响应         │
└────────┬────────┘                           └────────▲────────┘
         │                                             │
         │ 业务参数                                     │ Result<T>
         ▼                                             │
┌─────────────────┐                           ┌────────┴────────┐
│ Service 层      │ ────────────────────────► │ Service 层      │
│ (业务类型)      │                           │ (业务类型)      │
└─────────────────┘                           └─────────────────┘
```

---

## 🚨 错误处理

### 错误码映射

```cpp
enum class ErrorCode {
    // 成功
    Ok = 0,
    
    // 参数错误 (2xx)
    InvalidArgument = 200,
    
    // 认证错误 (1000~1999)
    Unauthenticated = 1000,
    TokenMissing = 1001,
    TokenInvalid = 1002,
    TokenExpired = 1003,
    
    // 权限错误 (3000~3999)
    PermissionDenied = 3000,
    AdminRequired = 3001,
    
    // ... 更多错误码
};
```

### 统一错误响应格式

```protobuf
message Result {
    ErrorCode code = 1;  // 错误码
    string msg = 2;      // 错误消息
}
```

### 错误处理原则

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           错误处理原则                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   1. gRPC Status 始终返回 OK                                                │
│      • 业务错误放在 response.result 中                                       │
│      • 只有系统级错误（如序列化失败）才返回非 OK 状态                          │
│                                                                             │
│   2. 错误消息用户友好                                                        │
│      • 不暴露内部细节（如 SQL 错误）                                          │
│      • 使用 GetErrorMessage(code) 获取标准消息                               │
│                                                                             │
│   3. 安全考虑                                                               │
│      • 登录失败返回模糊提示："账号或密码错误"                                  │
│      • 不暴露用户是否存在                                                    │
│                                                                             │
│   4. 日志记录                                                               │
│      • 错误详情记录到日志                                                    │
│      • 客户端只看到友好消息                                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 错误处理示例

```cpp
::grpc::Status AuthHandler::LoginByPassword(...) {
    // 1. 参数校验
    std::string error;
    if (!IsValidMobile(request->mobile(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;  // gRPC OK，业务错误在 response 中
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->LoginByPassword(
        request->mobile(),
        request->password()
    );

    // 3. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        const auto& auth_result = result.Value();
        ToProtoUserInfo(auth_result.user, response->mutable_user());
        ToProtoTokenPair(auth_result.tokens, response->mutable_tokens());
    }

    return ::grpc::Status::OK;
}
```

---

## 🔧 服务注册

### 在 main.cpp 中注册 Handler

```cpp
#include "handlers/auth_handler.h"
#include "handlers/user_handler.h"

int main() {
    // 1. 创建依赖
    auto auth_service = std::make_shared<AuthService>(...);
    auto user_service = std::make_shared<UserService>(...);
    auto authenticator = std::make_shared<JwtAuthenticator>(jwt_service);
    
    // 2. 创建 Handler
    auto auth_handler = std::make_unique<AuthHandler>(auth_service);
    auto user_handler = std::make_unique<UserHandler>(user_service, authenticator);
    
    // 3. 注册到 gRPC Server
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(auth_handler.get());  // 注册认证服务
    builder.RegisterService(user_handler.get());  // 注册用户服务
    
    // 4. 启动服务
    auto server = builder.BuildAndStart();
    server->Wait();
}
```

---

## 📡 客户端调用示例

### C++ gRPC 客户端

```cpp
#include "pb_auth/auth.grpc.pb.h"
#include "pb_user/user.grpc.pb.h"

// 创建 Channel
auto channel = grpc::CreateChannel("localhost:50051", 
                                   grpc::InsecureChannelCredentials());

// ==================== 认证服务调用 ====================
auto auth_stub = pb_auth::AuthService::NewStub(channel);

// 登录
grpc::ClientContext login_ctx;
pb_auth::LoginByPasswordRequest login_req;
pb_auth::LoginByPasswordResponse login_resp;

login_req.set_mobile("13800138000");
login_req.set_password("MyPassword123");

auto status = auth_stub->LoginByPassword(&login_ctx, login_req, &login_resp);

if (login_resp.result().code() == pb_common::ErrorCode::OK) {
    std::string access_token = login_resp.tokens().access_token();
    std::string refresh_token = login_resp.tokens().refresh_token();
}

// ==================== 用户服务调用（需要认证）====================
auto user_stub = pb_user::UserService::NewStub(channel);

grpc::ClientContext user_ctx;
// 设置 Authorization Header
user_ctx.AddMetadata("authorization", "Bearer " + access_token);

pb_user::GetCurrentUserRequest user_req;
pb_user::GetCurrentUserResponse user_resp;

status = user_stub->GetCurrentUser(&user_ctx, user_req, &user_resp);

if (user_resp.result().code() == pb_common::ErrorCode::OK) {
    auto& user = user_resp.user();
    // user.id(), user.mobile(), user.display_name()...
}
```

### grpcurl 命令行测试

```bash
# 发送验证码
grpcurl -plaintext -d '{
  "mobile": "13800138000",
  "scene": "SMS_SCENE_REGISTER"
}' localhost:50051 pb_auth.AuthService/SendVerifyCode

# 注册
grpcurl -plaintext -d '{
  "mobile": "13800138000",
  "verify_code": "123456",
  "password": "MyPassword123",
  "display_name": "张三"
}' localhost:50051 pb_auth.AuthService/Register

# 登录
grpcurl -plaintext -d '{
  "mobile": "13800138000",
  "password": "MyPassword123"
}' localhost:50051 pb_auth.AuthService/LoginByPassword

# 获取当前用户（需要 Token）
grpcurl -plaintext \
  -H "authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..." \
  -d '{}' \
  localhost:50051 pb_user.UserService/GetCurrentUser

# 用户列表（管理员）
grpcurl -plaintext \
  -H "authorization: Bearer <admin_token>" \
  -d '{
    "page": {"page": 1, "page_size": 20},
    "mobile_filter": "138"
  }' \
  localhost:50051 pb_user.UserService/ListUsers
```

---

## ⚠️ 注意事项

### 1. gRPC 状态码使用规范

```cpp
// ✅ 正确：业务错误放在 response 中
SetResultError(response->mutable_result(), ErrorCode::UserNotFound, "用户不存在");
return ::grpc::Status::OK;

// ❌ 错误：不要用 gRPC 状态码表示业务错误
return ::grpc::Status(grpc::StatusCode::NOT_FOUND, "用户不存在");
```

### 2. 参数校验顺序

```cpp
// Handler 层：格式校验
if (!IsValidMobile(request->mobile(), error)) { ... }
if (!IsValidPassword(request->password(), error)) { ... }

// Service 层：业务校验
// 如：手机号是否已注册、用户是否存在等
```

### 3. 认证 Token 传递

```cpp
// 客户端设置
context.AddMetadata("authorization", "Bearer " + access_token);

// 服务端读取
auto metadata = context->client_metadata();
auto it = metadata.find("authorization");  // key 必须小写
```

### 4. 日志规范

```cpp
// 请求开始
LOG_INFO("LoginByPassword: mobile={}", request->mobile());

// 不要记录敏感信息
LOG_INFO("Login: mobile={}, password={}", mobile, password);  // ❌ 错误

// 错误详情
LOG_ERROR("Login failed: mobile={}, error={}", mobile, result.message);
```

### 5. 线程安全

- Handler 实例被多个 gRPC 工作线程共享
- 不要在 Handler 中存储请求相关的状态
- Service 层已保证线程安全

---

## 📚 相关模块

| 模块 | 说明 |
|------|------|
| `service/auth_service` | 认证业务逻辑 |
| `service/user_service` | 用户业务逻辑 |
| `auth/authenticator` | Token 认证接口 |
| `auth/jwt_authenticator` | JWT 认证实现 |
| `common/proto_converter` | Proto 转换工具 |
| `common/validator` | 参数校验工具 |
| `api/proto/` | Proto 定义文件 |

