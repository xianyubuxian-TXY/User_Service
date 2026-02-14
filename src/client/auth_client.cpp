// src/client/auth_client.cpp

#include "client/auth_client.h"
#include "common/proto_converter.h"
#include "common/logger.h"

namespace user_service {

// ============================================================================
// 构造函数
// ============================================================================

AuthClient::AuthClient(const std::string& target)
    : channel_(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()))
    , stub_(pb_auth::AuthService::NewStub(channel_)) {
}

AuthClient::AuthClient(const ClientOptions& options)
    : channel_(grpc::CreateCustomChannel(
          options.target,
          options.CreateCredentials(),
          options.CreateChannelArgs()))
    , stub_(pb_auth::AuthService::NewStub(channel_))
    , timeout_(options.timeout) {
}

AuthClient::AuthClient(std::shared_ptr<grpc::Channel> channel)
    : channel_(std::move(channel))
    , stub_(pb_auth::AuthService::NewStub(channel_)) {
}

// ============================================================================
// 辅助方法
// ============================================================================

std::unique_ptr<grpc::ClientContext> AuthClient::CreateContext() const {
    auto context = std::make_unique<grpc::ClientContext>();
    context->set_deadline(std::chrono::system_clock::now() + timeout_);
    return context;
}

// ============================================================================
// 发送验证码
// ============================================================================

Result<int32_t> AuthClient::SendVerifyCode(const std::string& mobile, SmsScene scene) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile(mobile);
    request.set_scene(ToProtoSmsScene(scene));

    pb_auth::SendVerifyCodeResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->SendVerifyCode(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("SendVerifyCode RPC failed: {}", status.error_message());
        return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<int32_t>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<int32_t>::Ok(response.retry_after());
}

// ============================================================================
// 注册
// ============================================================================

Result<AuthResult> AuthClient::Register(
    const std::string& mobile,
    const std::string& verify_code,
    const std::string& password,
    const std::string& display_name) {
    
    pb_auth::RegisterRequest request;
    request.set_mobile(mobile);
    request.set_verify_code(verify_code);
    request.set_password(password);
    if (!display_name.empty()) {
        request.set_display_name(display_name);
    }

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->Register(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("Register RPC failed: {}", status.error_message());
        return Result<AuthResult>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<AuthResult>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    // 转换响应
    AuthResult result;
    result.user = FromProtoUserInfo(response.user());
    result.tokens = FromProtoTokenPair(response.tokens());

    return Result<AuthResult>::Ok(result);
}

// ============================================================================
// 密码登录
// ============================================================================

Result<AuthResult> AuthClient::LoginByPassword(
    const std::string& mobile,
    const std::string& password) {
    
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile(mobile);
    request.set_password(password);

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->LoginByPassword(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("LoginByPassword RPC failed: {}", status.error_message());
        return Result<AuthResult>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<AuthResult>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    AuthResult result;
    result.user = FromProtoUserInfo(response.user());
    result.tokens = FromProtoTokenPair(response.tokens());

    return Result<AuthResult>::Ok(result);
}

// ============================================================================
// 验证码登录
// ============================================================================

Result<AuthResult> AuthClient::LoginByCode(
    const std::string& mobile,
    const std::string& verify_code) {
    
    pb_auth::LoginByCodeRequest request;
    request.set_mobile(mobile);
    request.set_verify_code(verify_code);

    pb_auth::LoginByCodeResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->LoginByCode(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("LoginByCode RPC failed: {}", status.error_message());
        return Result<AuthResult>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<AuthResult>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    AuthResult result;
    result.user = FromProtoUserInfo(response.user());
    result.tokens = FromProtoTokenPair(response.tokens());

    return Result<AuthResult>::Ok(result);
}

// ============================================================================
// 刷新 Token
// ============================================================================

Result<TokenPair> AuthClient::RefreshToken(const std::string& refresh_token) {
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token(refresh_token);

    pb_auth::RefreshTokenResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->RefreshToken(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("RefreshToken RPC failed: {}", status.error_message());
        return Result<TokenPair>::Fail(ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<TokenPair>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    return Result<TokenPair>::Ok(FromProtoTokenPair(response.tokens()));
}

// ============================================================================
// 登出
// ============================================================================

Result<void> AuthClient::Logout(const std::string& refresh_token) {
    pb_auth::LogoutRequest request;
    request.set_refresh_token(refresh_token);

    pb_auth::LogoutResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->Logout(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("Logout RPC failed: {}", status.error_message());
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
// 重置密码
// ============================================================================

Result<void> AuthClient::ResetPassword(
    const std::string& mobile,
    const std::string& verify_code,
    const std::string& new_password) {
    
    pb_auth::ResetPasswordRequest request;
    request.set_mobile(mobile);
    request.set_verify_code(verify_code);
    request.set_new_password(new_password);

    pb_auth::ResetPasswordResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->ResetPassword(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("ResetPassword RPC failed: {}", status.error_message());
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
// 验证 Token（内部服务用）
// ============================================================================

Result<TokenValidationResult> AuthClient::ValidateToken(const std::string& access_token) {
    pb_auth::ValidateTokenRequest request;
    request.set_access_token(access_token);

    pb_auth::ValidateTokenResponse response;
    auto context = CreateContext();

    grpc::Status status = stub_->ValidateToken(context.get(), request, &response);

    if (!status.ok()) {
        LOG_ERROR("ValidateToken RPC failed: {}", status.error_message());
        return Result<TokenValidationResult>::Fail(
            ErrorCode::ServiceUnavailable, status.error_message());
    }

    if (response.result().code() != pb_common::ErrorCode::OK) {
        return Result<TokenValidationResult>::Fail(
            FromProtoErrorCode(response.result().code()),
            response.result().msg()
        );
    }

    TokenValidationResult result;
    result.user_id = std::stoll(response.user_id());
    result.user_uuid = response.user_uuid();
    result.mobile = response.mobile();
    result.role = FromProtoUserRole(response.role());
    
    // 转换过期时间
    if (response.has_expires_at()) {
        result.expires_at = std::chrono::system_clock::from_time_t(
            response.expires_at().seconds());
    }

    return Result<TokenValidationResult>::Ok(result);
}

} // namespace user_service
