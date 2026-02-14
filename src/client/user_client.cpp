#include "client/user_client.h"
#include "common/proto_converter.h"
#include "common/logger.h"

namespace user_service {

// ============================================================================
// 构造函数
// ============================================================================

UserClient::UserClient(const std::string& target)
    : channel_(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()))
    , stub_(pb_user::UserService::NewStub(channel_)) {
}

UserClient::UserClient(const ClientOptions& options)
    : channel_(grpc::CreateCustomChannel(
          options.target,
          options.CreateCredentials(),
          options.CreateChannelArgs()))
    , stub_(pb_user::UserService::NewStub(channel_))
    , timeout_(options.timeout)
    , access_token_(options.access_token) {
}

UserClient::UserClient(std::shared_ptr<grpc::Channel> channel)
    : channel_(std::move(channel))
    , stub_(pb_user::UserService::NewStub(channel_)) {
}

// ============================================================================
// 辅助方法
// ============================================================================

std::unique_ptr<grpc::ClientContext> UserClient::CreateContext() const {
    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(std::chrono::system_clock::now() + timeout_);
    
    // 添加认证信息
    if (!access_token_.empty()) {
        context->AddMetadata("authorization", "Bearer " + access_token_);
    }
    
    return context;
}

// ============================================================================
// 当前用户操作
// ============================================================================

Result<UserEntity> UserClient::GetCurrentUser() {
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->GetCurrentUser(context.get(), request, &response);

    if (!status.ok()) {
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<UserEntity>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<UserEntity>::Ok(FromProtoUser(response.user()));
}

Result<UserEntity> UserClient::UpdateUser(std::optional<std::string> display_name) {
    pb_user::UpdateUserRequest request;
    
    if (display_name.has_value()) {
        request.mutable_display_name()->set_value(display_name.value());
    }

    pb_user::UpdateUserResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->UpdateUser(context.get(), request, &response);

    if (!status.ok()) {
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<UserEntity>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<UserEntity>::Ok(FromProtoUser(response.user()));
}

Result<void> UserClient::ChangePassword(
    const std::string& old_password,
    const std::string& new_password) {
    
    pb_user::ChangePasswordRequest request;
    request.set_old_password(old_password);
    request.set_new_password(new_password);

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->ChangePassword(context.get(), request, &response);

    if (!status.ok()) {
        return Result<void>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<void>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<void>::Ok();
}

Result<void> UserClient::DeleteUser(const std::string& verify_code) {
    pb_user::DeleteUserRequest request;
    request.set_verify_code(verify_code);

    pb_user::DeleteUserResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->DeleteUser(context.get(), request, &response);

    if (!status.ok()) {
        return Result<void>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<void>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<void>::Ok();
}

// ============================================================================
// 管理员操作
// ============================================================================

Result<UserEntity> UserClient::GetUser(const std::string& user_id) {
    pb_user::GetUserRequest request;
    request.set_id(user_id);

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->GetUser(context.get(), request, &response);

    if (!status.ok()) {
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<UserEntity>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<UserEntity>::Ok(FromProtoUser(response.user()));
}

Result<std::pair<std::vector<UserEntity>, PageResult>> UserClient::ListUsers(
    std::optional<std::string> mobile_filter,
    std::optional<bool> disabled_filter,
    int32_t page,
    int32_t page_size) {
    
    pb_user::ListUsersRequest request;
    
    auto* page_req = request.mutable_page();
    page_req->set_page(page);
    page_req->set_page_size(page_size);
    
    if (mobile_filter.has_value()) {
        request.set_mobile_filter(mobile_filter.value());
    }
    
    if (disabled_filter.has_value()) {
        request.mutable_disabled_filter()->set_value(disabled_filter.value());
    }

    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->ListUsers(context.get(), request, &response);

    if (!status.ok()) {
        return Result<std::pair<std::vector<UserEntity>, PageResult>>::Fail(
            ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<std::pair<std::vector<UserEntity>, PageResult>>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    // 转换用户列表
    std::vector<UserEntity> users;
    users.reserve(response.users_size());
    for (const auto& proto_user : response.users()) {
        users.push_back(FromProtoUser(proto_user));
    }

    // 转换分页信息
    PageResult page_result;
    page_result.total_records = response.page_info().total_records();
    page_result.total_pages = response.page_info().total_pages();
    page_result.page = response.page_info().page();
    page_result.page_size = response.page_info().page_size();

    return Result<std::pair<std::vector<UserEntity>, PageResult>>::Ok(
        std::make_pair(std::move(users), page_result)
    );
}

} // namespace user_service
