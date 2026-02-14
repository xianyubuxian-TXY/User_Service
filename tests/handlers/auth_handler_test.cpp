// tests/handlers/auth_handler_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <grpcpp/grpcpp.h>

#include "handlers/auth_handler.h"
#include "mock_services.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::Eq;

class AuthHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_auth_service_ = std::make_shared<MockAuthService>();
        handler_ = std::make_unique<AuthHandler>(mock_auth_service_);
    }

    void TearDown() override {
        handler_.reset();
        mock_auth_service_.reset();
    }

    // 创建测试用的 ServerContext
    std::unique_ptr<grpc::ServerContext> CreateContext() {
        return std::make_unique<grpc::ServerContext>();
    }

    std::shared_ptr<MockAuthService> mock_auth_service_;
    std::unique_ptr<AuthHandler> handler_;
};

// ============================================================================
// SendVerifyCode 测试
// ============================================================================

TEST_F(AuthHandlerTest, SendVerifyCode_Success) {
    // Arrange
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode("13800138000", SmsScene::Register))
        .WillOnce(Return(Result<int32_t>::Ok(60)));

    // Act
    auto status = handler_->SendVerifyCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.retry_after(), 60);
}

TEST_F(AuthHandlerTest, SendVerifyCode_InvalidMobile) {
    // Arrange
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("123");  // 无效手机号
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_REGISTER);

    pb_auth::SendVerifyCodeResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->SendVerifyCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());  // gRPC 状态成功，业务错误在 response 中
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, SendVerifyCode_UnknownScene) {
    // Arrange
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_UNKNOWN);

    pb_auth::SendVerifyCodeResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->SendVerifyCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, SendVerifyCode_RateLimited) {
    // Arrange
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_LOGIN);

    pb_auth::SendVerifyCodeResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, SendVerifyCode("13800138000", SmsScene::Login))
        .WillOnce(Return(Result<int32_t>::Fail(ErrorCode::RateLimited, "请求过于频繁")));

    // Act
    auto status = handler_->SendVerifyCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::RATE_LIMITED);
}

// ============================================================================
// Register 测试
// ============================================================================

TEST_F(AuthHandlerTest, Register_Success) {
    // Arrange
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123");
    request.set_display_name("TestUser");

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    auto auth_result = CreateTestAuthResult();
    EXPECT_CALL(*mock_auth_service_, Register("13800138000", "123456", "Password123", "TestUser"))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    // Act
    auto status = handler_->Register(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().id(), "test-uuid-123");
    EXPECT_EQ(response.user().mobile(), "13800138000");
    EXPECT_EQ(response.tokens().access_token(), "test-access-token");
    EXPECT_EQ(response.tokens().refresh_token(), "test-refresh-token");
    EXPECT_EQ(response.tokens().expires_in(), 900);
}

TEST_F(AuthHandlerTest, Register_InvalidMobile) {
    // Arrange
    pb_auth::RegisterRequest request;
    request.set_mobile("invalid");
    request.set_verify_code("123456");
    request.set_password("Password123");

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->Register(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, Register_InvalidVerifyCode) {
    // Arrange
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("12345");  // 长度不对
    request.set_password("Password123");

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->Register(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, Register_InvalidPassword) {
    // Arrange
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("123");  // 密码太短

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->Register(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, Register_MobileTaken) {
    // Arrange
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123");

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, Register(_, _, _, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::MobileTaken, "手机号已被注册")));

    // Act
    auto status = handler_->Register(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::MOBILE_TAKEN);
}

TEST_F(AuthHandlerTest, Register_WrongCaptcha) {
    // Arrange
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("000000");
    request.set_password("Password123");

    pb_auth::RegisterResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, Register(_, _, _, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::CaptchaWrong, "验证码错误")));

    // Act
    auto status = handler_->Register(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::CAPTCHA_WRONG);
}

// ============================================================================
// LoginByPassword 测试
// ============================================================================

TEST_F(AuthHandlerTest, LoginByPassword_Success) {
    // Arrange
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123");

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    auto auth_result = CreateTestAuthResult();
    EXPECT_CALL(*mock_auth_service_, LoginByPassword("13800138000", "Password123"))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    // Act
    auto status = handler_->LoginByPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().id(), "test-uuid-123");
    EXPECT_FALSE(response.tokens().access_token().empty());
}

