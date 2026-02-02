#pragma once

#include <memory>
#include <string>

#include "config/config.h"
#include "common/result.h"
#include "db/user_db.h"
#include "auth/token_repository.h"
#include  "auth/sms_service.h"

namespace user_service{


struct ListUsersResult {
    std::vector<UserEntity> users;
    PageResult page_res;
};

/// @brief 用户操作类
class UserService {
public:
    UserService(std::shared_ptr<Config> config,
                std::shared_ptr<UserDB> user_db,
                std::shared_ptr<TokenRepository> token_repo,
                std::shared_ptr<SmsService> sms_srv);

    // ==================== 当前用户操作 ====================
    
    /// @brief 获取用户信息，
    /// @param user_uuid 由上层调用者从Access Token获取，并传入
    Result<UserEntity> GetCurrentUser(const std::string& user_uuid);
    
    /// @brief 更新用户信息（可选更新：后续扩展：avatar_url, email 等）
    /// @param user_uuid 由上层调用者从Access Token获取，并传入
    Result<UserEntity> UpdateUser(const std::string& user_uuid,
                                    std::optional<std::string> display_name);
    


    /// @brief 修改密码（已登录，需要旧密码）
    /// @param user_uuid 由上层调用者从Access Token获取，并传入
    Result<void> ChangePassword(const std::string& user_uuid,
                                const std::string& old_password,
                                const std::string& new_password);
    

    /// @brief 删除用户（注销账号）
    /// @param user_uuid 由上层调用者从Access Token获取，并传入
    Result<void> DeleteUser(const std::string& user_uuid,
                            const std::string verify_code,
                            const std::string& mobile);

    // ==================== 管理员操作 ====================
    


    /// @brief 获取指定用户
    /// @param user_uuid 由上层调用者从Access Token获取，并传入
    Result<UserEntity> GetUser(const std::string& user_uuid);

    /// @brief 获取用户列表（可按条件筛选）
    /// @param mobile_filter “电话号码”筛选条件
    /// @param disabled_filter “是否禁用”筛选条件
    /// @param page 返回的页数
    /// @param page_size 每页的大小
    /// @return 返回用户列表
    Result<ListUsersResult> ListUsers(std::optional<std::string> mobile_filter,
                                        std::optional<bool> disabled_filter,
                                        int32_t page=1,
                                        int32_t page_size=20);

    /// @brief 禁用/启用用户
    /// @param user_uuid 由上层调用者从Access Token获取，并传入
    /// @param disabled 禁用/启用
    Result<void> SetUserDisabled(const std::string& user_uuid, bool disabled);

private:
    std::shared_ptr<Config> config_;                // 配置句柄
    std::shared_ptr<UserDB> user_db_;               // 用户数据库句柄
    std::shared_ptr<TokenRepository> token_repo_;   // token数据库句柄
    std::shared_ptr<SmsService> sms_srv_;           // 验证码服务句柄
};


}