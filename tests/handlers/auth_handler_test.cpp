#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "handlers/auth_handler.h"
#include "mock_services.h"
#include "common/error_codes.h"
#include "common/auth_type.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::Eq;

// ============================================================================
// 测试夹具
// ============================================================================
class AuthHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_auth_service_ = std::make_shared<MockAuthService>();
        handler_ = std::make_unique<AuthHandler>(mock_auth_service_);
    }

    // 辅助：创建成功的 AuthResult
    AuthResult MakeAuthResult(const std::string& uuid = "test-uuid-123",
                               const std::string& mobile = "13800138000") {
        AuthResult result;
        result.user.id = 1;
        result.user.uuid = uuid;
        result.user.mobile = mobile;
        result.user.display_name = "测试用户";
        result.user.role = UserRole::User;
        result.user.disabled = false;
        result.tokens.access_token = "access_token_xxx";
        result.tokens.refresh_token = "refresh_token_xxx";
        result.tokens.expires_in = 3600;
        return result;
    }

    // 辅助：创建 TokenPair
    TokenPair MakeTokenPair() {
        TokenPair tokens;
        tokens.access_token = "new_access_token";
        tokens.refresh_token = "new_refresh_token";
        tokens.expires_in = 3600;
        return tokens;
    }

    std::shared_ptr<MockAuthService> mock_auth_service_;
    std::unique_ptr<AuthHandler> handler_;
};

// ============================================================================
// SendVerifyCode 测试
// ============================================================================

TEST_F(AuthHandlerTest, SendVerifyCode_Success) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode("13800138000", SmsScene::Register))
        .WillOnce(Return(Result<int32_t>::Ok(60)));

    grpc::ServerContext context;
    auto status = handler_->SendVerifyCode(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.retry_after(), 60);
}

TEST_F(AuthHandlerTest, SendVerifyCode_InvalidMobile_Empty) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("");
    request.set_scene(pb_auth::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;

    // Service 不应该被调用
    EXPECT_CALL(*mock_auth_service_, SendVerifyCode(_, _)).Times(0);

    grpc::ServerContext context;
    auto status = handler_->SendVerifyCode(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, SendVerifyCode_InvalidMobile_TooShort) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("1380013800");  // 10位
    request.set_scene(pb_auth::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->SendVerifyCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, SendVerifyCode_InvalidScene) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SMS_SCENE_UNKNOWN);

    pb_auth::SendVerifyCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->SendVerifyCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, SendVerifyCode_RateLimited) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode("13800138000", SmsScene::Register))
        .WillOnce(Return(Result<int32_t>::Fail(ErrorCode::RateLimited, "请求过于频繁")));

    grpc::ServerContext context;
    handler_->SendVerifyCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::RATE_LIMITED));
}

TEST_F(AuthHandlerTest, SendVerifyCode_MobileTaken_RegisterScene) {
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode("13800138000", SmsScene::Register))
        .WillOnce(Return(Result<int32_t>::Fail(ErrorCode::MobileTaken)));

    grpc::ServerContext context;
    handler_->SendVerifyCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::MOBILE_TAKEN));
}

// ============================================================================
// Register 测试
// ============================================================================

TEST_F(AuthHandlerTest, Register_Success) {
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123!");
    request.set_display_name("测试用户");

    pb_auth::RegisterResponse response;

    auto auth_result = MakeAuthResult();
    EXPECT_CALL(*mock_auth_service_, 
                Register("13800138000", "123456", "Password123!", "测试用户"))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    grpc::ServerContext context;
    auto status = handler_->Register(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user().id(), "test-uuid-123");
    EXPECT_EQ(response.user().mobile(), "13800138000");
    EXPECT_EQ(response.tokens().access_token(), "access_token_xxx");
}

