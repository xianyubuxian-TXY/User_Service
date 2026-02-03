#pragma once

#include <string>
#include <chrono>
#include "entity/user_entity.h"

namespace user_service {

// ============================================================================
// Token 相关结构体
// ============================================================================

/// @brief Token 令牌对（统一定义，JwtService 和 AuthService 共用）
struct TokenPair {
    std::string access_token;   ///< 访问令牌
    std::string refresh_token;  ///< 刷新令牌
    int64_t expires_in;         ///< access_token 有效期（秒）
};

/// @brief Access Token 解析后的载荷
struct AccessTokenPayload {
    int64_t user_id;            ///< 数据库用户ID
    std::string user_uuid;      ///< 用户 UUID（对外标识）
    std::string mobile;         ///< 手机号
    UserRole role;              ///< 用户身份
    std::chrono::system_clock::time_point expires_at;  ///< 过期时间
};

/// @brief Token 验证结果（ValidateToken 返回）
struct TokenValidationResult {
    int64_t user_id = 0;        ///< 数据库用户ID
    std::string user_uuid;      ///< 用户 UUID
    std::string mobile;         ///< 手机号
    UserRole role;              ///< 用户身份
    std::chrono::system_clock::time_point expires_at;  ///< 过期时间
};


// ============================================================================
// 认证业务相关结构体
// ============================================================================

/// @brief 认证结果（注册/登录成功后返回）
struct AuthResult {
    UserEntity user;    ///< 用户信息
    TokenPair tokens;   ///< Token 对
};

/// @brief 验证码场景
enum class SmsScene {
    UnKnow = 0,         // 未指定场景（默认值，用于参数校验，非法场景）
    Register = 1,       // 注册场景 - 新用户账号注册验证
    Login = 2,          // 登录场景 - 账号短信登录/验证登录
    ResetPassword = 3,  // 重置密码场景 - 忘记密码/账号密码重置
    DeleteUser =4       // 删除用户
};

} // namespace user_service
