#pragma once

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

#include "client/client_options.h"
#include "common/result.h"
#include "common/auth_type.h"
#include "entity/user_entity.h"
#include "pb_auth/auth.grpc.pb.h"

namespace user_service {

/**
 * @brief 认证服务客户端
 * 
 * 提供认证相关操作的封装，包括：
 * - 发送验证码
 * - 注册/登录
 * - Token 刷新
 * - 登出
 * 
 * @example
 * @code
 *   AuthClient client("localhost:50051");
 *   
 *   // 发送验证码
 *   auto result = client.SendVerifyCode("13800138000", SmsScene::Register);
 *   
 *   // 注册
 *   auto reg_result = client.Register("13800138000", "123456", "Password123");
 *   if (reg_result.IsOk()) {
 *       auto& auth = reg_result.Value();
 *       std::cout << "User ID: " << auth.user.uuid << std::endl;
 *       std::cout << "Access Token: " << auth.tokens.access_token << std::endl;
 *   }
 * @endcode
 */
class AuthClient {
public:
    /**
     * @brief 构造函数（简单版本）
     * @param target 服务地址，如 "localhost:50051"
     */
    explicit AuthClient(const std::string& target);

    /**
     * @brief 构造函数（完整配置）
     * @param options 客户端配置选项
     */
    explicit AuthClient(const ClientOptions& options);

    /**
     * @brief 构造函数（使用已有 Channel）
     * @param channel gRPC Channel
     */
    explicit AuthClient(std::shared_ptr<grpc::Channel> channel);

    virtual ~AuthClient() = default;

    // ==================== 验证码 ====================

    /**
     * @brief 发送验证码
     * @param mobile 手机号
     * @param scene 场景（注册/登录/重置密码）
     * @return 成功返回重试间隔（秒）
     */
    virtual Result<int32_t> SendVerifyCode(const std::string& mobile, SmsScene scene);

    // ==================== 注册 ====================

    /**
     * @brief 用户注册
     * @param mobile 手机号
     * @param verify_code 验证码
     * @param password 密码
     * @param display_name 昵称（可选）
     * @return 成功返回用户信息和 Token
     */
    virtual Result<AuthResult> Register(
        const std::string& mobile,
        const std::string& verify_code,
        const std::string& password,
        const std::string& display_name = ""
    );

    // ==================== 登录 ====================

    /**
     * @brief 密码登录
     */
    virtual Result<AuthResult> LoginByPassword(
        const std::string& mobile,
        const std::string& password
    );

    /**
     * @brief 验证码登录
     */
    virtual Result<AuthResult> LoginByCode(
        const std::string& mobile,
        const std::string& verify_code
    );

    // ==================== Token 管理 ====================

    /**
     * @brief 刷新 Token
     * @param refresh_token 刷新令牌
     * @return 成功返回新的 Token 对
     */
    virtual Result<TokenPair> RefreshToken(const std::string& refresh_token);

    /**
     * @brief 登出
     * @param refresh_token 刷新令牌
     */
    virtual Result<void> Logout(const std::string& refresh_token);

    // ==================== 密码管理 ====================

    /**
     * @brief 重置密码
     */
    virtual Result<void> ResetPassword(
        const std::string& mobile,
        const std::string& verify_code,
        const std::string& new_password
    );

    // ==================== Token 验证（内部服务用）====================

    /**
     * @brief 验证 Access Token
     */
    virtual Result<TokenValidationResult> ValidateToken(const std::string& access_token);

    // ==================== 配置 ====================

    /**
     * @brief 设置默认超时时间
     */
    void SetTimeout(std::chrono::milliseconds timeout) {
        timeout_ = timeout;
    }

protected:
    /**
     * @brief 创建带超时的 ClientContext
     */
    std::unique_ptr<grpc::ClientContext> CreateContext() const;

    /**
     * @brief 从 gRPC Status 和 Proto Result 创建 Result
     */
    template<typename T>
    Result<T> HandleResponse(
        const grpc::Status& status,
        const pb_common::Result& proto_result,
        std::function<T()> extractor
    );

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<pb_auth::AuthService::Stub> stub_;
    std::chrono::milliseconds timeout_{5000};
};

} // namespace user_service
