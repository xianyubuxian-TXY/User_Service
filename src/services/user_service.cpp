#include "user_service.h"

#include "common/logger.h"
#include "common/validator.h"
#include "common/password_helper.h"
#include "common/error_codes.h"

namespace user_service{

// ============================================================================
// 构造函数
// ============================================================================

UserService::UserService(std::shared_ptr<Config> config,
                            std::shared_ptr<UserDB> user_db,
                            std::shared_ptr<TokenRepository> token_repo,
                            std::shared_ptr<SmsService> sms_srv)
    : config_(std::move(config)),
    user_db_(std::move(user_db)),
    token_repo_(std::move(token_repo)),
    sms_srv_(std::move(sms_srv))
{}

// 获取当前用户
Result<UserEntity> UserService::GetCurrentUser(const std::string& user_uuid){
    // 1. 参数校验
    if (user_uuid.empty()) {
        return Result<UserEntity>::Fail(ErrorCode::InvalidArgument, "用户ID不能为空");
    }

    // 2. 查询用户
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<UserEntity>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 3. 清除敏感信息
    user.password_hash.clear();

    return Result<UserEntity>::Ok(user);
}

// 更新用户信息（可选更新：后续扩展：avatar_url, email 等）
Result<UserEntity> UserService::UpdateUser(const std::string& user_uuid,
                                            std::optional<std::string> display_name){
    // 1.参数校验
    if (user_uuid.empty()) {
        return Result<UserEntity>::Fail(ErrorCode::InvalidArgument, "用户ID不能为空");
    }

    // 2.查询用户
    auto user_res=user_db_->FindByUUID(user_uuid);
    if(!user_res.IsOk()){
        return Result<UserEntity>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 3.检查账户状态
    if(user.disabled){
        return Result<UserEntity>::Fail(ErrorCode::UserDisabled, "账号已被禁用");
    }

    // 4.更新字段
    bool has_update=false;
    if(display_name.has_value()){
        std::string error;
        // 检查名称是否符合格式
        if(!IsValidDisplayName(display_name.value(),error)){
            return Result<UserEntity>::Fail(ErrorCode::InvalidArgument, error);
        }
        user.display_name=display_name.value();
        has_update=true;
    }

    // 后续扩展：avatar_url, email 等
    // if (req.avatar_url.has_value()) { ... }

    // 5.没有需要更新的字段
    if(!has_update){
        user.password_hash.clear(); // 清除敏感字段
        return Result<UserEntity>::Ok(user);
    }

    // 6.保留更新
    auto update_res = user_db_->Update(user);
    if (!update_res.IsOk()) {
        return Result<UserEntity>::Fail(update_res.code, update_res.message);
    }

    // 7.返回更新后的用户
    user.password_hash.clear(); // 清除敏感字段

    LOG_INFO("User updated: uuid={}", user_uuid);
    return Result<UserEntity>::Ok(user);
}

// 修改密码（已登录，需要旧密码）
Result<void> UserService::ChangePassword(const std::string& user_uuid,
                                            const std::string& old_password,
                                            const std::string& new_password){
                                            std::string error;

    // 1. 参数校验
    if (user_uuid.empty()) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, "用户ID不能为空");
    }

    if (!IsValidPassword(old_password, error, config_->password)) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, "旧密码格式错误");
    }

    if (!IsValidPassword(new_password, error, config_->password)) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, error);
    }

    // 旧密码不可以与新密码一致
    if (old_password == new_password) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, "新密码不能与旧密码相同");
    }

    // 2. 查询用户
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<void>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 3. 检查账号状态
    if (user.disabled) {
        return Result<void>::Fail(ErrorCode::UserDisabled, "账号已被禁用");
    }

    // 4. 验证旧密码
    if (!PasswordHelper::Verify(old_password, user.password_hash)) {
        return Result<void>::Fail(ErrorCode::WrongPassword, "旧密码错误");
    }

    // 5. 更新密码
    // 获取密码哈希值，再存入
    user.password_hash = PasswordHelper::Hash(new_password);
    auto update_res = user_db_->Update(user);
    if (!update_res.IsOk()) {
        return Result<void>::Fail(update_res.code, update_res.message);
    }

    // 6. 可选：使其他设备的 Token 失效（当前设备保持登录）
    // 这里暂不实现，如需要可以传入当前 token_hash 排除
    // token_repo_->DeleteByUserIdExcept(user.id, current_token_hash);

    LOG_INFO("User changed password: uuid={}", user_uuid);
    return Result<void>::Ok();
}

