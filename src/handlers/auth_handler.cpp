#include "handlers/auth_handler.h"
#include "service/auth_service.h"
#include "common/validator.h"
#include "common/logger.h"
#include "common/auth_type.h"
#include "common/proto_converter.h"

namespace user_service{

AuthHandler::AuthHandler(std::shared_ptr<AuthService> auth_service)
    : auth_service_(std::move(auth_service)) {
}

// ============================================================================
// 发送验证码
// ============================================================================

::grpc::Status AuthHandler::SendVerifyCode(
    ::grpc::ServerContext* context,
    const ::pb_auth::SendVerifyCodeRequest* request,
    ::pb_auth::SendVerifyCodeResponse* response) {

    LOG_INFO("SendVerifyCode: mobile={}, scene={}", 
        request->mobile(), static_cast<int>(request->scene()));

    // 1.参数校验
    std::string error;
    if(!IsValidMobile(request->mobile(),error)){
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }

    auto scene=FromProtoSmsScene(request->scene());
    if(scene==SmsScene::UnKnow){
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,"无效的验证码");
        return ::grpc::Status::OK;
    }

    // 2.调用业务逻辑
    auto result=auth_service_->SendVerifyCode(request->mobile(),scene);

    // 3.设置响应
    SetResultError(response->mutable_result(),result.code,result.message);

    if(result.IsOk()){
        response->set_retry_after(result.Value());
    }

    return ::grpc::Status::OK;
}


// ============================================================================
// 注册
// ============================================================================

