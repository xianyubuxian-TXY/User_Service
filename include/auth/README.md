# Auth 模块设计文档

## 目录结构

```
include/auth/
├── authenticator.h         # 认证器接口（抽象基类）
├── jwt_authenticator.h     # JWT 认证器实现
├── jwt_service.h           # JWT 令牌服务
├── sms_service.h           # 短信验证码服务
├── token_repository.h      # Token 持久化仓库
└── token_cleanup_task.h    # Token 清理定时任务

src/auth/
├── CMakeLists.txt
├── jwt_service.cpp
├── sms_service.cpp
├── token_repository.cpp
└── token_cleanup_task.cpp
```

---

## 1. 模块概述

### 1.1 模块职责划分

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Auth 模块架构                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                     对外接口层                                   │   │
│  │                                                                  │   │
│  │   Authenticator (抽象接口)                                       │   │
│  │        │                                                         │   │
│  │        └──► JwtAuthenticator (实现)                              │   │
│  │             • 从 gRPC Context 提取 Token                         │   │
│  │             • 调用 JwtService 验证 Token                         │   │
│  │             • 返回 AuthContext（用户身份信息）                   │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                     核心服务层                                   │   │
│  │                                                                  │   │
│  │  ┌──────────────────┐    ┌──────────────────┐                   │   │
│  │  │   JwtService     │    │   SmsService     │                   │   │
│  │  │                  │    │                  │                   │   │
│  │  │ • 生成 Token     │    │ • 发送验证码     │                   │   │
│  │  │ • 验证 Token     │    │ • 验证验证码     │                   │   │
│  │  │ • 解析 Token     │    │ • 限流控制       │                   │   │
│  │  │ • 哈希 Token     │    │ • 防暴力破解     │                   │   │
│  │  └──────────────────┘    └──────────────────┘                   │   │
│  │                                                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                     数据持久层                                   │   │
│  │                                                                  │   │
│  │  ┌──────────────────┐    ┌──────────────────┐                   │   │
│  │  │ TokenRepository  │    │TokenCleanupTask  │                   │   │
│  │  │                  │    │                  │                   │   │
│  │  │ • 存储 Token     │    │ • 定时清理       │                   │   │
│  │  │ • 验证 Token     │    │ • 过期 Token     │                   │   │
│  │  │ • 删除 Token     │    │ • 后台运行       │                   │   │
│  │  └──────────────────┘    └──────────────────┘                   │   │
│  │           │                       │                              │   │
│  │           ▼                       ▼                              │   │
│  │  ┌─────────────────────────────────────────────────────────┐    │   │
│  │  │             MySQL (user_sessions 表)                     │    │   │
│  │  └─────────────────────────────────────────────────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 组件依赖关系

| 组件 | 依赖 | 说明 |
|------|------|------|
| `JwtAuthenticator` | `JwtService` | 委托 JWT 验证 |
| `JwtService` | `SecurityConfig` | 读取密钥和有效期配置 |
| `SmsService` | `RedisClient`, `SmsConfig` | 验证码存储和配置 |
| `TokenRepository` | `MySQLPool` | 数据库连接池 |
| `TokenCleanupTask` | `TokenRepository` | 调用清理方法 |

---

## 2. 模块使用示例

### 2.1 初始化所有组件

```cpp
#include "auth/jwt_service.h"
#include "auth/jwt_authenticator.h"
#include "auth/sms_service.h"
#include "auth/token_repository.h"
#include "auth/token_cleanup_task.h"

// ==================== 组件初始化 ====================

// 1. 加载配置
auto config = std::make_shared<Config>(Config::LoadFromFile("config.yaml"));

// 2. 创建 MySQL 连接池
auto mysql_pool = std::make_shared<MySQLPool>(
    config,
    [](const MySQLConfig& cfg) {
        return std::make_unique<MySQLConnection>(cfg);
    }
);

// 3. 创建 Redis 客户端
auto redis_client = std::make_shared<RedisClient>(config->redis);

// 4. 创建 Auth 模块组件
auto jwt_service = std::make_shared<JwtService>(config->security);
auto sms_service = std::make_shared<SmsService>(redis_client, config->sms);
auto token_repo = std::make_shared<TokenRepository>(mysql_pool);
auto authenticator = std::make_shared<JwtAuthenticator>(jwt_service);

// 5. 启动 Token 清理任务（每 60 分钟清理一次过期 Token）
auto cleanup_task = std::make_shared<TokenCleanupTask>(token_repo, 60);
cleanup_task->Start();

// ==================== 使用组件 ====================

// 在 AuthService 中使用
class AuthService {
public:
    AuthService(
        std::shared_ptr<JwtService> jwt_srv,
        std::shared_ptr<SmsService> sms_srv,
        std::shared_ptr<TokenRepository> token_repo
    ) : jwt_srv_(jwt_srv), sms_srv_(sms_srv), token_repo_(token_repo) {}
    
    // ... 业务方法
};

// 在 Handler 中使用 Authenticator
class UserHandler {
public:
    UserHandler(std::shared_ptr<Authenticator> auth)
        : authenticator_(auth) {}
    
    grpc::Status GetCurrentUser(grpc::ServerContext* context, ...) {
        // 认证
        auto auth_result = authenticator_->Authenticate(context);
        if (!auth_result.IsOk()) {
            // 认证失败
            return grpc::Status::OK;
        }
        
        auto& auth_ctx = auth_result.Value();
        // 使用 auth_ctx.user_id, auth_ctx.user_uuid 等
    }
};
```

