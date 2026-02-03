#pragma once

#include <memory>
#include <string>
#include "common/result.h"
#include "entity/user_entity.h"
#include "db/user_db.h"
#include "auth/token_repository.h"
#include "auth/jwt_service.h"
#include "auth/sms_service.h"
#include "common/auth_type.h"
#include "cache/redis_client.h"
#include "config/config.h"

namespace user_service {

/// @brief 认证服务类
class AuthService{
public:
    AuthService(std::shared_ptr<Config> config,
                std::shared_ptr<UserDB> user_db,
                std::shared_ptr<RedisClient> redis_cli,
                std::shared_ptr<TokenRepository> token_repo,
                std::shared_ptr<JwtService> jwt_srv,
                std::shared_ptr<SmsService> sms_srv);

    virtual ~AuthService()=default;
    
    // 发送验证码
    virtual Result<int32_t> SendVerifyCode(const std::string& mobile,
                                    SmsScene scene);

    // 注册
    virtual Result<AuthResult> Register(const std::string& mobile,
                                const std::string& verify_code,
                                const std::string& password,
                                const std::string& display_name);

    // 登录
    virtual Result<AuthResult> LoginByPassword(const std::string& mobile,
                                        const std::string& password);

    virtual Result<AuthResult> LoginByCode(const std::string& mobile,
                                    const std::string& verify_code);

    // 重置密码
    virtual Result<void> ResetPassword(const std::string& mobile,
                                const std::string& verify_code,
                                const std::string& new_password);
    
    // Token 管理
    virtual Result<TokenPair> RefreshToken(const std::string& refresh_token);
    virtual Result<void> Logout(const std::string& refresh_token);
    virtual Result<void> LogoutAll(const std::string& user_uuid);

    /// @brief 验证 Access Token 有效性（供其他微服务调用）
    /// @param access_token 待验证的访问令牌
    /// @return TokenValidation 包含有效性、用户ID、过期时间
    virtual Result<TokenValidationResult> ValidateAccessToken(const std::string& access_token);

private:
    // 检查登录失败次数（超过最大次数将被锁定）
    Result<void> CheckLoginFailedAttempts(const std::string& mobile);

    // 记录登录失败
    void RecordLoginFailure(const std::string& mobile);

    // 清除登录失败记录
    void ClearLoginFailure(const std::string& mobile);

    // 存储Refress Token
    Result<void> StoreRefreshToken(int64_t user_id, const std::string& refresh_token);
    
    std::shared_ptr<Config> config_;

    std::shared_ptr<UserDB> user_db_;               // user数据库操作句柄
    std::shared_ptr<RedisClient> redis_cli_;        // redis缓存句柄 
    std::shared_ptr<TokenRepository> token_repo_;   // token数据库操作句柄
    std::shared_ptr<JwtService> jwt_srv_;           // token服务句柄
    std::shared_ptr<SmsService> sms_srv_;           // Captcha服务句柄


};

}  // namespace user_service