::grpc::Status AuthHandler::Register(
    ::grpc::ServerContext* context,
    const ::pb_auth::RegisterRequest* request,
    ::pb_auth::RegisterResponse* response) {
    
    LOG_INFO("Register: mobile={}", request->mobile());

    // 1. 参数校验
    std::string error;
    if (!IsValidMobile(request->mobile(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;
    }
    if (!IsValidVerifyCode(request->verify_code(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;
    }
    if (!IsValidPassword(request->password(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;
    }
    // display_name 可选，但如果提供了就要校验
    if (!request->display_name().empty() && 
        !IsValidDisplayName(request->display_name(), error)) {
        SetResultError(response->mutable_result(), ErrorCode::InvalidArgument, error);
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->Register(
        request->mobile(),
        request->verify_code(),
        request->password(),
        request->display_name()
    );

    // 3. 设置响应
    SetResultError(response->mutable_result(), result.code, result.message);

    if (result.IsOk()) {
        const auto& auth_result = result.Value();
        // 将业务层结果转为proto结果
        ToProtoUserInfo(auth_result.user, response->mutable_user());
        ToProtoTokenPair(auth_result.tokens, response->mutable_tokens());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 密码登录
// ============================================================================

::grpc::Status AuthHandler::LoginByPassword(
    ::grpc::ServerContext* context,
    const ::pb_auth::LoginByPasswordRequest* request,
    ::pb_auth::LoginByPasswordResponse* response) {
    
    LOG_INFO("LoginByPassword: mobile={}", request->mobile());

    // 1. 参数校验
    std::string error;
    if (!IsValidMobile(request->mobile(), error)) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }
    if (request->password().empty()) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,"密码不能为空");
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->LoginByPassword(
        request->mobile(),
        request->password()
    );

    // 3. 设置响应
    SetResultError(response->mutable_result(),result.code,result.message);

    if (result.IsOk()) {
        const auto& auth_result = result.Value();
        // 将业务层结果转为proto结果
        ToProtoUserInfo(auth_result.user,response->mutable_user());
        ToProtoTokenPair(auth_result.tokens,response->mutable_tokens());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 验证码登录
// ============================================================================

::grpc::Status AuthHandler::LoginByCode(
    ::grpc::ServerContext* context,
    const ::pb_auth::LoginByCodeRequest* request,
    ::pb_auth::LoginByCodeResponse* response) {
    
    LOG_INFO("LoginByCode: mobile={}", request->mobile());

    // 1. 参数校验
    std::string error;
    if (!IsValidMobile(request->mobile(), error)) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }
    if (!IsValidVerifyCode(request->verify_code(), error)) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->LoginByCode(
        request->mobile(),
        request->verify_code()
    );

    // 3. 设置响应
    SetResultError(response->mutable_result(),result.code,result.message);

    if (result.IsOk()) {
        const auto& auth_result = result.Value();
        // 将业务层结果转为proto结果
        ToProtoUserInfo(auth_result.user,response->mutable_user());
        ToProtoTokenPair(auth_result.tokens,response->mutable_tokens());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 刷新 Token
// ============================================================================

::grpc::Status AuthHandler::RefreshToken(
    ::grpc::ServerContext* context,
    const ::pb_auth::RefreshTokenRequest* request,
    ::pb_auth::RefreshTokenResponse* response) {
    
    LOG_DEBUG("RefreshToken requested");

    // 1. 参数校验
    if (request->refresh_token().empty()) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,"refresh_token 不能为空");
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->RefreshToken(request->refresh_token());

    // 3. 设置响应
    SetResultError(response->mutable_result(),result.code,result.message);

    if (result.IsOk()) {
        const auto& token_pair = result.Value();
        ToProtoTokenPair(token_pair,response->mutable_tokens());
    }

    return ::grpc::Status::OK;
}

// ============================================================================
// 登出
// ============================================================================

::grpc::Status AuthHandler::Logout(
    ::grpc::ServerContext* context,
    const ::pb_auth::LogoutRequest* request,
    ::pb_auth::LogoutResponse* response) {
    
    LOG_INFO("Logout requested");

    // 1. 参数校验
    if (request->refresh_token().empty()) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,"refresh_token 不能为空");
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->Logout(request->refresh_token());

    // 3. 设置响应
    SetResultError(response->mutable_result(),result.code,result.message);

    return ::grpc::Status::OK;
}

// ============================================================================
// 重置密码
// ============================================================================

::grpc::Status AuthHandler::ResetPassword(
    ::grpc::ServerContext* context,
    const ::pb_auth::ResetPasswordRequest* request,
    ::pb_auth::ResetPasswordResponse* response) {
    
    LOG_INFO("ResetPassword: mobile={}", request->mobile());

    // 1. 参数校验
    std::string error;
    if (!IsValidMobile(request->mobile(), error)) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }
    if (!IsValidVerifyCode(request->verify_code(), error)) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }
    if (!IsValidPassword(request->new_password(), error)) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,error);
        return ::grpc::Status::OK;
    }

    // 2. 调用业务逻辑
    auto result = auth_service_->ResetPassword(
        request->mobile(),
        request->verify_code(),
        request->new_password()
    );

    // 3. 设置响应
    SetResultError(response->mutable_result(),result.code,result.message);

    return ::grpc::Status::OK;
}

// ============================================================================
// 验证 Token（内部服务调用）
// ============================================================================

::grpc::Status AuthHandler::ValidateToken(
    ::grpc::ServerContext* context,
    const ::pb_auth::ValidateTokenRequest* request,
    ::pb_auth::ValidateTokenResponse* response) {
    
    LOG_DEBUG("ValidateToken requested");

    // 1. 参数校验
    if (request->access_token().empty()) {
        SetResultError(response->mutable_result(),ErrorCode::InvalidArgument,"access_token 不能为空");
        return ::grpc::Status::OK;
    }

    // 2. 验证 Token
    auto verify_res=auth_service_->ValidateAccessToken(request->access_token());
    if(!verify_res.IsOk()){
        SetResultError(response->mutable_result(),verify_res.code,verify_res.message);
        return ::grpc::Status::OK;
    }

    // 3. 设置响应
    SetResultOk(response->mutable_result());
    SetValidateTokenResponse(verify_res.Value(),response);

    return ::grpc::Status::OK;
}

}  // namespace user_service