### 2.2 完整认证流程示例

```cpp
// ==================== 用户注册流程 ====================

// 1. 发送验证码
auto send_result = sms_service->SendCaptcha(SmsScene::Register, "13800138000");
if (!send_result.IsOk()) {
    // 发送失败（限流/锁定等）
    return send_result;
}
int32_t retry_after = send_result.Value();  // 可重发时间

// 2. 验证验证码
auto verify_result = sms_service->VerifyCaptcha(
    SmsScene::Register, 
    "13800138000", 
    "123456"
);
if (!verify_result.IsOk()) {
    // 验证失败
    return verify_result;
}

// 3. 创建用户（UserDB）
UserEntity user;
user.mobile = "13800138000";
user.password_hash = PasswordHelper::Hash("password123");
auto create_result = user_db->Create(user);

// 4. 生成 Token 对
auto token_pair = jwt_service->GenerateTokenPair(create_result.Value());

// 5. 存储 Refresh Token（哈希存储）
std::string token_hash = JwtService::HashToken(token_pair.refresh_token);
token_repo->SaveRefreshToken(
    user.id, 
    token_hash, 
    config->security.refresh_token_ttl_seconds
);

// 6. 消费验证码（防止重复使用）
sms_service->ConsumeCaptcha(SmsScene::Register, "13800138000");

// 7. 返回结果
return AuthResult{ user, token_pair };
```

---

## 3. JwtService 详解

### 3.1 功能概述

`JwtService` 是 JWT 令牌的核心处理类，负责：

- 生成 Access Token 和 Refresh Token
- 验证和解析 Token
- Token 哈希（用于安全存储）

### 3.2 类定义

```cpp
class JwtService {
public:
    explicit JwtService(const SecurityConfig& config);
    
    // 生成双令牌对
    TokenPair GenerateTokenPair(const UserEntity& user);
    
    // 验证 Access Token
    Result<AccessTokenPayload> VerifyAccessToken(const std::string& token);
    
    // 解析 Refresh Token（返回用户 ID）
    Result<std::string> ParseRefreshToken(const std::string& token);
    
    // Token 哈希（静态方法）
    static std::string HashToken(const std::string& token);
    
private:
    SecurityConfig config_;
    
    std::string GenerateAccessToken(const UserEntity& user);
    std::string GenerateRefreshToken(const UserEntity& user);
    
    // JWT 底层工具
    static std::string Base64UrlEncode(const std::string& input);
    static std::string Base64UrlDecode(const std::string& input);
    static std::string HmacSha256(const std::string& key, const std::string& data);
    static std::string CreateJwt(const std::map<std::string, std::string>& claims,
                                  const std::string& secret);
    static JwtVerifyResult VerifyJwt(const std::string& token, 
                                      const std::string& secret);
};
```

