// tests/service/auth_service_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "service/auth_service.h"
#include "mock_services.h"
#include "common/password_helper.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgReferee;
using ::testing::Invoke;
using ::testing::AnyNumber;

class AuthServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = TestDataFactory::CreateTestConfig();
        mock_user_db_ = std::make_shared<MockUserDB>();
        mock_redis_ = std::make_shared<MockRedisClient>();
        mock_token_repo_ = std::make_shared<MockTokenRepository>();
        mock_jwt_service_ = std::make_shared<MockJwtService>();
        
        // 创建真实的 SmsService（使用 mock redis）
        sms_service_ = std::make_shared<SmsService>(mock_redis_, config_->sms);
        
        auth_service_ = std::make_shared<AuthService>(
            config_,
            mock_user_db_,
            mock_redis_,
            mock_token_repo_,
            mock_jwt_service_,
            sms_service_
        );
    }

    std::shared_ptr<Config> config_;
    std::shared_ptr<MockUserDB> mock_user_db_;
    std::shared_ptr<MockRedisClient> mock_redis_;
    std::shared_ptr<MockTokenRepository> mock_token_repo_;
    std::shared_ptr<MockJwtService> mock_jwt_service_;
    std::shared_ptr<SmsService> sms_service_;
    std::shared_ptr<AuthService> auth_service_;
};

// ==================== SendVerifyCode 测试 ====================

TEST_F(AuthServiceTest, SendVerifyCode_Register_Success) {
    // 手机号未注册
    EXPECT_CALL(*mock_user_db_, ExistsByMobile("13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    // Redis 操作
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, SetPx(_, _, _))
        .WillRepeatedly(Return(Result<void>::Ok()));
    
    auto result = auth_service_->SendVerifyCode("13800138000", SmsScene::Register);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), config_->sms.send_interval_seconds);
}

TEST_F(AuthServiceTest, SendVerifyCode_Register_MobileAlreadyExists) {
    // 手机号已注册
    EXPECT_CALL(*mock_user_db_, ExistsByMobile("13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = auth_service_->SendVerifyCode("13800138000", SmsScene::Register);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::MobileTaken);
}

TEST_F(AuthServiceTest, SendVerifyCode_Login_UserNotFound) {
    // 手机号未注册
    EXPECT_CALL(*mock_user_db_, ExistsByMobile("13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    auto result = auth_service_->SendVerifyCode("13800138000", SmsScene::Login);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(AuthServiceTest, SendVerifyCode_InvalidMobile) {
    auto result = auth_service_->SendVerifyCode("invalid", SmsScene::Register);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

// ==================== Register 测试 ====================

TEST_F(AuthServiceTest, Register_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    auto test_tokens = TestDataFactory::CreateTestTokenPair();
    
    // SMS 验证成功
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    // 创建用户成功
    EXPECT_CALL(*mock_user_db_, Create(_))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 生成 Token
    EXPECT_CALL(*mock_jwt_service_, GenerateTokenPair(_))
        .WillOnce(Return(test_tokens));
    
    // 保存 Refresh Token
    EXPECT_CALL(*mock_token_repo_, SaveRefreshToken(_, _, _))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = auth_service_->Register(
        "13800138000", "123456", "Password123", "TestUser"
    );
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user.mobile, test_user.mobile);
    EXPECT_EQ(result.Value().tokens.access_token, test_tokens.access_token);
}

TEST_F(AuthServiceTest, Register_InvalidPassword) {
    auto result = auth_service_->Register(
        "13800138000", "123456", "weak", "TestUser"  // 密码太短
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(AuthServiceTest, Register_InvalidVerifyCode) {
    // SMS 验证失败
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("654321")));  // 错误的验证码
    EXPECT_CALL(*mock_redis_, Incr(_))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    EXPECT_CALL(*mock_redis_, PExpire(_, _))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = auth_service_->Register(
        "13800138000", "123456", "Password123", "TestUser"
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

TEST_F(AuthServiceTest, Register_MobileAlreadyExists) {
    // SMS 验证成功
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    // 创建用户失败 - 手机号已存在
    EXPECT_CALL(*mock_user_db_, Create(_))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::MobileTaken, "手机号已被使用")));
    
    auto result = auth_service_->Register(
        "13800138000", "123456", "Password123", "TestUser"
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::MobileTaken);
}

// ==================== LoginByPassword 测试 ====================

TEST_F(AuthServiceTest, LoginByPassword_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.password_hash = PasswordHelper::Hash("Password123");
    auto test_tokens = TestDataFactory::CreateTestTokenPair();
    
    // 无登录失败记录
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(std::nullopt)));
    
    // 查找用户
    EXPECT_CALL(*mock_user_db_, FindByMobile("13800138000"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 清除登录失败记录
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    // 生成 Token
    EXPECT_CALL(*mock_jwt_service_, GenerateTokenPair(_))
        .WillOnce(Return(test_tokens));
    
    // 保存 Refresh Token
    EXPECT_CALL(*mock_token_repo_, SaveRefreshToken(_, _, _))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = auth_service_->LoginByPassword("13800138000", "Password123");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user.uuid, test_user.uuid);
}

TEST_F(AuthServiceTest, LoginByPassword_WrongPassword) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.password_hash = PasswordHelper::Hash("Password123");
    
    // 无登录失败记录
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(std::nullopt)));
    
    // 查找用户
    EXPECT_CALL(*mock_user_db_, FindByMobile("13800138000"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 记录登录失败
    EXPECT_CALL(*mock_redis_, Incr(_))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    EXPECT_CALL(*mock_redis_, PTTL(_))
        .WillOnce(Return(Result<int64_t>::Ok(-2)));  // key 不存在
    EXPECT_CALL(*mock_redis_, PExpire(_, _))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = auth_service_->LoginByPassword("13800138000", "WrongPassword1");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::WrongPassword);
}

