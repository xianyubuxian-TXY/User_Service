#include "service_test_fixture.h"

namespace user_service {
namespace test {

class AuthServiceTest : public ServiceTestFixture {};

// ============================================================================
// SendVerifyCode 测试
// ============================================================================

TEST_F(AuthServiceTest, SendVerifyCode_Register_Success) {
    // 清除可能存在的限制
    ClearSendInterval("13800000001");
    
    auto result = auth_service_->SendVerifyCode("13800000001", SmsScene::Register);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_GT(result.data.value(), 0);  // 返回重发等待时间
}

TEST_F(AuthServiceTest, SendVerifyCode_Register_MobileAlreadyExists) {
    CreateTestUser("13800000002", "password123", "已注册用户");
    ClearSendInterval("13800000002");
    
    auto result = auth_service_->SendVerifyCode("13800000002", SmsScene::Register);
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::MobileTaken);
}

TEST_F(AuthServiceTest, SendVerifyCode_Login_Success) {
    CreateTestUser("13800000003", "password123", "测试用户");
    ClearSendInterval("13800000003");
    
    auto result = auth_service_->SendVerifyCode("13800000003", SmsScene::Login);
    
    ASSERT_TRUE(result.IsOk());
}

TEST_F(AuthServiceTest, SendVerifyCode_Login_UserNotFound) {
    ClearSendInterval("13899999999");
    
    auto result = auth_service_->SendVerifyCode("13899999999", SmsScene::Login);
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(AuthServiceTest, SendVerifyCode_InvalidMobile) {
    auto result = auth_service_->SendVerifyCode("12345", SmsScene::Register);
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

// ============================================================================
// Register 测试
// ============================================================================

TEST_F(AuthServiceTest, Register_Success) {
    std::string mobile = "13800001001";
    SendTestVerifyCode(SmsScene::Register, mobile);
    
    auto result = auth_service_->Register(
        mobile,
        GetTestVerifyCode(),
        "password123",
        "新用户"
    );
    
    ASSERT_TRUE(result.IsOk()) << "Register failed: " << result.message;
    EXPECT_EQ(result.data.value().user.mobile, mobile);
    EXPECT_EQ(result.data.value().user.display_name, "新用户");
    EXPECT_FALSE(result.data.value().tokens.access_token.empty());
    EXPECT_FALSE(result.data.value().tokens.refresh_token.empty());
    EXPECT_TRUE(result.data.value().user.password_hash.empty());  // 密码不返回
}

TEST_F(AuthServiceTest, Register_MobileTaken) {
    CreateTestUser("13800001002", "password123", "已存在用户");
    SendTestVerifyCode(SmsScene::Register, "13800001002");
    
    auto result = auth_service_->Register(
        "13800001002",
        GetTestVerifyCode(),
        "password456",
        "新用户"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::MobileTaken);
}

TEST_F(AuthServiceTest, Register_WrongVerifyCode) {
    std::string mobile = "13800001003";
    SendTestVerifyCode(SmsScene::Register, mobile);
    
    auto result = auth_service_->Register(
        mobile,
        "999999",  // 错误的验证码
        "password123",
        "新用户"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

TEST_F(AuthServiceTest, Register_InvalidPassword_TooShort) {
    std::string mobile = "13800001004";
    SendTestVerifyCode(SmsScene::Register, mobile);
    
    auto result = auth_service_->Register(
        mobile,
        GetTestVerifyCode(),
        "123",  // 太短（小于6位）
        "新用户"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(AuthServiceTest, Register_InvalidMobile) {
    auto result = auth_service_->Register(
        "12345",  // 无效手机号
        GetTestVerifyCode(),
        "password123",
        "新用户"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(AuthServiceTest, Register_ExpiredVerifyCode) {
    std::string mobile = "13800001005";
    // 不设置验证码，模拟过期
    
    auto result = auth_service_->Register(
        mobile,
        GetTestVerifyCode(),
        "password123",
        "新用户"
    );
    
    ASSERT_FALSE(result.IsOk());
    // 可能是 CaptchaWrong 或 CaptchaExpired
    EXPECT_TRUE(result.code == ErrorCode::CaptchaWrong || 
                result.code == ErrorCode::CaptchaExpired);
}

// ============================================================================
// LoginByPassword 测试
// ============================================================================

TEST_F(AuthServiceTest, LoginByPassword_Success) {
    CreateTestUser("13800002001", "password123", "测试用户");
    ClearLoginFailure("13800002001");
    
    auto result = auth_service_->LoginByPassword("13800002001", "password123");
    
    ASSERT_TRUE(result.IsOk()) << "Login failed: " << result.message;
    EXPECT_EQ(result.data.value().user.mobile, "13800002001");
    EXPECT_FALSE(result.data.value().tokens.access_token.empty());
    EXPECT_TRUE(result.data.value().user.password_hash.empty());
}

TEST_F(AuthServiceTest, LoginByPassword_WrongPassword) {
    CreateTestUser("13800002002", "password123", "测试用户");
    ClearLoginFailure("13800002002");
    
    auto result = auth_service_->LoginByPassword("13800002002", "wrongpassword");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::WrongPassword);
}

TEST_F(AuthServiceTest, LoginByPassword_UserNotFound) {
    ClearLoginFailure("13899999998");
    
    auto result = auth_service_->LoginByPassword("13899999998", "password123");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::WrongPassword);  // 不泄露用户是否存在
}

TEST_F(AuthServiceTest, LoginByPassword_UserDisabled) {
    CreateTestUser("13800002003", "password123", "禁用用户", true);
    ClearLoginFailure("13800002003");
    
    auto result = auth_service_->LoginByPassword("13800002003", "password123");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

TEST_F(AuthServiceTest, LoginByPassword_AccountLocked_AfterMaxAttempts) {
    CreateTestUser("13800002004", "password123", "测试用户");
    ClearLoginFailure("13800002004");
    
    // 尝试登录失败多次
    for (int i = 0; i < config_->login.max_failed_attempts; ++i) {
        auth_service_->LoginByPassword("13800002004", "wrongpassword");
    }
    
    // 再次尝试应该被锁定（即使密码正确）
    auto result = auth_service_->LoginByPassword("13800002004", "password123");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::AccountLocked);
}

TEST_F(AuthServiceTest, LoginByPassword_ClearFailureAfterSuccess) {
    CreateTestUser("13800002005", "password123", "测试用户");
    ClearLoginFailure("13800002005");
    
    // 失败几次（但不超过限制）
    for (int i = 0; i < config_->login.max_failed_attempts - 1; ++i) {
        auth_service_->LoginByPassword("13800002005", "wrongpassword");
    }
    
    // 成功登录
    auto success_result = auth_service_->LoginByPassword("13800002005", "password123");
    ASSERT_TRUE(success_result.IsOk());
    
    // 再次失败（计数应该已重置）
    for (int i = 0; i < config_->login.max_failed_attempts - 1; ++i) {
        auth_service_->LoginByPassword("13800002005", "wrongpassword");
    }
    
    // 应该还能登录成功
    auto result = auth_service_->LoginByPassword("13800002005", "password123");
    EXPECT_TRUE(result.IsOk());
}

// ============================================================================
// LoginByCode 测试
// ============================================================================

TEST_F(AuthServiceTest, LoginByCode_Success) {
    CreateTestUser("13800003001", "password123", "测试用户");
    SendTestVerifyCode(SmsScene::Login, "13800003001");
    
    auto result = auth_service_->LoginByCode("13800003001", GetTestVerifyCode());
    
    ASSERT_TRUE(result.IsOk()) << "LoginByCode failed: " << result.message;
    EXPECT_EQ(result.data.value().user.mobile, "13800003001");
    EXPECT_FALSE(result.data.value().tokens.access_token.empty());
}

TEST_F(AuthServiceTest, LoginByCode_WrongCode) {
    CreateTestUser("13800003002", "password123", "测试用户");
    SendTestVerifyCode(SmsScene::Login, "13800003002");
    
    auto result = auth_service_->LoginByCode("13800003002", "999999");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

TEST_F(AuthServiceTest, LoginByCode_UserNotFound) {
    SendTestVerifyCode(SmsScene::Login, "13899999997");
    
    auto result = auth_service_->LoginByCode("13899999997", GetTestVerifyCode());
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(AuthServiceTest, LoginByCode_UserDisabled) {
    CreateTestUser("13800003003", "password123", "禁用用户", true);
    SendTestVerifyCode(SmsScene::Login, "13800003003");
    
    auto result = auth_service_->LoginByCode("13800003003", GetTestVerifyCode());
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

TEST_F(AuthServiceTest, LoginByCode_ClearsPasswordFailure) {
    CreateTestUser("13800003004", "password123", "测试用户");
    ClearLoginFailure("13800003004");
    
    // 密码登录失败几次
    for (int i = 0; i < config_->login.max_failed_attempts - 1; ++i) {
        auth_service_->LoginByPassword("13800003004", "wrongpassword");
    }
    
    // 验证码登录成功
    SendTestVerifyCode(SmsScene::Login, "13800003004");
    auto result = auth_service_->LoginByCode("13800003004", GetTestVerifyCode());
    ASSERT_TRUE(result.IsOk());
    
    // 失败计数应该已清除，可以再次尝试密码登录
    for (int i = 0; i < config_->login.max_failed_attempts - 1; ++i) {
        auth_service_->LoginByPassword("13800003004", "wrongpassword");
    }
    
    // 还能成功登录（未被锁定）
    auto login_result = auth_service_->LoginByPassword("13800003004", "password123");
    EXPECT_TRUE(login_result.IsOk());
}

// ============================================================================
// ResetPassword 测试
// ============================================================================

TEST_F(AuthServiceTest, ResetPassword_Success) {
    CreateTestUser("13800004001", "oldpassword", "测试用户");
    SendTestVerifyCode(SmsScene::ResetPassword, "13800004001");
    ClearLoginFailure("13800004001");
    
    auto result = auth_service_->ResetPassword(
        "13800004001",
        GetTestVerifyCode(),
        "newpassword123"
    );
    
    ASSERT_TRUE(result.IsOk()) << "ResetPassword failed: " << result.message;
    
    // 验证新密码可以登录
    auto login_result = auth_service_->LoginByPassword("13800004001", "newpassword123");
    EXPECT_TRUE(login_result.IsOk()) << "Login with new password failed: " << login_result.message;
    
    // 验证旧密码不能登录
    auto old_login = auth_service_->LoginByPassword("13800004001", "oldpassword");
    EXPECT_FALSE(old_login.IsOk());
}

TEST_F(AuthServiceTest, ResetPassword_UserNotFound) {
    SendTestVerifyCode(SmsScene::ResetPassword, "13899999996");
    
    auto result = auth_service_->ResetPassword(
        "13899999996",
        GetTestVerifyCode(),
        "newpassword123"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(AuthServiceTest, ResetPassword_WrongVerifyCode) {
    CreateTestUser("13800004002", "oldpassword", "测试用户");
    SendTestVerifyCode(SmsScene::ResetPassword, "13800004002");
    
    auto result = auth_service_->ResetPassword(
        "13800004002",
        "999999",
        "newpassword123"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

TEST_F(AuthServiceTest, ResetPassword_InvalidNewPassword) {
    CreateTestUser("13800004003", "oldpassword", "测试用户");
    SendTestVerifyCode(SmsScene::ResetPassword, "13800004003");
    
    auto result = auth_service_->ResetPassword(
        "13800004003",
        GetTestVerifyCode(),
        "123"  // 太短
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(AuthServiceTest, ResetPassword_InvalidatesOldTokens) {
    CreateTestUser("13800004004", "oldpassword", "测试用户");
    ClearLoginFailure("13800004004");
    
    // 先登录获取 token
    auto login_result = auth_service_->LoginByPassword("13800004004", "oldpassword");
    ASSERT_TRUE(login_result.IsOk());
    std::string old_token = login_result.data.value().tokens.refresh_token;
    
    // 重置密码
    SendTestVerifyCode(SmsScene::ResetPassword, "13800004004");
    auto result = auth_service_->ResetPassword(
        "13800004004",
        GetTestVerifyCode(),
        "newpassword123"
    );
    ASSERT_TRUE(result.IsOk());
    
    // 旧 token 应该失效
    auto refresh_result = auth_service_->RefreshToken(old_token);
    EXPECT_FALSE(refresh_result.IsOk());
}

// ============================================================================
// RefreshToken 测试
// ============================================================================

TEST_F(AuthServiceTest, RefreshToken_Success) {
    CreateTestUser("13800005001", "password123", "测试用户");
    ClearLoginFailure("13800005001");
    
    auto login_result = auth_service_->LoginByPassword("13800005001", "password123");
    ASSERT_TRUE(login_result.IsOk()) << "Login failed: " << login_result.message;
    
    std::string old_refresh_token = login_result.data.value().tokens.refresh_token;
    
    // 等待一小段时间确保 token 不同
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto result = auth_service_->RefreshToken(old_refresh_token);
    
    ASSERT_TRUE(result.IsOk()) << "RefreshToken failed: " << result.message;
    EXPECT_FALSE(result.data.value().access_token.empty());
    EXPECT_FALSE(result.data.value().refresh_token.empty());
    EXPECT_NE(result.data.value().refresh_token, old_refresh_token);
}

TEST_F(AuthServiceTest, RefreshToken_OldTokenInvalidAfterRefresh) {
    CreateTestUser("13800005002", "password123", "测试用户");
    ClearLoginFailure("13800005002");
    
    auto login_result = auth_service_->LoginByPassword("13800005002", "password123");
    ASSERT_TRUE(login_result.IsOk());
    
    std::string old_refresh_token = login_result.data.value().tokens.refresh_token;
    
    // 刷新 token
    auto refresh_result = auth_service_->RefreshToken(old_refresh_token);
    ASSERT_TRUE(refresh_result.IsOk());
    
    // 旧 token 应该失效
    auto second_refresh = auth_service_->RefreshToken(old_refresh_token);
    EXPECT_FALSE(second_refresh.IsOk());
}

TEST_F(AuthServiceTest, RefreshToken_InvalidToken) {
    auto result = auth_service_->RefreshToken("invalid_token_string");
    
    ASSERT_FALSE(result.IsOk());
    // 可能是 TokenRevoked 或 TokenInvalid
    EXPECT_TRUE(result.code == ErrorCode::TokenRevoked || 
                result.code == ErrorCode::TokenInvalid);
    
    // 打印实际的错误码以便调试
    std::cout << "Actual error code: " << static_cast<int>(result.code) 
    << ", message: " << result.message << std::endl;
}

TEST_F(AuthServiceTest, RefreshToken_Empty) {
    auto result = auth_service_->RefreshToken("");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(AuthServiceTest, RefreshToken_UserDisabled) {
    CreateTestUser("13800005003", "password123", "测试用户");
    ClearLoginFailure("13800005003");
    
    // 登录
    auto login_result = auth_service_->LoginByPassword("13800005003", "password123");
    ASSERT_TRUE(login_result.IsOk());
    std::string refresh_token = login_result.data.value().tokens.refresh_token;
    
    // 禁用用户
    user_service_->SetUserDisabled("test_uuid_13800005003", true);
    
    // 刷新 token 应该失败
    auto result = auth_service_->RefreshToken(refresh_token);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

// ============================================================================
// Logout 测试
// ============================================================================

TEST_F(AuthServiceTest, Logout_Success) {
    CreateTestUser("13800006001", "password123", "测试用户");
    ClearLoginFailure("13800006001");
    
    auto login_result = auth_service_->LoginByPassword("13800006001", "password123");
    ASSERT_TRUE(login_result.IsOk());
    
    std::string refresh_token = login_result.data.value().tokens.refresh_token;
    
    auto result = auth_service_->Logout(refresh_token);
    
    ASSERT_TRUE(result.IsOk());
    
    // 验证 token 已失效
    auto refresh_result = auth_service_->RefreshToken(refresh_token);
    EXPECT_FALSE(refresh_result.IsOk());
}

TEST_F(AuthServiceTest, Logout_EmptyToken_Idempotent) {
    auto result = auth_service_->Logout("");
    
    ASSERT_TRUE(result.IsOk());  // 幂等性，空 token 也返回成功
}

TEST_F(AuthServiceTest, Logout_InvalidToken_Idempotent) {
    auto result = auth_service_->Logout("some_invalid_token");
    
    ASSERT_TRUE(result.IsOk());  // 幂等性
}

TEST_F(AuthServiceTest, Logout_Twice_Idempotent) {
    CreateTestUser("13800006002", "password123", "测试用户");
    ClearLoginFailure("13800006002");
    
    auto login_result = auth_service_->LoginByPassword("13800006002", "password123");
    ASSERT_TRUE(login_result.IsOk());
    
    std::string refresh_token = login_result.data.value().tokens.refresh_token;
    
    // 第一次登出
    auto result1 = auth_service_->Logout(refresh_token);
    ASSERT_TRUE(result1.IsOk());
    
    // 第二次登出（幂等）
    auto result2 = auth_service_->Logout(refresh_token);
    EXPECT_TRUE(result2.IsOk());
}

// ============================================================================
// LogoutAll 测试
// ============================================================================

TEST_F(AuthServiceTest, LogoutAll_Success) {
    CreateTestUser("13800007001", "password123", "测试用户");
    ClearLoginFailure("13800007001");
    
    // 模拟多次登录（多设备）
    auto login1 = auth_service_->LoginByPassword("13800007001", "password123");
    auto login2 = auth_service_->LoginByPassword("13800007001", "password123");
    ASSERT_TRUE(login1.IsOk());
    ASSERT_TRUE(login2.IsOk());
    
    std::string user_uuid = login1.data.value().user.uuid;
    std::string token1 = login1.data.value().tokens.refresh_token;
    std::string token2 = login2.data.value().tokens.refresh_token;
    
    // 全部登出
    auto result = auth_service_->LogoutAll(user_uuid);
    ASSERT_TRUE(result.IsOk());
    
    // 验证所有 token 都已失效
    EXPECT_FALSE(auth_service_->RefreshToken(token1).IsOk());
    EXPECT_FALSE(auth_service_->RefreshToken(token2).IsOk());
}

TEST_F(AuthServiceTest, LogoutAll_UserNotFound) {
    auto result = auth_service_->LogoutAll("non_existent_uuid");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

}  // namespace test
}  // namespace user_service
