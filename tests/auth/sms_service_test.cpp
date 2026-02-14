// tests/auth/sms_service_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "auth/sms_service.h"
#include "mock_auth_deps.h"

using ::testing::_;
using ::testing::Return;
using ::testing::AnyOf;
using ::testing::HasSubstr;

namespace user_service {
namespace testing {

class SmsServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_redis_ = std::make_shared<MockRedisClient>();
        config_ = CreateTestSmsConfig();
        sms_service_ = std::make_unique<SmsService>(mock_redis_, config_);
    }

    std::shared_ptr<MockRedisClient> mock_redis_;
    SmsConfig config_;
    std::unique_ptr<SmsService> sms_service_;
    
    const std::string test_mobile_ = "13800138000";
};

// ============================================================================
// SendCaptcha 测试
// ============================================================================

TEST_F(SmsServiceTest, SendCaptcha_Success) {
    // 设置 Mock 期望
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));  // 未被锁定，未在间隔期内
    
    EXPECT_CALL(*mock_redis_, SetPx(_, _, _))
        .WillRepeatedly(Return(Result<void>::Ok()));  // 存储验证码和间隔成功
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    
    ASSERT_TRUE(result.IsOk()) << "Error: " << result.message;
    EXPECT_EQ(result.Value(), config_.send_interval_seconds);
}

TEST_F(SmsServiceTest, SendCaptcha_AlreadyLocked) {
    // 模拟已被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, PTTL(_))
        .WillOnce(Return(Result<int64_t>::Ok(60000)));  // 60秒
    
    auto result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::RateLimited);
}

TEST_F(SmsServiceTest, SendCaptcha_WithinInterval) {
    // 未被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    // 在发送间隔内
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("interval")))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, PTTL(_))
        .WillOnce(Return(Result<int64_t>::Ok(30000)));  // 30秒
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::RateLimited);
}

TEST_F(SmsServiceTest, SendCaptcha_RedisError) {
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillOnce(Return(Result<bool>::Fail(ErrorCode::ServiceUnavailable, "Redis error")));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::ServiceUnavailable);
}

TEST_F(SmsServiceTest, SendCaptcha_StoreCodeFailed) {
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    
    // 存储验证码失败
    EXPECT_CALL(*mock_redis_, SetPx(HasSubstr("code"), _, _))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::ServiceUnavailable, "Store failed")));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::ServiceUnavailable);
}

TEST_F(SmsServiceTest, SendCaptcha_DifferentScenes) {
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, SetPx(_, _, _))
        .WillRepeatedly(Return(Result<void>::Ok()));
    
    // 不同场景都应该能发送
    auto result1 = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    auto result2 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    auto result3 = sms_service_->SendCaptcha(SmsScene::ResetPassword, test_mobile_);
    auto result4 = sms_service_->SendCaptcha(SmsScene::DeleteUser, test_mobile_);
    
    EXPECT_TRUE(result1.IsOk());
    EXPECT_TRUE(result2.IsOk());
    EXPECT_TRUE(result3.IsOk());
    EXPECT_TRUE(result4.IsOk());
}

// ============================================================================
// VerifyCaptcha 测试
// ============================================================================

TEST_F(SmsServiceTest, VerifyCaptcha_Success) {
    const std::string correct_code = "123456";
    
    // 未被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    // 返回正确的验证码
    EXPECT_CALL(*mock_redis_, Get(HasSubstr("code")))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(correct_code)));
    
    // 清除错误计数
    EXPECT_CALL(*mock_redis_, Del(HasSubstr("verify_count")))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, test_mobile_, correct_code);
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(SmsServiceTest, VerifyCaptcha_WrongCode) {
    // 未被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    // 返回正确的验证码
    EXPECT_CALL(*mock_redis_, Get(HasSubstr("code")))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    
    // 增加错误计数
    EXPECT_CALL(*mock_redis_, Incr(_))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    
    EXPECT_CALL(*mock_redis_, PExpire(_, _))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, test_mobile_, "999999");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

TEST_F(SmsServiceTest, VerifyCaptcha_CodeExpired) {
    // 未被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    // 验证码不存在（已过期）
    EXPECT_CALL(*mock_redis_, Get(HasSubstr("code")))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(std::nullopt)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, test_mobile_, "123456");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaExpired);
}

TEST_F(SmsServiceTest, VerifyCaptcha_AccountLocked) {
    // 已被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, PTTL(_))
        .WillOnce(Return(Result<int64_t>::Ok(1800000)));  // 30分钟
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Login, test_mobile_, "123456");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::RateLimited);
}

TEST_F(SmsServiceTest, VerifyCaptcha_TriggerLock) {
    // 未被锁定
    EXPECT_CALL(*mock_redis_, Exists(HasSubstr("lock")))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    // 返回正确的验证码
    EXPECT_CALL(*mock_redis_, Get(HasSubstr("code")))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    
    // 错误次数达到上限
    EXPECT_CALL(*mock_redis_, Incr(_))
        .WillOnce(Return(Result<int64_t>::Ok(config_.max_retry_count)));
    
    // 触发锁定
    EXPECT_CALL(*mock_redis_, SetPx(HasSubstr("lock"), _, _))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 删除验证码和计数器
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Login, test_mobile_, "wrong_code");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::AccountLocked);
}

// ============================================================================
// ConsumeCaptcha 测试
// ============================================================================

TEST_F(SmsServiceTest, ConsumeCaptcha_Success) {
    EXPECT_CALL(*mock_redis_, Del(HasSubstr("code")))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->ConsumeCaptcha(SmsScene::Register, test_mobile_);
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(SmsServiceTest, ConsumeCaptcha_CodeNotExists) {
    // 即使验证码不存在，消费也应该成功（幂等性）
    EXPECT_CALL(*mock_redis_, Del(HasSubstr("code")))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    auto result = sms_service_->ConsumeCaptcha(SmsScene::Register, test_mobile_);
    
    EXPECT_TRUE(result.IsOk());
}

}  // namespace testing
}  // namespace user_service
