// tests/client/mock_auth_service.h
#pragma once

#include <gmock/gmock.h>
#include "pb_auth/auth.grpc.pb.h"

namespace user_service {
namespace testing {

/**
 * @brief Mock AuthService::StubInterface
 * 
 * 用于单元测试，模拟 gRPC 服务端响应
 */
class MockAuthServiceStub : public pb_auth::AuthService::StubInterface {
public:
    // ==================== 同步方法 Mock ====================
    
    MOCK_METHOD(grpc::Status, SendVerifyCode,
        (grpc::ClientContext* context,
         const pb_auth::SendVerifyCodeRequest& request,
         pb_auth::SendVerifyCodeResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, Register,
        (grpc::ClientContext* context,
         const pb_auth::RegisterRequest& request,
         pb_auth::RegisterResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, LoginByPassword,
        (grpc::ClientContext* context,
         const pb_auth::LoginByPasswordRequest& request,
         pb_auth::LoginByPasswordResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, LoginByCode,
        (grpc::ClientContext* context,
         const pb_auth::LoginByCodeRequest& request,
         pb_auth::LoginByCodeResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, RefreshToken,
        (grpc::ClientContext* context,
         const pb_auth::RefreshTokenRequest& request,
         pb_auth::RefreshTokenResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, Logout,
        (grpc::ClientContext* context,
         const pb_auth::LogoutRequest& request,
         pb_auth::LogoutResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, ResetPassword,
        (grpc::ClientContext* context,
         const pb_auth::ResetPasswordRequest& request,
         pb_auth::ResetPasswordResponse* response),
        (override));

    MOCK_METHOD(grpc::Status, ValidateToken,
        (grpc::ClientContext* context,
         const pb_auth::ValidateTokenRequest& request,
         pb_auth::ValidateTokenResponse* response),
        (override));

    // ==================== 异步方法 Mock（必须实现，即使不使用）====================
    
    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::SendVerifyCodeResponse>*,
        AsyncSendVerifyCodeRaw,
        (grpc::ClientContext* context,
         const pb_auth::SendVerifyCodeRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::SendVerifyCodeResponse>*,
        PrepareAsyncSendVerifyCodeRaw,
        (grpc::ClientContext* context,
         const pb_auth::SendVerifyCodeRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::RegisterResponse>*,
        AsyncRegisterRaw,
        (grpc::ClientContext* context,
         const pb_auth::RegisterRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::RegisterResponse>*,
        PrepareAsyncRegisterRaw,
        (grpc::ClientContext* context,
         const pb_auth::RegisterRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByPasswordResponse>*,
        AsyncLoginByPasswordRaw,
        (grpc::ClientContext* context,
         const pb_auth::LoginByPasswordRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByPasswordResponse>*,
        PrepareAsyncLoginByPasswordRaw,
        (grpc::ClientContext* context,
         const pb_auth::LoginByPasswordRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByCodeResponse>*,
        AsyncLoginByCodeRaw,
        (grpc::ClientContext* context,
         const pb_auth::LoginByCodeRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByCodeResponse>*,
        PrepareAsyncLoginByCodeRaw,
        (grpc::ClientContext* context,
         const pb_auth::LoginByCodeRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::RefreshTokenResponse>*,
        AsyncRefreshTokenRaw,
        (grpc::ClientContext* context,
         const pb_auth::RefreshTokenRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::RefreshTokenResponse>*,
        PrepareAsyncRefreshTokenRaw,
        (grpc::ClientContext* context,
         const pb_auth::RefreshTokenRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::LogoutResponse>*,
        AsyncLogoutRaw,
        (grpc::ClientContext* context,
         const pb_auth::LogoutRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::LogoutResponse>*,
        PrepareAsyncLogoutRaw,
        (grpc::ClientContext* context,
         const pb_auth::LogoutRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::ResetPasswordResponse>*,
        AsyncResetPasswordRaw,
        (grpc::ClientContext* context,
         const pb_auth::ResetPasswordRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::ResetPasswordResponse>*,
        PrepareAsyncResetPasswordRaw,
        (grpc::ClientContext* context,
         const pb_auth::ResetPasswordRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::ValidateTokenResponse>*,
        AsyncValidateTokenRaw,
        (grpc::ClientContext* context,
         const pb_auth::ValidateTokenRequest& request,
         grpc::CompletionQueue* cq),
        (override));

    MOCK_METHOD(grpc::ClientAsyncResponseReaderInterface<pb_auth::ValidateTokenResponse>*,
        PrepareAsyncValidateTokenRaw,
        (grpc::ClientContext* context,
         const pb_auth::ValidateTokenRequest& request,
         grpc::CompletionQueue* cq),
        (override));
};

} // namespace testing
} // namespace user_service