### 3.3 JWT 结构详解

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         JWT 令牌结构                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.                                  │
│  eyJpc3MiOiJ1c2VyLXNlcnZpY2UiLCJzdWIiOiJhY2Nlc3MiLC4uLn0.               │
│  SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c                            │
│                                                                         │
│  ├─────────────────┼─────────────────────────────┼────────────────┤    │
│  │     Header      │          Payload            │   Signature    │    │
│  │   (Base64URL)   │        (Base64URL)          │  (Base64URL)   │    │
│  └─────────────────┴─────────────────────────────┴────────────────┘    │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Header (固定)                                                   │   │
│  │  {                                                               │   │
│  │    "alg": "HS256",    // 签名算法                                │   │
│  │    "typ": "JWT"       // 令牌类型                                │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Access Token Payload                                            │   │
│  │  {                                                               │   │
│  │    "iss": "user-service",  // 签发者                             │   │
│  │    "sub": "access",        // 令牌类型标识                       │   │
│  │    "id": "12345",          // 数据库用户 ID                      │   │
│  │    "uid": "uuid-xxx",      // 用户 UUID（对外）                  │   │
│  │    "mobile": "138xxx",     // 手机号                             │   │
│  │    "role": "0",            // 用户角色                           │   │
│  │    "iat": 1704067200,      // 签发时间                           │   │
│  │    "exp": 1704068100,      // 过期时间（+15分钟）                │   │
│  │    "jti": "582943"         // 唯一标识                           │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Refresh Token Payload（最小化）                                 │   │
│  │  {                                                               │   │
│  │    "iss": "user-service",                                        │   │
│  │    "sub": "refresh",       // 标识为 Refresh Token               │   │
│  │    "uid": "12345",         // 仅保留用户 ID                      │   │
│  │    "iat": 1704067200,                                            │   │
│  │    "exp": 1704672000,      // 过期时间（+7天）                   │   │
│  │    "jti": "847291"                                               │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Signature                                                       │   │
│  │                                                                  │   │
│  │  HMAC-SHA256(                                                    │   │
│  │    base64UrlEncode(header) + "." + base64UrlEncode(payload),    │   │
│  │    secret                                                        │   │
│  │  )                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.4 核心方法实现

#### 3.4.1 生成 Token 对

```cpp
TokenPair JwtService::GenerateTokenPair(const UserEntity& user) {
    TokenPair result;
    result.access_token = GenerateAccessToken(user);
    result.refresh_token = GenerateRefreshToken(user);
    result.expires_in = config_.access_token_ttl_seconds;
    return result;
}

std::string JwtService::GenerateAccessToken(const UserEntity& user) {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(config_.access_token_ttl_seconds);
    
    // 随机 jti（确保唯一性）
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    std::map<std::string, std::string> claims;
    claims["iss"] = config_.jwt_issuer;
    claims["sub"] = "access";                              // 标识令牌类型
    claims["id"]  = std::to_string(user.id);              // 数据库 ID
    claims["uid"] = user.uuid;                             // 对外 UUID
    claims["mobile"] = user.mobile;
    claims["role"] = UserRoleToString(user.role);
    claims["iat"] = std::to_string(std::chrono::system_clock::to_time_t(now));
    claims["exp"] = std::to_string(std::chrono::system_clock::to_time_t(exp));
    claims["jti"] = std::to_string(dis(gen));
    
    return CreateJwt(claims, config_.jwt_secret);
}
```

#### 3.4.2 验证 Access Token

```cpp
Result<AccessTokenPayload> JwtService::VerifyAccessToken(const std::string& token) {
    // 1. 空值检查
    if (token.empty()) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenMissing, "Token 不能为空");
    }
    
    // 2. JWT 验证（签名 + 过期时间）
    auto verify_result = VerifyJwt(token, config_.jwt_secret);
    if (!verify_result.success) {
        return Result<AccessTokenPayload>::Fail(verify_result.error_code);
    }
    
    const auto& claims = verify_result.claims;
    
    // 3. 签发者校验
    if (claims.at("iss") != config_.jwt_issuer) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌签发者不匹配");
    }
    
    // 4. 令牌类型校验（必须是 access，不能用 refresh token 调接口）
    if (claims.at("sub") != "access") {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌类型不匹配");
    }
    
    // 5. 解析为结构化载荷
    AccessTokenPayload payload;
    payload.user_id = std::stoll(claims.at("id"));
    payload.user_uuid = claims.at("uid");
    payload.mobile = claims.at("mobile");
    payload.role = StringToUserRole(claims.at("role"));
    payload.expires_at = std::chrono::system_clock::from_time_t(
        std::stoll(claims.at("exp"))
    );
    
    return Result<AccessTokenPayload>::Ok(payload);
}
```

#### 3.4.3 Token 哈希

```cpp
std::string JwtService::HashToken(const std::string& token) {
    // 使用 SHA256 哈希，避免明文存储
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(token.c_str()), 
           token.length(), hash);
    
    // 转为 64 字符的十六进制字符串
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}
```

### 3.5 使用示例

```cpp
auto jwt_service = std::make_shared<JwtService>(config.security);

// 生成 Token
UserEntity user;
user.id = 12345;
user.uuid = "550e8400-e29b-41d4...";
user.mobile = "13800138000";
user.role = UserRole::User;

auto token_pair = jwt_service->GenerateTokenPair(user);
// token_pair.access_token  = "eyJhbG..."
// token_pair.refresh_token = "eyJhbG..."
// token_pair.expires_in    = 900

// 验证 Access Token
auto verify_result = jwt_service->VerifyAccessToken(token_pair.access_token);
if (verify_result.IsOk()) {
    auto& payload = verify_result.Value();
    // payload.user_id   = 12345
    // payload.user_uuid = "550e8400-e29b-41d4..."
    // payload.role      = UserRole::User
}

// 哈希 Token（用于存储）
std::string token_hash = JwtService::HashToken(token_pair.refresh_token);
// token_hash = "a1b2c3d4e5f6..." (64 字符)
```

---

## 4. SmsService 详解

### 4.1 功能概述

`SmsService` 负责短信验证码的完整生命周期管理：

- 生成和发送验证码
- 验证码校验
- 防暴力破解（错误次数限制）
- 防频繁发送（发送间隔限制）
- 场景隔离（不同场景验证码独立）

### 4.2 类定义

```cpp
class SmsService {
public:
    SmsService(std::shared_ptr<RedisClient> redis, const SmsConfig& config);
    
    // 发送验证码，返回可重发时间（秒）
    Result<int32_t> SendCaptcha(SmsScene scene, const std::string& mobile);
    
    // 验证验证码
    Result<void> VerifyCaptcha(SmsScene scene, 
                               const std::string& mobile,
                               const std::string& code);
    
    // 消费验证码（验证成功后删除）
    Result<void> ConsumeCaptcha(SmsScene scene, const std::string& mobile);
    
private:
    // Redis Key 生成
    std::string CaptchaKey(SmsScene scene, const std::string& mobile);
    std::string IntervalKey(const std::string& mobile);
    std::string VerifyCountKey(SmsScene scene, const std::string& mobile);
    std::string LockKey(SmsScene scene, const std::string& mobile);
    
    std::string GenerateCaptcha();
    Result<void> DoSend(const std::string& mobile, const std::string& code, SmsScene scene);
    static std::string SceneName(SmsScene scene);
    
    std::shared_ptr<RedisClient> redis_;
    SmsConfig config_;
};
```

### 4.3 Redis Key 设计

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    SmsService Redis Key 设计                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Key 类型              格式                          用途                │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  验证码存储            sms:code:{scene}:{mobile}     存储验证码          │
│                        示例: sms:code:login:138xxx   值: "123456"       │
│                        TTL: 5分钟                                       │
│                                                                         │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  发送间隔              sms:interval:{mobile}         防频繁发送          │
│                        示例: sms:interval:138xxx    值: "1"             │
│                        TTL: 60秒                    全场景共享           │
│                                                                         │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  验证错误计数          sms:verify_count:{scene}:{mobile}  防暴力破解    │
│                        示例: sms:verify_count:login:138xxx              │
│                        值: "3" (已错误3次)                              │
│                        TTL: 5分钟                                       │
│                                                                         │
│  ─────────────────────────────────────────────────────────────────────  │
│                                                                         │
│  锁定状态              sms:lock:{scene}:{mobile}     临时锁定            │
│                        示例: sms:lock:login:138xxx                      │
│                        值: "1"                                          │
│                        TTL: 30分钟                                      │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.4 发送验证码流程

```cpp
Result<int32_t> SmsService::SendCaptcha(SmsScene scene, const std::string& mobile) {
    
    // 1. 检查是否被锁定（之前验证失败过多）
    auto lock_exists = redis_->Exists(LockKey(scene, mobile));
    if (lock_exists.IsOk() && lock_exists.Value()) {
        int64_t ttl = GetTTLSeconds(LockKey(scene, mobile));
        return Result<int32_t>::Fail(
            ErrorCode::RateLimited,
            fmt::format("操作过于频繁，请{}秒后再试", ttl)
        );
    }
    
    // 2. 检查发送间隔（60秒内不能重复发送）
    auto interval_exists = redis_->Exists(IntervalKey(mobile));
    if (interval_exists.IsOk() && interval_exists.Value()) {
        int64_t ttl = GetTTLSeconds(IntervalKey(mobile));
        return Result<int32_t>::Fail(
            ErrorCode::RateLimited,
            fmt::format("请{}秒后再试", ttl)
        );
    }
    
    // 3. 生成验证码
    std::string code = GenerateCaptcha();  // "123456"
    
    // 4. 存储到 Redis（原子操作）
    redis_->SetPx(
        CaptchaKey(scene, mobile),    // sms:code:login:138xxx
        code,                          // "123456"
        std::chrono::milliseconds(config_.code_ttl_seconds * 1000)  // 5分钟
    );
    
    // 5. 设置发送间隔
    redis_->SetPx(
        IntervalKey(mobile),          // sms:interval:138xxx
        "1",
        std::chrono::milliseconds(config_.send_interval_seconds * 1000)  // 60秒
    );
    
    // 6. 发送短信（对接短信服务商）
    auto send_result = DoSend(mobile, code, scene);
    if (!send_result.IsOk()) {
        // 发送失败，回滚（删除已存储的验证码）
        redis_->Del(CaptchaKey(scene, mobile));
        return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable, "短信发送失败");
    }
    
    return Result<int32_t>::Ok(config_.send_interval_seconds);  // 返回可重发时间
}
```

### 4.5 验证验证码流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     VerifyCaptcha 流程图                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  输入: scene=Login, mobile="138xxx", code="123456"                      │
│                              │                                          │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 1. 检查是否被锁定                                                │   │
│  │    EXISTS sms:lock:login:138xxx                                  │   │
│  │    → 存在: 返回 RateLimited 错误                                 │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │ 不存在                                   │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 2. 获取正确的验证码                                              │   │
│  │    GET sms:code:login:138xxx                                     │   │
│  │    → 不存在: 返回 CaptchaExpired 错误                            │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                              │ 存在，值为 "654321"                      │
│                              ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ 3. 比对验证码                                                    │   │
│  │    "123456" == "654321" ?                                        │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│              │                               │                          │
│              │ 不匹配                         │ 匹配                     │
│              ▼                               ▼                          │
│  ┌─────────────────────────┐    ┌─────────────────────────────────┐   │
│  │ 4a. 增加错误计数         │    │ 4b. 验证成功                     │   │
│  │     INCR verify_count    │    │     清除错误计数                 │   │
│  │                          │    │     DEL verify_count             │   │
│  │     count >= 5?          │    │     返回 OK                      │   │
│  │     ├─ Yes: 触发锁定     │    └─────────────────────────────────┘   │
│  │     │   SET lock (30min) │                                          │
│  │     │   DEL code         │                                          │
│  │     │   DEL verify_count │                                          │
│  │     │   返回 AccountLocked│                                         │
│  │     └─ No: 返回 CaptchaWrong                                        │
│  │            还剩 N 次机会  │                                          │
│  └──────────────────────────┘                                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.6 使用示例

```cpp
auto sms_service = std::make_shared<SmsService>(redis_client, config.sms);

// 发送验证码
auto send_result = sms_service->SendCaptcha(SmsScene::Login, "13800138000");
if (send_result.IsOk()) {
    int32_t retry_after = send_result.Value();
    // 提示用户：验证码已发送，{retry_after}秒后可重发
}

// 验证验证码
auto verify_result = sms_service->VerifyCaptcha(
    SmsScene::Login, 
    "13800138000", 
    "123456"
);

if (verify_result.IsOk()) {
    // 验证成功，继续后续业务
    
    // 业务成功后消费验证码（防止重复使用）
    sms_service->ConsumeCaptcha(SmsScene::Login, "13800138000");
} else {
    // 处理错误
    switch (verify_result.code) {
        case ErrorCode::CaptchaExpired:
            // 验证码已过期
            break;
        case ErrorCode::CaptchaWrong:
            // 验证码错误
            break;
        case ErrorCode::AccountLocked:
            // 错误次数过多，被锁定
            break;
    }
}
```

---

## 5. TokenRepository 详解

### 5.1 功能概述

`TokenRepository` 负责 Refresh Token 的持久化存储和管理：

- 存储 Refresh Token 哈希（非明文）
- 验证 Token 有效性
- 删除 Token（登出）
- 清理过期 Token

### 5.2 数据库表结构

```sql
CREATE TABLE IF NOT EXISTS `user_sessions` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    `token_hash` VARCHAR(64) NOT NULL COMMENT 'Token哈希(SHA256)',
    `expires_at` DATETIME NOT NULL COMMENT '过期时间',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_token_hash` (`token_hash`),
    KEY `idx_user_id` (`user_id`),
    KEY `idx_expires_at` (`expires_at`),
    CONSTRAINT `fk_session_user` FOREIGN KEY (`user_id`) 
        REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户会话表';
```

### 5.3 类定义

```cpp
class TokenRepository {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    explicit TokenRepository(std::shared_ptr<MySQLPool> pool);
    
    // ==================== 创建 ====================
    Result<void> SaveRefreshToken(int64_t user_id,
                                   const std::string& token_hash,
                                   int expires_in_seconds);
    
    // ==================== 查询 ====================
    Result<TokenSession> FindByTokenHash(const std::string& token_hash);
    Result<bool> IsTokenValid(const std::string& token_hash);
    Result<int64_t> CountActiveSessionsByUserId(int64_t user_id);
    
    // ==================== 删除 ====================
    Result<void> DeleteByTokenHash(const std::string& token_hash);
    Result<void> DeleteByUserId(int64_t user_id);
    Result<int64_t> CleanExpiredTokens();
    
private:
    std::shared_ptr<MySQLPool> pool_;
    
    TokenSession ParseRow(MySQLResult& res);
    static std::string FormatDatetime(std::chrono::system_clock::time_point tp);
};
```

### 5.4 核心方法实现

#### 5.4.1 存储 Refresh Token

```cpp
Result<void> TokenRepository::SaveRefreshToken(
    int64_t user_id,
    const std::string& token_hash,
    int expires_in_seconds
) {
    try {
        auto conn = pool_->CreateConnection();
        
        // 计算过期时间
        auto expires_at = std::chrono::system_clock::now() 
                        + std::chrono::seconds(expires_in_seconds);
        std::string expires_at_str = FormatDatetime(expires_at);
        
        std::string sql = 
            "INSERT INTO user_sessions (user_id, token_hash, expires_at) "
            "VALUES (?, ?, ?)";
        
        conn->Execute(sql, {
            std::to_string(user_id),
            token_hash,           // 哈希值，非明文
            expires_at_str
        });
        
        return Result<void>::Ok();
        
    } catch (const MySQLDuplicateKeyException& e) {
        // Token 哈希冲突（理论上不会发生）
        return Result<void>::Fail(ErrorCode::Internal, "Token 保存失败");
    } catch (const std::exception& e) {
        return Result<void>::Fail(ErrorCode::ServiceUnavailable);
    }
}
```

#### 5.4.2 验证 Token 有效性

```cpp
Result<bool> TokenRepository::IsTokenValid(const std::string& token_hash) {
    try {
        auto conn = pool_->CreateConnection();
        
        // 检查：存在 AND 未过期
        std::string sql = 
            "SELECT 1 FROM user_sessions "
            "WHERE token_hash = ? AND expires_at > NOW() "
            "LIMIT 1";
        
        auto res = conn->Query(sql, {token_hash});
        bool valid = res.Next();  // 有结果则有效
        
        return Result<bool>::Ok(valid);
        
    } catch (const std::exception& e) {
        return Result<bool>::Fail(ErrorCode::ServiceUnavailable);
    }
}
```

#### 5.4.3 清理过期 Token

```cpp
Result<int64_t> TokenRepository::CleanExpiredTokens() {
    try {
        auto conn = pool_->CreateConnection();
        
        std::string sql = "DELETE FROM user_sessions WHERE expires_at <= NOW()";
        auto affected = conn->Execute(sql, {});
        
        LOG_INFO("Cleaned {} expired tokens", affected);
        return Result<int64_t>::Ok(static_cast<int64_t>(affected));
        
    } catch (const std::exception& e) {
        return Result<int64_t>::Fail(ErrorCode::ServiceUnavailable);
    }
}
```

### 5.5 使用示例

```cpp
auto token_repo = std::make_shared<TokenRepository>(mysql_pool);

// 存储 Refresh Token
std::string refresh_token = jwt_service->GenerateRefreshToken(user);
std::string token_hash = JwtService::HashToken(refresh_token);

token_repo->SaveRefreshToken(
    user.id,
    token_hash,
    config.security.refresh_token_ttl_seconds  // 7天
);

// 刷新 Token 时验证
auto valid_result = token_repo->IsTokenValid(token_hash);
if (valid_result.IsOk() && valid_result.Value()) {
    // Token 有效
    // 1. 删除旧 Token
    token_repo->DeleteByTokenHash(token_hash);
    // 2. 生成并存储新 Token
    // ...
}

// 登出：删除单个 Token
token_repo->DeleteByTokenHash(token_hash);

// 强制登出所有设备
token_repo->DeleteByUserId(user.id);
```

---

## 6. TokenCleanupTask 详解

### 6.1 功能概述

`TokenCleanupTask` 是一个后台定时任务，定期清理数据库中过期的 Refresh Token，防止 `user_sessions` 表无限增长。

### 6.2 类定义

```cpp
class TokenCleanupTask {
public:
    TokenCleanupTask(std::shared_ptr<TokenRepository> token_repo, 
                     int interval_minutes = 60);
    ~TokenCleanupTask();
    
    // 禁止拷贝和移动
    TokenCleanupTask(const TokenCleanupTask&) = delete;
    TokenCleanupTask& operator=(const TokenCleanupTask&) = delete;
    
    void Start();
    void Stop();
    
private:
    std::shared_ptr<TokenRepository> token_repo_;
    int interval_;                // 清理间隔（分钟）
    std::atomic<bool> running_;   // 运行状态
    std::thread thread_;          // 后台线程
    std::mutex mutex_;            // 保护 Start/Stop
};
```

### 6.3 实现详解

```cpp
TokenCleanupTask::TokenCleanupTask(
    std::shared_ptr<TokenRepository> token_repo, 
    int interval_minutes
)
    : token_repo_(std::move(token_repo))
    , interval_(interval_minutes)
    , running_(false) 
{}

TokenCleanupTask::~TokenCleanupTask() {
    Stop();  // 确保析构时停止线程
}

void TokenCleanupTask::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 防止重复启动
    if (running_.load()) {
        LOG_INFO("TokenCleanupTask already running");
        return;
    }
    
    // 等待之前的线程结束
    if (thread_.joinable()) {
        thread_.join();
    }
    
    running_.store(true);
    
    thread_ = std::thread([this] {
        LOG_INFO("TokenCleanupTask started, interval = {} minutes", interval_);
        
        while (running_.load()) {
            // 执行清理
            try {
                auto result = token_repo_->CleanExpiredTokens();
                if (result.Success()) {
                    LOG_INFO("Token cleanup: removed {} expired tokens", *result.data);
                } else {
                    LOG_ERROR("Token cleanup failed: {}", result.message);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Token cleanup exception: {}", e.what());
            }
            
            // 分段睡眠（便于快速响应停止请求）
            for (int i = 0; i < interval_ * 60 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        LOG_INFO("TokenCleanupTask stopped");
    });
}

void TokenCleanupTask::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    running_.store(false);
    
    if (thread_.joinable()) {
        thread_.join();
    }
}
```

### 6.4 设计要点

```
┌─────────────────────────────────────────────────────────────────────────┐
│                   TokenCleanupTask 设计要点                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. 分段睡眠                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  // ❌ 不好：长时间睡眠，Stop() 需要等待很久                      │   │
│  │  std::this_thread::sleep_for(std::chrono::minutes(60));          │   │
│  │                                                                  │   │
│  │  // ✓ 好：分段睡眠，每秒检查一次 running_ 标志                   │   │
│  │  for (int i = 0; i < 60 * 60 && running_.load(); ++i) {          │   │
│  │      std::this_thread::sleep_for(std::chrono::seconds(1));       │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  2. 原子变量 + 互斥锁                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  std::atomic<bool> running_;  // 后台线程读取，无锁              │   │
│  │  std::mutex mutex_;           // 保护 Start/Stop 的并发调用      │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  3. 析构时自动停止                                                      │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  ~TokenCleanupTask() {                                           │   │
│  │      Stop();  // RAII 模式，确保资源正确释放                     │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  4. 异常处理                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  try {                                                           │   │
│  │      token_repo_->CleanExpiredTokens();                          │   │
│  │  } catch (...) {                                                 │   │
│  │      LOG_ERROR(...);  // 记录错误但不崩溃，继续下次清理          │   │
│  │  }                                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.5 使用示例

```cpp
// 创建清理任务（每 60 分钟清理一次）
auto cleanup_task = std::make_shared<TokenCleanupTask>(token_repo, 60);

// 启动
cleanup_task->Start();

// ... 服务运行中 ...

// 服务关闭时停止（或依赖析构函数自动停止）
cleanup_task->Stop();
```

---

## 7. 双 Token 机制实现

### 7.1 机制概述

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        双 Token 机制                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Access Token                                                    │   │
│  │                                                                  │   │
│  │  用途：调用业务 API 时携带（Authorization Header）               │   │
│  │  有效期：15 分钟（短）                                           │   │
│  │  存储：仅客户端（内存/SessionStorage）                           │   │
│  │  验证：无状态，仅校验签名和过期时间                              │   │
│  │  泄露影响：有限（15分钟内有效）                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  Refresh Token                                                   │   │
│  │                                                                  │   │
│  │  用途：刷新 Access Token                                         │   │
│  │  有效期：7 天（长）                                              │   │
│  │  存储：客户端 + 服务端数据库（哈希存储）                         │   │
│  │  验证：有状态，需查询数据库确认有效                              │   │
│  │  泄露影响：可通过服务端主动失效                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  为什么需要双 Token？                                                   │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  单 Token 问题：                                                 │   │
│  │  • 有效期短 → 频繁登录，体验差                                   │   │
│  │  • 有效期长 → 泄露风险大，无法主动失效                           │   │
│  │                                                                  │   │
│  │  双 Token 解决：                                                 │   │
│  │  • Access Token 短期，降低泄露影响                               │   │
│  │  • Refresh Token 长期，保证用户体验                              │   │
│  │  • Refresh Token 服务端存储，可随时失效（改密码/登出）           │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 7.2 完整流程图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      双 Token 完整流程                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────┐                               ┌─────────────────────────┐  │
│  │ Client  │                               │        Server           │  │
│  └────┬────┘                               └───────────┬─────────────┘  │
│       │                                                │                │
│       │  1. Login(mobile, password)                    │                │
│       │───────────────────────────────────────────────►│                │
│       │                                                │                │
│       │                                    ┌───────────┴───────────┐   │
│       │                                    │ 验证密码               │   │
│       │                                    │ 生成 AccessToken       │   │
│       │                                    │ 生成 RefreshToken      │   │
│       │                                    │ 存储 Hash(Refresh)     │   │
│       │                                    └───────────┬───────────┘   │
│       │                                                │                │
│       │  2. Response: { access_token, refresh_token }  │                │
│       │◄───────────────────────────────────────────────│                │
│       │                                                │                │
│  ┌────┴────────────────────────────────────────────────┴────────────┐  │
│  │  客户端存储:                                                      │  │
│  │  • access_token  → 内存（或 sessionStorage）                     │  │
│  │  • refresh_token → 安全存储（HttpOnly Cookie / Keychain）        │  │
│  └────┬────────────────────────────────────────────────┬────────────┘  │
│       │                                                │                │
│       │  3. GetUser() + Header: Bearer {access_token}  │                │
│       │───────────────────────────────────────────────►│                │
│       │                                                │                │
│       │                                    ┌───────────┴───────────┐   │
│       │                                    │ 验证 AccessToken      │   │
│       │                                    │ （无状态，不查库）    │   │
│       │                                    └───────────┬───────────┘   │
│       │                                                │                │
│       │  4. Response: { user_info }                    │                │
│       │◄───────────────────────────────────────────────│                │
│       │                                                │                │
│       │        ... 15分钟后 AccessToken 过期 ...        │                │
│       │                                                │                │
│       │  5. GetUser() + Header: Bearer {access_token}  │                │
│       │───────────────────────────────────────────────►│                │
│       │                                                │                │
│       │  6. Response: { error: TOKEN_EXPIRED }         │                │
│       │◄───────────────────────────────────────────────│                │
│       │                                                │                │
│       │  7. RefreshToken(refresh_token)                │                │
│       │───────────────────────────────────────────────►│                │
│       │                                                │                │
│       │                                    ┌───────────┴───────────┐   │
│       │                                    │ 1. 解析 RefreshToken  │   │
│       │                                    │ 2. 查库验证 Hash 存在 │   │
│       │                                    │ 3. 删除旧 Hash        │   │
│       │                                    │ 4. 生成新 Token 对    │   │
│       │                                    │ 5. 存储新 Hash        │   │
│       │                                    └───────────┬───────────┘   │
│       │                                                │                │
│       │  8. Response: { new_access, new_refresh }      │                │
│       │◄───────────────────────────────────────────────│                │
│       │                                                │                │
│       │  9. 继续使用新 Token 调用 API                   │                │
│       │                                                │                │
└───────┴────────────────────────────────────────────────┴────────────────┘
```

### 7.3 代码实现

#### 7.3.1 登录时生成 Token 对

```cpp
// AuthService::LoginByPassword
Result<AuthResult> AuthService::LoginByPassword(
    const std::string& mobile, 
    const std::string& password
) {
    // ... 验证密码 ...
    
    // 生成 Token 对
    auto token_pair = jwt_srv_->GenerateTokenPair(user);
    
    // 存储 Refresh Token 哈希
    auto store_result = StoreRefreshToken(user.id, token_pair.refresh_token);
    
    AuthResult result;
    result.user = user;
    result.tokens = token_pair;
    return Result<AuthResult>::Ok(result);
}

Result<void> AuthService::StoreRefreshToken(
    int64_t user_id, 
    const std::string& refresh_token
) {
    // 哈希存储，不存明文
    auto token_hash = jwt_srv_->HashToken(refresh_token);
    return token_repo_->SaveRefreshToken(
        user_id, 
        token_hash, 
        config_->security.refresh_token_ttl_seconds
    );
}
```

#### 7.3.2 刷新 Token

```cpp
Result<TokenPair> AuthService::RefreshToken(const std::string& refresh_token) {
    // 1. 解析 Refresh Token，获取 user_id
    auto verify_res = jwt_srv_->ParseRefreshToken(refresh_token);
    if (!verify_res.IsOk()) {
        return Result<TokenPair>::Fail(verify_res.code, verify_res.message);
    }
    
    int64_t user_id = std::stoll(verify_res.Value());
    
    // 2. 查询用户状态
    auto user_res = user_db_->FindById(user_id);
    if (!user_res.IsOk()) {
        return Result<TokenPair>::Fail(user_res.code, user_res.message);
    }
    if (user_res.Value().disabled) {
        return Result<TokenPair>::Fail(ErrorCode::UserDisabled, "账号已被禁用");
    }
    
    // 3. 验证 Token 哈希是否存在于数据库
    std::string token_hash = jwt_srv_->HashToken(refresh_token);
    auto exists_res = token_repo_->IsTokenValid(token_hash);
    if (!exists_res.IsOk() || !exists_res.Value()) {
        return Result<TokenPair>::Fail(ErrorCode::TokenRevoked, "Token 已失效");
    }
    
    // 4. 删除旧 Token
    token_repo_->DeleteByTokenHash(token_hash);
    
    // 5. 生成新 Token 对
    auto& user = user_res.Value();
    auto new_tokens = jwt_srv_->GenerateTokenPair(user);
    
    // 6. 存储新 Refresh Token
    StoreRefreshToken(user.id, new_tokens.refresh_token);
    
    return Result<TokenPair>::Ok(new_tokens);
}
```

#### 7.3.3 登出

```cpp
Result<void> AuthService::Logout(const std::string& refresh_token) {
    // 幂等性：空 Token 视为成功
    if (refresh_token.empty()) {
        return Result<void>::Ok();
    }
    
    // 删除数据库中的 Token 记录
    std::string token_hash = jwt_srv_->HashToken(refresh_token);
    token_repo_->DeleteByTokenHash(token_hash);
    
    return Result<void>::Ok();
}

// 全设备登出
Result<void> AuthService::LogoutAll(const std::string& user_uuid) {
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<void>::Fail(user_res.code, user_res.message);
    }
    
    // 删除用户所有 Token
    token_repo_->DeleteByUserId(user_res.Value().id);
    
    return Result<void>::Ok();
}
```

### 7.4 安全增强：修改密码后强制登出

```cpp
Result<void> AuthService::ResetPassword(
    const std::string& mobile,
    const std::string& verify_code,
    const std::string& new_password
) {
    // ... 验证验证码 ...
    // ... 更新密码 ...
    
    // 关键：使所有 Refresh Token 失效
    // 用户修改密码后，所有设备需要重新登录
    auto revoke_res = token_repo_->DeleteByUserId(user.id);
    
    return Result<void>::Ok();
}
```

---

## 8. Authenticator 接口设计

### 8.1 接口定义

```cpp
// 认证上下文（从 Token 解析的用户信息）
struct AuthContext {
    int64_t user_id;        // 数据库 ID
    std::string user_uuid;  // UUID
    std::string mobile;     // 手机号
    UserRole role;          // 用户角色
};

// 认证器抽象接口
class Authenticator {
public:
    virtual ~Authenticator() = default;
    
    // 从 gRPC Context 中认证用户
    virtual Result<AuthContext> Authenticate(grpc::ServerContext* context) = 0;
};
```

### 8.2 JwtAuthenticator 实现

```cpp
class JwtAuthenticator : public Authenticator {
public:
    explicit JwtAuthenticator(std::shared_ptr<JwtService> jwt_service)
        : jwt_service_(std::move(jwt_service)) {}

    Result<AuthContext> Authenticate(grpc::ServerContext* context) override {
        // 1. 从 metadata 获取 Authorization header
        auto metadata = context->client_metadata();
        auto it = metadata.find("authorization");
        
        if (it == metadata.end()) {
            return Result<AuthContext>::Fail(
                ErrorCode::Unauthenticated, "缺少认证信息"
            );
        }
        
        std::string auth_header(it->second.data(), it->second.size());
        
        // 2. 解析 "Bearer {token}" 格式
        const std::string prefix = "Bearer ";
        if (auth_header.rfind(prefix, 0) != 0) {
            return Result<AuthContext>::Fail(
                ErrorCode::Unauthenticated, "认证格式错误"
            );
        }
        
        std::string token = auth_header.substr(prefix.length());
        
        // 3. 验证 Token
        auto verify_result = jwt_service_->VerifyAccessToken(token);
        if (!verify_result.IsOk()) {
            return Result<AuthContext>::Fail(
                verify_result.code, verify_result.message
            );
        }
        
        // 4. 构造 AuthContext
        const auto& payload = verify_result.Value();
        AuthContext auth_ctx;
        auth_ctx.user_id = payload.user_id;
        auth_ctx.user_uuid = payload.user_uuid;
        auth_ctx.mobile = payload.mobile;
        auth_ctx.role = payload.role;
        
        return Result<AuthContext>::Ok(auth_ctx);
    }

private:
    std::shared_ptr<JwtService> jwt_service_;
};
```

### 8.3 在 Handler 中使用

```cpp
class UserHandler : public pb_user::UserService::Service {
public:
    UserHandler(std::shared_ptr<UserService> user_service,
                std::shared_ptr<Authenticator> authenticator)
        : user_service_(user_service), authenticator_(authenticator) {}

    grpc::Status GetCurrentUser(
        grpc::ServerContext* context,
        const pb_user::GetCurrentUserRequest* request,
        pb_user::GetCurrentUserResponse* response
    ) override {
        // 认证
        auto auth_result = authenticator_->Authenticate(context);
        if (!auth_result.IsOk()) {
            SetResultError(response->mutable_result(), 
                           auth_result.code, auth_result.message);
            return grpc::Status::OK;
        }
        
        // 使用认证信息
        auto& auth_ctx = auth_result.Value();
        auto user_result = user_service_->GetCurrentUser(auth_ctx.user_uuid);
        
        // ... 填充响应 ...
    }
    
private:
    std::shared_ptr<UserService> user_service_;
    std::shared_ptr<Authenticator> authenticator_;
};
```

---

## 9. 配置说明

### 9.1 SecurityConfig

```yaml
security:
  jwt_secret: "your-super-secret-key-at-least-32-bytes!"
  jwt_issuer: "user-service"
  access_token_ttl_seconds: 900       # 15 分钟
  refresh_token_ttl_seconds: 604800   # 7 天
```

### 9.2 SmsConfig

```yaml
sms:
  code_len: 6                    # 验证码长度
  code_ttl_seconds: 300          # 有效期 5 分钟
  send_interval_seconds: 60      # 发送间隔 60 秒
  max_retry_count: 5             # 最大错误次数
  retry_ttl_seconds: 300         # 错误计数窗口 5 分钟
  lock_seconds: 1800             # 锁定 30 分钟
```

---

## 10. 相关文件

| 文件 | 说明 |
|------|------|
| `include/auth/jwt_service.h` | JWT 服务接口 |
| `include/auth/sms_service.h` | 短信服务接口 |
| `include/auth/token_repository.h` | Token 仓库接口 |
| `include/auth/token_cleanup_task.h` | 清理任务接口 |
| `include/auth/authenticator.h` | 认证器抽象接口 |
| `include/auth/jwt_authenticator.h` | JWT 认证器实现 |
| `src/auth/*.cpp` | 实现文件 |
| `include/common/auth_type.h` | 认证相关类型定义 |
| `include/config/config.h` | 配置结构体 |