TEST_F(AuthHandlerTest, LoginByPassword_WrongPassword) {
    // Arrange
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("WrongPassword");

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, LoginByPassword(_, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::WrongPassword, "账号或密码错误")));

    // Act
    auto status = handler_->LoginByPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::WRONG_PASSWORD);
}

TEST_F(AuthHandlerTest, LoginByPassword_UserNotFound) {
    // Arrange
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138001");
    request.set_password("Password123");

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, LoginByPassword(_, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::WrongPassword, "账号或密码错误")));

    // Act
    auto status = handler_->LoginByPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::WRONG_PASSWORD);
}

TEST_F(AuthHandlerTest, LoginByPassword_AccountLocked) {
    // Arrange
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123");

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, LoginByPassword(_, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::AccountLocked, "账号已锁定")));

    // Act
    auto status = handler_->LoginByPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::ACCOUNT_LOCKED);
}

TEST_F(AuthHandlerTest, LoginByPassword_UserDisabled) {
    // Arrange
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123");

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, LoginByPassword(_, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::UserDisabled, "账号已禁用")));

    // Act
    auto status = handler_->LoginByPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::USER_DISABLED);
}

TEST_F(AuthHandlerTest, LoginByPassword_EmptyPassword) {
    // Arrange
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("");

    pb_auth::LoginByPasswordResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->LoginByPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

// ============================================================================
// LoginByCode 测试
// ============================================================================

TEST_F(AuthHandlerTest, LoginByCode_Success) {
    // Arrange
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");

    pb_auth::LoginByCodeResponse response;
    auto context = CreateContext();

    auto auth_result = CreateTestAuthResult();
    EXPECT_CALL(*mock_auth_service_, LoginByCode("13800138000", "123456"))
        .WillOnce(Return(Result<AuthResult>::Ok(auth_result)));

    // Act
    auto status = handler_->LoginByCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().id(), "test-uuid-123");
}

TEST_F(AuthHandlerTest, LoginByCode_WrongCode) {
    // Arrange
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("000000");

    pb_auth::LoginByCodeResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, LoginByCode(_, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::CaptchaWrong, "验证码错误")));

    // Act
    auto status = handler_->LoginByCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::CAPTCHA_WRONG);
}

TEST_F(AuthHandlerTest, LoginByCode_ExpiredCode) {
    // Arrange
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");

    pb_auth::LoginByCodeResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, LoginByCode(_, _))
        .WillOnce(Return(Result<AuthResult>::Fail(ErrorCode::CaptchaExpired, "验证码已过期")));

    // Act
    auto status = handler_->LoginByCode(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::CAPTCHA_EXPIRED);
}

// ============================================================================
// RefreshToken 测试
// ============================================================================

TEST_F(AuthHandlerTest, RefreshToken_Success) {
    // Arrange
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("valid-refresh-token");

    pb_auth::RefreshTokenResponse response;
    auto context = CreateContext();

    auto new_tokens = CreateTestTokenPair();
    new_tokens.access_token = "new-access-token";
    new_tokens.refresh_token = "new-refresh-token";
    
    EXPECT_CALL(*mock_auth_service_, RefreshToken("valid-refresh-token"))
        .WillOnce(Return(Result<TokenPair>::Ok(new_tokens)));

    // Act
    auto status = handler_->RefreshToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.tokens().access_token(), "new-access-token");
    EXPECT_EQ(response.tokens().refresh_token(), "new-refresh-token");
}

TEST_F(AuthHandlerTest, RefreshToken_EmptyToken) {
    // Arrange
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("");

    pb_auth::RefreshTokenResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->RefreshToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, RefreshToken_InvalidToken) {
    // Arrange
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("invalid-token");

    pb_auth::RefreshTokenResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, RefreshToken("invalid-token"))
        .WillOnce(Return(Result<TokenPair>::Fail(ErrorCode::TokenInvalid, "Token 无效")));

    // Act
    auto status = handler_->RefreshToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_INVALID);
}