// 删除用户（注销账号）
Result<void> UserService::DeleteUser(const std::string& user_uuid,
                                        const std::string verify_code,
                                        const std::string& mobile){
                                            std::string error;

    // 1. 参数校验
    if (user_uuid.empty()) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, "用户ID不能为空");
    }

    if (!IsValidVerifyCode(verify_code, error, config_->sms)) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, error);
    }

    // 2. 查询用户
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<void>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 3. 验证验证码
    auto verify_res = sms_srv_->VerifyCaptcha(SmsScene::DeleteUser, user.mobile,verify_code);
    if (!verify_res.IsOk()) {
        return Result<void>::Fail(verify_res.code, verify_res.message);
    }

    // 4. 删除用户所有 Token（强制登出）
    auto revoke_res = token_repo_->DeleteByUserId(user.id);
    if (!revoke_res.IsOk()) {
        LOG_WARN("Revoke tokens failed for user {}: {}", user.uuid, revoke_res.message);
    }

    // 5. 删除用户（软删除或硬删除，根据业务需求）
    // 方案A：软删除（推荐，保留数据用于审计）
    user.disabled = true;
    user.mobile = "deleted_" + std::to_string(user.id) + "_" + user.mobile;  // 释放手机号
    auto update_res = user_db_->Update(user);
    
    // 方案B：硬删除
    // auto delete_res = user_db_->Delete(user.id);

    if (!update_res.IsOk()) {
        return Result<void>::Fail(update_res.code, update_res.message);
    }

    LOG_INFO("User deleted (soft): uuid={}, mobile={}", user_uuid, user.mobile);
    return Result<void>::Ok();
}

// ==================== 管理员操作 ====================

// 获取指定用户
Result<UserEntity> UserService::GetUser(const std::string& user_uuid){
    // 1. 参数校验
    if (user_uuid.empty()) {
        return Result<UserEntity>::Fail(ErrorCode::InvalidArgument, "用户ID不能为空");
    }

    // 2. 查询用户
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<UserEntity>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 3. 清除敏感信息
    user.password_hash.clear();

    return Result<UserEntity>::Ok(user);
}

// 获取用户列表（可按条件筛选）
Result<ListUsersResult> UserService::ListUsers(std::optional<std::string> mobile_filter,
                                                std::optional<bool> disabled_filter,
                                                int32_t page,
                                                int32_t page_size){
    // 1. 参数校验和默认值
    page = page > 0 ? page : 1;
    page_size = page_size > 0 ? page_size : 20;
    
    // 限制最大分页大小
    if (page_size > 100) {
        page_size = 100;
    }
    // 2. 构建查询条件
    UserQueryParams params;
    params.page_params.page = page;
    params.page_params.page_size = page_size;
    
    if (mobile_filter.has_value() && !mobile_filter.value().empty()) {
        params.mobile_like = mobile_filter.value();
    }
    
    if (disabled_filter.has_value()) {
        params.disabled = disabled_filter;
    }

    // 3. 查询总数
    auto count_res = user_db_->Count(params);
    if (!count_res.IsOk()) {
        return Result<ListUsersResult>::Fail(count_res.code, count_res.message);
    }
    int64_t total_records = count_res.data.value();

    // 4. 查询列表
    auto list_res = user_db_->FindAll(params);
    if (!list_res.IsOk()) {
        return Result<ListUsersResult>::Fail(list_res.code, list_res.message);
    }

    auto& users = list_res.data.value();

    // 5. 清除敏感信息
    for (auto& user : users) {
        user.password_hash.clear();
    }

    // 6. 构建分页信息
    ListUsersResult result;
    result.users = std::move(users);
    result.page_res.total_records = total_records;
    result.page_res.total_pages = (total_records + page_size - 1) / page_size;
    result.page_res.page = page;
    result.page_res.page_size = page_size;

    return Result<ListUsersResult>::Ok(result);
}

// 禁用/启用用户
Result<void> UserService::SetUserDisabled(const std::string& user_uuid, bool disabled){
    // 1. 参数校验
    if (user_uuid.empty()) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, "用户ID不能为空");
    }

    // 2. 查询用户
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<void>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 3. 状态相同，无需更新
    if (user.disabled == disabled) {
        return Result<void>::Ok();
    }

    // 4. 更新状态
    user.disabled = disabled;
    auto update_res = user_db_->Update(user);
    if (!update_res.IsOk()) {
        return Result<void>::Fail(update_res.code, update_res.message);
    }

    // 5. 如果是禁用，使所有 Token 失效
    if (disabled) {
        auto revoke_res = token_repo_->DeleteByUserId(user.id);
        if (!revoke_res.IsOk()) {
            LOG_WARN("Revoke tokens failed for disabled user {}: {}", user.uuid, revoke_res.message);
        }
    }

    LOG_INFO("User {} : uuid={}", disabled ? "disabled" : "enabled", user_uuid);
    return Result<void>::Ok();
}


}