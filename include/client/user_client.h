#pragma once

#include <memory>
#include <string>
#include <optional>
#include <grpcpp/grpcpp.h>

#include "client/client_options.h"
#include "common/result.h"
#include "entity/user_entity.h"
#include "entity/page.h"
#include "pb_user/user.grpc.pb.h"

namespace user_service {

/**
 * @brief 用户服务客户端
 * 
 * 提供用户管理操作的封装，所有接口都需要认证（Access Token）
 * 
 * @example
 * @code
 *   UserClient client("localhost:50051");
 *   client.SetAccessToken("eyJhbGciOi...");
 *   
 *   auto result = client.GetCurrentUser();
 *   if (result.IsOk()) {
 *       std::cout << "Hello, " << result.Value().display_name << std::endl;
 *   }
 * @endcode
 */
class UserClient {
public:
    /**
     * @brief 构造函数（简单版本）
     */
    explicit UserClient(const std::string& target);

    /**
     * @brief 构造函数（完整配置）
     */
    explicit UserClient(const ClientOptions& options);

    /**
     * @brief 构造函数（使用已有 Channel）
     */
    explicit UserClient(std::shared_ptr<grpc::Channel> channel);

    virtual ~UserClient() = default;

    // ==================== 当前用户操作 ====================

    /**
     * @brief 获取当前用户信息
     */
    virtual Result<UserEntity> GetCurrentUser();

    /**
     * @brief 更新用户信息
     * @param display_name 新昵称（可选）
     */
    virtual Result<UserEntity> UpdateUser(std::optional<std::string> display_name);

    /**
     * @brief 修改密码
     */
    virtual Result<void> ChangePassword(
        const std::string& old_password,
        const std::string& new_password
    );

    /**
     * @brief 注销账号
     * @param verify_code 验证码（确认身份）
     */
    virtual Result<void> DeleteUser(const std::string& verify_code);

    // ==================== 管理员操作 ====================

    /**
     * @brief 获取指定用户（需要管理员权限）
     */
    virtual Result<UserEntity> GetUser(const std::string& user_id);

    /**
     * @brief 获取用户列表（需要管理员权限）
     */
    virtual Result<std::pair<std::vector<UserEntity>, PageResult>> ListUsers(
        std::optional<std::string> mobile_filter = std::nullopt,
        std::optional<bool> disabled_filter = std::nullopt,
        int32_t page = 1,
        int32_t page_size = 20
    );

    // ==================== 认证配置 ====================

    /**
     * @brief 设置 Access Token（必须调用，否则请求会被拒绝）
     */
    void SetAccessToken(const std::string& token) {
        access_token_ = token;
    }

    /**
     * @brief 设置超时时间
     */
    void SetTimeout(std::chrono::milliseconds timeout) {
        timeout_ = timeout;
    }

protected:
    /**
     * @brief 创建带认证信息的 ClientContext
     */
    std::unique_ptr<grpc::ClientContext> CreateContext() const;

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<pb_user::UserService::Stub> stub_;
    std::string access_token_;
    std::chrono::milliseconds timeout_{5000};
};

} // namespace user_service