TEST_F(AuthHandlerTest, RefreshToken_ExpiredToken) {
    // Arrange
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("expired-token");

    pb_auth::RefreshTokenResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, RefreshToken("expired-token"))
        .WillOnce(Return(Result<TokenPair>::Fail(ErrorCode::TokenExpired, "Token 已过期")));

    // Act
    auto status = handler_->RefreshToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_EXPIRED);
}

TEST_F(AuthHandlerTest, RefreshToken_RevokedToken) {
    // Arrange
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("revoked-token");

    pb_auth::RefreshTokenResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, RefreshToken("revoked-token"))
        .WillOnce(Return(Result<TokenPair>::Fail(ErrorCode::TokenRevoked, "Token 已注销")));

    // Act
    auto status = handler_->RefreshToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_REVOKED);
}

// ============================================================================
// Logout 测试
// ============================================================================

TEST_F(AuthHandlerTest, Logout_Success) {
    // Arrange
    pb_auth::LogoutRequest request;
    request.set_refresh_token("valid-refresh-token");

    pb_auth::LogoutResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, Logout("valid-refresh-token"))
        .WillOnce(Return(Result<void>::Ok()));

    // Act
    auto status = handler_->Logout(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(AuthHandlerTest, Logout_EmptyToken) {
    // Arrange
    pb_auth::LogoutRequest request;
    request.set_refresh_token("");

    pb_auth::LogoutResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->Logout(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

// ============================================================================
// ResetPassword 测试
// ============================================================================

TEST_F(AuthHandlerTest, ResetPassword_Success) {
    // Arrange
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_new_password("NewPassword123");

    pb_auth::ResetPasswordResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, ResetPassword("13800138000", "123456", "NewPassword123"))
        .WillOnce(Return(Result<void>::Ok()));

    // Act
    auto status = handler_->ResetPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(AuthHandlerTest, ResetPassword_InvalidMobile) {
    // Arrange
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("invalid");
    request.set_verify_code("123456");
    request.set_new_password("NewPassword123");

    pb_auth::ResetPasswordResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->ResetPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, ResetPassword_InvalidVerifyCode) {
    // Arrange
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("12345");  // 长度不对
    request.set_new_password("NewPassword123");

    pb_auth::ResetPasswordResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->ResetPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, ResetPassword_WeakPassword) {
    // Arrange
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_new_password("123");  // 密码太弱

    pb_auth::ResetPasswordResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->ResetPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, ResetPassword_UserNotFound) {
    // Arrange
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138001");
    request.set_verify_code("123456");
    request.set_new_password("NewPassword123");

    pb_auth::ResetPasswordResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, ResetPassword(_, _, _))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::UserNotFound, "用户不存在")));

    // Act
    auto status = handler_->ResetPassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::USER_NOT_FOUND);
}

// ============================================================================
// ValidateToken 测试
// ============================================================================

TEST_F(AuthHandlerTest, ValidateToken_Success) {
    // Arrange
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("valid-access-token");

    pb_auth::ValidateTokenResponse response;
    auto context = CreateContext();

    TokenValidationResult validation;
    validation.user_id = 1;
    validation.user_uuid = "test-uuid-123";
    validation.mobile = "13800138000";
    validation.role = UserRole::User;
    validation.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);

    EXPECT_CALL(*mock_auth_service_, ValidateAccessToken("valid-access-token"))
        .WillOnce(Return(Result<TokenValidationResult>::Ok(validation)));

    // Act
    auto status = handler_->ValidateToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user_id(), "1");
    EXPECT_EQ(response.user_uuid(), "test-uuid-123");
    EXPECT_EQ(response.mobile(), "13800138000");
}

TEST_F(AuthHandlerTest, ValidateToken_EmptyToken) {
    // Arrange
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("");

    pb_auth::ValidateTokenResponse response;
    auto context = CreateContext();

    // Act
    auto status = handler_->ValidateToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AuthHandlerTest, ValidateToken_InvalidToken) {
    // Arrange
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("invalid-token");

    pb_auth::ValidateTokenResponse response;
    auto context = CreateContext();

    EXPECT_CALL(*mock_auth_service_, ValidateAccessToken("invalid-token"))
        .WillOnce(Return(Result<TokenValidationResult>::Fail(ErrorCode::TokenInvalid, "Token 无效")));

    // Act
    auto status = handler_->ValidateToken(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_INVALID);
}