TEST_F(AuthServiceTest, LoginByPassword_UserNotFound) {
    // 无登录失败记录
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(std::nullopt)));
    
    // 用户不存在
    EXPECT_CALL(*mock_user_db_, FindByMobile("13800138000"))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserNotFound)));
    
    // 记录登录失败
    EXPECT_CALL(*mock_redis_, Incr(_))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    EXPECT_CALL(*mock_redis_, PTTL(_))
        .WillOnce(Return(Result<int64_t>::Ok(-2)));
    EXPECT_CALL(*mock_redis_, PExpire(_, _))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = auth_service_->LoginByPassword("13800138000", "Password123");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::WrongPassword);  // 故意返回密码错误，不泄露用户是否存在
}

TEST_F(AuthServiceTest, LoginByPassword_AccountLocked) {
    // 登录失败次数达到上限
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("5")));  // max_failed_attempts
    EXPECT_CALL(*mock_redis_, PTTL(_))
        .WillOnce(Return(Result<int64_t>::Ok(600000)));  // 10分钟
    
    auto result = auth_service_->LoginByPassword("13800138000", "Password123");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::AccountLocked);
}

TEST_F(AuthServiceTest, LoginByPassword_UserDisabled) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.password_hash = PasswordHelper::Hash("Password123");
    test_user.disabled = true;
    
    // 无登录失败记录
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(std::nullopt)));
    
    // 查找用户
    EXPECT_CALL(*mock_user_db_, FindByMobile("13800138000"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    auto result = auth_service_->LoginByPassword("13800138000", "Password123");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

// ==================== LoginByCode 测试 ====================

TEST_F(AuthServiceTest, LoginByCode_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    auto test_tokens = TestDataFactory::CreateTestTokenPair();
    
    // SMS 验证成功
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillRepeatedly(Return(Result<std::optional<std::string>>::Ok("123456")));
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    // 查找用户
    EXPECT_CALL(*mock_user_db_, FindByMobile("13800138000"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 生成 Token
    EXPECT_CALL(*mock_jwt_service_, GenerateTokenPair(_))
        .WillOnce(Return(test_tokens));
    
    // 保存 Refresh Token
    EXPECT_CALL(*mock_token_repo_, SaveRefreshToken(_, _, _))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = auth_service_->LoginByCode("13800138000", "123456");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user.uuid, test_user.uuid);
}

// ==================== RefreshToken 测试 ====================

TEST_F(AuthServiceTest, RefreshToken_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    auto test_tokens = TestDataFactory::CreateTestTokenPair();
    
    // 解析 Refresh Token
    EXPECT_CALL(*mock_jwt_service_, ParseRefreshToken("old_refresh_token"))
        .WillOnce(Return(Result<std::string>::Ok("1")));  // user_id
    
    // 查找用户
    EXPECT_CALL(*mock_user_db_, FindById(1))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 验证 Token 有效
    EXPECT_CALL(*mock_token_repo_, IsTokenValid(_))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    // 删除旧 Token
    EXPECT_CALL(*mock_token_repo_, DeleteByTokenHash(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 生成新 Token
    EXPECT_CALL(*mock_jwt_service_, GenerateTokenPair(_))
        .WillOnce(Return(test_tokens));
    
    // 保存新 Refresh Token
    EXPECT_CALL(*mock_token_repo_, SaveRefreshToken(_, _, _))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = auth_service_->RefreshToken("old_refresh_token");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().access_token, test_tokens.access_token);
}

TEST_F(AuthServiceTest, RefreshToken_InvalidToken) {
    EXPECT_CALL(*mock_jwt_service_, ParseRefreshToken("invalid_token"))
        .WillOnce(Return(Result<std::string>::Fail(ErrorCode::TokenInvalid)));
    
    auto result = auth_service_->RefreshToken("invalid_token");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(AuthServiceTest, RefreshToken_TokenRevoked) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_jwt_service_, ParseRefreshToken("revoked_token"))
        .WillOnce(Return(Result<std::string>::Ok("1")));
    
    EXPECT_CALL(*mock_user_db_, FindById(1))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // Token 已被撤销
    EXPECT_CALL(*mock_token_repo_, IsTokenValid(_))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    auto result = auth_service_->RefreshToken("revoked_token");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenRevoked);
}

// ==================== Logout 测试 ====================

TEST_F(AuthServiceTest, Logout_Success) {
    EXPECT_CALL(*mock_token_repo_, DeleteByTokenHash(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = auth_service_->Logout("refresh_token");
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(AuthServiceTest, Logout_EmptyToken) {
    // 空 Token 也返回成功（幂等性）
    auto result = auth_service_->Logout("");
    
    EXPECT_TRUE(result.IsOk());
}

// ==================== ResetPassword 测试 ====================

TEST_F(AuthServiceTest, ResetPassword_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    // SMS 验证成功
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    // 查找用户
    EXPECT_CALL(*mock_user_db_, FindByMobile("13800138000"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 更新密码
    EXPECT_CALL(*mock_user_db_, Update(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 撤销所有 Token
    EXPECT_CALL(*mock_token_repo_, DeleteByUserId(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = auth_service_->ResetPassword("13800138000", "123456", "NewPassword123");
    
    EXPECT_TRUE(result.IsOk());
}

// ==================== ValidateAccessToken 测试 ====================

TEST_F(AuthServiceTest, ValidateAccessToken_Success) {
    AccessTokenPayload payload;
    payload.user_id = 1;
    payload.user_uuid = "test-uuid";
    payload.mobile = "13800138000";
    payload.role = UserRole::User;
    payload.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);
    
    EXPECT_CALL(*mock_jwt_service_, VerifyAccessToken("valid_token"))
        .WillOnce(Return(Result<AccessTokenPayload>::Ok(payload)));
    
    auto result = auth_service_->ValidateAccessToken("valid_token");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user_uuid, "test-uuid");
}

TEST_F(AuthServiceTest, ValidateAccessToken_EmptyToken) {
    auto result = auth_service_->ValidateAccessToken("");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}
