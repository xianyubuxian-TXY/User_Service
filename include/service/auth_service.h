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


    // 发送验证码
    Result<int32_t> SendVerifyCode(const std::string& mobile,
                                    SmsScene scene);

    // 注册
    Result<AuthResult> Register(const std::string& mobile,
                                const std::string& verify_code,
                                const std::string& password,
                                const std::string& display_name);

    // 登录
    Result<AuthResult> LoginByPassword(const std::string& mobile,
                                        const std::string& password);

    Result<AuthResult> LoginByCode(const std::string& mobile,
                                    const std::string& verify_code);

    // 重置密码
    Result<void> ResetPassword(const std::string& mobile,
                                const std::string& verify_code,
                                const std::string& new_password);
    
    // Token 管理
    Result<TokenPair> RefreshToken(const std::string& refresh_token);
    Result<void> Logout(const std::string& refresh_token);
    Result<void> LogoutAll(const std::string& user_uuid);

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