TEST_F(AuthHandlerTest, Register_Success_NoDisplayName) {
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123!");
    // display_name 留空

    pb_auth::RegisterResponse response;

    auto auth_result = MakeAuthResult();
    EXPECT_CALL(*mock_auth_service_, 
                Register("13800138000", "123456", "Password123!", ""))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    grpc::ServerContext context;
    handler_->Register(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(AuthHandlerTest, Register_InvalidMobile) {
    pb_auth::RegisterRequest request;
    request.set_mobile("123");
    request.set_verify_code("123456");
    request.set_password("Password123!");

    pb_auth::RegisterResponse response;

    EXPECT_CALL(*mock_auth_service_, Register(_, _, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->Register(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, Register_InvalidVerifyCode_TooShort) {
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123");  // 太短
    request.set_password("Password123!");

    pb_auth::RegisterResponse response;

    EXPECT_CALL(*mock_auth_service_, Register(_, _, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->Register(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, Register_WeakPassword) {
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("123");  // 太弱

    pb_auth::RegisterResponse response;

    EXPECT_CALL(*mock_auth_service_, Register(_, _, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->Register(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, Register_MobileTaken) {
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123!");

    pb_auth::RegisterResponse response;

    EXPECT_CALL(*mock_auth_service_, Register("13800138000", "123456", "Password123!", ""))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::MobileTaken)));

    grpc::ServerContext context;
    handler_->Register(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::MOBILE_TAKEN));
}

TEST_F(AuthHandlerTest, Register_CaptchaWrong) {
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("000000");
    request.set_password("Password123!");

    pb_auth::RegisterResponse response;

    EXPECT_CALL(*mock_auth_service_, Register("13800138000", "000000", "Password123!", ""))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::CaptchaWrong)));

    grpc::ServerContext context;
    handler_->Register(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::CAPTCHA_WRONG));
}

// ============================================================================
// LoginByPassword 测试
// ============================================================================

TEST_F(AuthHandlerTest, LoginByPassword_Success) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123!");

    pb_auth::LoginByPasswordResponse response;

    auto auth_result = MakeAuthResult();
    EXPECT_CALL(*mock_auth_service_, LoginByPassword("13800138000", "Password123!"))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    grpc::ServerContext context;
    auto status = handler_->LoginByPassword(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user().id(), "test-uuid-123");
    EXPECT_FALSE(response.tokens().access_token().empty());
}

TEST_F(AuthHandlerTest, LoginByPassword_InvalidMobile) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("invalid");
    request.set_password("Password123!");

    pb_auth::LoginByPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByPassword(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->LoginByPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, LoginByPassword_EmptyPassword) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("");

    pb_auth::LoginByPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByPassword(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->LoginByPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, LoginByPassword_WrongPassword) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("WrongPassword123!");

    pb_auth::LoginByPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByPassword("13800138000", "WrongPassword123!"))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::WrongPassword)));

    grpc::ServerContext context;
    handler_->LoginByPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::WRONG_PASSWORD));
}

TEST_F(AuthHandlerTest, LoginByPassword_UserNotFound) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123!");

    pb_auth::LoginByPasswordResponse response;

    // 注意：为了安全，用户不存在也返回 WrongPassword
    EXPECT_CALL(*mock_auth_service_, LoginByPassword("13800138000", "Password123!"))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::WrongPassword, "账号或密码错误")));

    grpc::ServerContext context;
    handler_->LoginByPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::WRONG_PASSWORD));
}

TEST_F(AuthHandlerTest, LoginByPassword_AccountLocked) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123!");

    pb_auth::LoginByPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByPassword("13800138000", "Password123!"))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::AccountLocked, "账号已锁定")));

    grpc::ServerContext context;
    handler_->LoginByPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::ACCOUNT_LOCKED));
}

TEST_F(AuthHandlerTest, LoginByPassword_UserDisabled) {
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123!");

    pb_auth::LoginByPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByPassword("13800138000", "Password123!"))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::UserDisabled)));

    grpc::ServerContext context;
    handler_->LoginByPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::USER_DISABLED));
}

// ============================================================================
// LoginByCode 测试
// ============================================================================

TEST_F(AuthHandlerTest, LoginByCode_Success) {
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");

    pb_auth::LoginByCodeResponse response;

    auto auth_result = MakeAuthResult();
    EXPECT_CALL(*mock_auth_service_, LoginByCode("13800138000", "123456"))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    grpc::ServerContext context;
    handler_->LoginByCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user().id(), "test-uuid-123");
}

TEST_F(AuthHandlerTest, LoginByCode_InvalidMobile) {
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("invalid");
    request.set_verify_code("123456");

    pb_auth::LoginByCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByCode(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->LoginByCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, LoginByCode_InvalidVerifyCode) {
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("12");  // 太短

    pb_auth::LoginByCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByCode(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->LoginByCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, LoginByCode_CaptchaWrong) {
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("000000");

    pb_auth::LoginByCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByCode("13800138000", "000000"))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::CaptchaWrong)));

    grpc::ServerContext context;
    handler_->LoginByCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::CAPTCHA_WRONG));
}

TEST_F(AuthHandlerTest, LoginByCode_UserNotFound) {
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");

    pb_auth::LoginByCodeResponse response;

    EXPECT_CALL(*mock_auth_service_, LoginByCode("13800138000", "123456"))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::UserNotFound)));

    grpc::ServerContext context;
    handler_->LoginByCode(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::USER_NOT_FOUND));
}

// ============================================================================
// RefreshToken 测试
// ============================================================================

TEST_F(AuthHandlerTest, RefreshToken_Success) {
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("valid_refresh_token");

    pb_auth::RefreshTokenResponse response;

    auto new_tokens = MakeTokenPair();
    EXPECT_CALL(*mock_auth_service_, RefreshToken("valid_refresh_token"))
        .WillOnce(Return(Result<TokenPair>::Ok(new_tokens)));

    grpc::ServerContext context;
    handler_->RefreshToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.tokens().access_token(), "new_access_token");
    EXPECT_EQ(response.tokens().refresh_token(), "new_refresh_token");
}

