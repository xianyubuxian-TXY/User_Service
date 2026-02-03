#include "handlers/user_handler.h"
#include "common/logger.h"
#include "common/validator.h"
#include "common/proto_converter.h"
#include "common/error_codes.h"
#include "common/auth_type.h"
namespace user_service{

namespace {

// 检查是否是管理员
inline Result<void> RequireAdmin(const AuthContext& auth) {
    if (auth.role != UserRole::Admin && auth.role != UserRole::SuperAdmin) {
        return Result<void>::Fail(ErrorCode::AdminRequired, "需要管理员权限");
    }
    return Result<void>::Ok();
}

}  // namespace

// ============================================================================
// 构造函数
// ============================================================================

UserHandler::UserHandler(std::shared_ptr<UserService> user_service,
    std::shared_ptr<Authenticator> authenticator)
: user_service_(std::move(user_service)),
authenticator_(std::move(authenticator)) {  // 保存 authenticator
}

// ============================================================================
// 获取当前用户
// ============================================================================

::grpc::Status UserHandler::GetCurrentUser(
    ::grpc::ServerContext* context,
    const ::pb_user::GetCurrentUserRequest* request,
    ::pb_user::GetCurrentUserResponse* response) {
    
    LOG_DEBUG("GetCurrentUser requested");

    // 1. 认证（从 Token 解析用户信息）
    auto auth_ctx = authenticator_->Authenticate(context);
    if (!auth_ctx.IsOk()) {
        SetResultError(response->mutable_result(), auth_ctx.code, auth_ctx.message);
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = user_service_->GetCurrentUser(auth_ctx.Value().user_uuid);

    // 3. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        ToProtoUser(result.Value(), response->mutable_user());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 更新用户信息
// ============================================================================

::grpc::Status UserHandler::UpdateUser(
    ::grpc::ServerContext* context,
    const ::pb_user::UpdateUserRequest* request,
    ::pb_user::UpdateUserResponse* response) {
    
    LOG_DEBUG("UpdateUser requested");

    // 1. 认证
    auto auth_ctx = authenticator_->Authenticate(context);
    if (!auth_ctx.IsOk()) {
        SetResultError(response->mutable_result(), auth_ctx.code, auth_ctx.message);
        return ::grpc::Status::OK;
    }

    // 2. 提取可选字段
    std::optional<std::string> display_name;
    if (request->has_display_name()) {
        display_name = request->display_name().value();
    }

    // 3. 调用业务逻辑
    auto result = user_service_->UpdateUser(auth_ctx.Value().user_uuid, display_name);

    // 4. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        ToProtoUser(result.Value(), response->mutable_user());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 修改密码
// ============================================================================

::grpc::Status UserHandler::ChangePassword(
    ::grpc::ServerContext* context,
    const ::pb_user::ChangePasswordRequest* request,
    ::pb_user::ChangePasswordResponse* response) {
    
    LOG_INFO("ChangePassword requested");

    // 1. 认证
    auto auth_ctx = authenticator_->Authenticate(context);
    if (!auth_ctx.IsOk()) {
        SetResultError(response->mutable_result(), auth_ctx.code, auth_ctx.message);
        return ::grpc::Status::OK;
    }

    // 2. 参数校验
    if (request->old_password().empty()) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, "旧密码不能为空");
        return ::grpc::Status::OK;
    }
    if (request->new_password().empty()) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, "新密码不能为空");
        return ::grpc::Status::OK;
    }

    std::string error;
    if (!IsValidPassword(request->new_password(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;
    }

    // 3. 调用业务逻辑
    auto result = user_service_->ChangePassword(
        auth_ctx.Value().user_uuid,
        request->old_password(),
        request->new_password()
    );

    // 4. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    return ::grpc::Status::OK;
}

// ============================================================================
// 删除用户（注销账号）
// ============================================================================

::grpc::Status UserHandler::DeleteUser(
    ::grpc::ServerContext* context,
    const ::pb_user::DeleteUserRequest* request,
    ::pb_user::DeleteUserResponse* response) {
    
    LOG_INFO("DeleteUser requested");

    // 1. 认证
    auto auth_ctx = authenticator_->Authenticate(context);
    if (!auth_ctx.IsOk()) {
        SetResultError(response->mutable_result(), auth_ctx.code, auth_ctx.message);
        return ::grpc::Status::OK;
    }

    // 2. 参数校验
    std::string error;
    if (!IsValidVerifyCode(request->verify_code(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;
    }

    // 3. 调用业务逻辑
    auto result = user_service_->DeleteUser(
        auth_ctx.Value().user_uuid,
        request->verify_code()
    );

    // 4. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    return ::grpc::Status::OK;
}

// ============================================================================
// 获取指定用户（管理员接口）
// ============================================================================

::grpc::Status UserHandler::GetUser(
    ::grpc::ServerContext* context,
    const ::pb_user::GetUserRequest* request,
    ::pb_user::GetUserResponse* response) {
    
    LOG_INFO("GetUser requested: id={}", request->id());

    // 1. 认证（管理员也需要登录）
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

    // 3. 参数校验
    if (request->id().empty()) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, "用户ID不能为空");
        return ::grpc::Status::OK;
    }

    // 4. 调用业务逻辑
    auto result = user_service_->GetUser(request->id());

    // 5. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        ToProtoUser(result.Value(), response->mutable_user());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 获取用户列表（管理员接口）
// ============================================================================

::grpc::Status UserHandler::ListUsers(
    ::grpc::ServerContext* context,
    const ::pb_user::ListUsersRequest* request,
    ::pb_user::ListUsersResponse* response) {
    
    LOG_INFO("ListUsers requested");

    // 1. 认证
    auto auth = authenticator_->Authenticate(context);
    if (!auth.IsOk()) {
        SetResultError(response->mutable_result(), auth.code, auth.message);
        return ::grpc::Status::OK;
    }

    // 2. 检查管理员权限
    auto admin_check = RequireAdmin(auth.Value());
    if (!admin_check.IsOk()) {
        SetResultError(response->mutable_result(), admin_check.code, admin_check.message);
        return ::grpc::Status::OK;
    }

    // 3. 提取参数
    int32_t page = request->has_page() ? request->page().page() : 1;
    int32_t page_size = request->has_page() ? request->page().page_size() : 20;

    std::optional<std::string> mobile_filter;
    if (!request->mobile_filter().empty()) {
        mobile_filter = request->mobile_filter();
    }

    std::optional<bool> disabled_filter;
    if (request->has_disabled_filter()) {
        disabled_filter = request->disabled_filter().value();
    }

    // 4. 调用业务逻辑
    auto result = user_service_->ListUsers(mobile_filter, disabled_filter, page, page_size);

    // 5. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        const auto& list_result = result.Value();
        
        for (const auto& user : list_result.users) {
            ToProtoUser(user, response->add_users());
        }
        
        auto* page_info = response->mutable_page_info();
        page_info->set_total_records(list_result.page_res.total_records);
        page_info->set_total_pages(list_result.page_res.total_pages);
        page_info->set_page(list_result.page_res.page);
        page_info->set_page_size(list_result.page_res.page_size);
    }

    return ::grpc::Status::OK;
}

}