TEST_F(AuthHandlerTest, RefreshToken_EmptyToken) {
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("");

    pb_auth::RefreshTokenResponse response;

    EXPECT_CALL(*mock_auth_service_, RefreshToken(_)).Times(0);

    grpc::ServerContext context;
    handler_->RefreshToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, RefreshToken_TokenInvalid) {
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("invalid_token");

    pb_auth::RefreshTokenResponse response;

    EXPECT_CALL(*mock_auth_service_, RefreshToken("invalid_token"))
        .WillOnce(Return(Result<TokenPair>::Fail(ErrorCode::TokenInvalid)));

    grpc::ServerContext context;
    handler_->RefreshToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::TOKEN_INVALID));
}

TEST_F(AuthHandlerTest, RefreshToken_TokenExpired) {
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("expired_token");

    pb_auth::RefreshTokenResponse response;

    EXPECT_CALL(*mock_auth_service_, RefreshToken("expired_token"))
        .WillOnce(Return(Result<TokenPair>::Fail(ErrorCode::TokenExpired)));

    grpc::ServerContext context;
    handler_->RefreshToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::TOKEN_EXPIRED));
}

TEST_F(AuthHandlerTest, RefreshToken_TokenRevoked) {
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("revoked_token");

    pb_auth::RefreshTokenResponse response;

    EXPECT_CALL(*mock_auth_service_, RefreshToken("revoked_token"))
        .WillOnce(Return(Result<TokenPair>::Fail(ErrorCode::TokenRevoked)));

    grpc::ServerContext context;
    handler_->RefreshToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::TOKEN_REVOKED));
}

// ============================================================================
// Logout 测试
// ============================================================================

TEST_F(AuthHandlerTest, Logout_Success) {
    pb_auth::LogoutRequest request;
    request.set_refresh_token("valid_refresh_token");

    pb_auth::LogoutResponse response;

    EXPECT_CALL(*mock_auth_service_, Logout("valid_refresh_token"))
        .WillOnce(Return(Result<void>::Ok()));

    grpc::ServerContext context;
    handler_->Logout(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(AuthHandlerTest, Logout_EmptyToken) {
    pb_auth::LogoutRequest request;
    request.set_refresh_token("");

    pb_auth::LogoutResponse response;

    EXPECT_CALL(*mock_auth_service_, Logout(_)).Times(0);

    grpc::ServerContext context;
    handler_->Logout(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

// ============================================================================
// ResetPassword 测试
// ============================================================================

TEST_F(AuthHandlerTest, ResetPassword_Success) {
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_new_password("NewPassword123!");

    pb_auth::ResetPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, ResetPassword("13800138000", "123456", "NewPassword123!"))
        .WillOnce(Return(Result<void>::Ok()));

    grpc::ServerContext context;
    handler_->ResetPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(AuthHandlerTest, ResetPassword_InvalidMobile) {
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("123");
    request.set_verify_code("123456");
    request.set_new_password("NewPassword123!");

    pb_auth::ResetPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, ResetPassword(_, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->ResetPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, ResetPassword_WeakPassword) {
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_new_password("weak");

    pb_auth::ResetPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, ResetPassword(_, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->ResetPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, ResetPassword_CaptchaWrong) {
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("000000");
    request.set_new_password("NewPassword123!");

    pb_auth::ResetPasswordResponse response;

    EXPECT_CALL(*mock_auth_service_, ResetPassword("13800138000", "000000", "NewPassword123!"))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::CaptchaWrong)));

    grpc::ServerContext context;
    handler_->ResetPassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::CAPTCHA_WRONG));
}

// ============================================================================
// ValidateToken 测试
// ============================================================================

TEST_F(AuthHandlerTest, ValidateToken_Success) {
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("valid_access_token");

    pb_auth::ValidateTokenResponse response;

    TokenValidationResult validation;
    validation.user_id = 123;
    validation.user_uuid = "uuid-123";
    validation.mobile = "13800138000";
    validation.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);

    EXPECT_CALL(*mock_auth_service_, ValidateAccessToken("valid_access_token"))
        .WillOnce(Return(Result<TokenValidationResult>::Ok(validation)));

    grpc::ServerContext context;
    handler_->ValidateToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user_uuid(), "uuid-123");
    EXPECT_EQ(response.mobile(), "13800138000");
}

TEST_F(AuthHandlerTest, ValidateToken_EmptyToken) {
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("");

    pb_auth::ValidateTokenResponse response;

    EXPECT_CALL(*mock_auth_service_, ValidateAccessToken(_)).Times(0);

    grpc::ServerContext context;
    handler_->ValidateToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(AuthHandlerTest, ValidateToken_TokenExpired) {
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("expired_token");

    pb_auth::ValidateTokenResponse response;

    EXPECT_CALL(*mock_auth_service_, ValidateAccessToken("expired_token"))
        .WillOnce(Return(Result<TokenValidationResult>::Fail(ErrorCode::TokenExpired)));

    grpc::ServerContext context;
    handler_->ValidateToken(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::TOKEN_EXPIRED));
}
