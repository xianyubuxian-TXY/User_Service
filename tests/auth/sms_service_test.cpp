#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "auth/sms_service.h"
#include "mock_auth_deps.h"
#include "common/error_codes.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::HasSubstr;

// ============================================================================
// 测试夹具
// ============================================================================
class SmsServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_redis_ = std::make_shared<MockRedisClient>();
        
        SmsConfig config;
        config.code_len = 6;
        config.code_ttl_seconds = 300;
        config.send_interval_seconds = 60;
        config.max_retry_count = 3;
        config.retry_ttl_seconds = 300;
        config.lock_seconds = 600;
        
        sms_service_ = std::make_unique<SmsService>(mock_redis_, config);
    }
    
    std::shared_ptr<MockRedisClient> mock_redis_;
    std::unique_ptr<SmsService> sms_service_;
};

// ============================================================================
// SendCaptcha 测试
// ============================================================================

TEST_F(SmsServiceTest, SendCaptcha_Success) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, Exists("sms:interval:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, SetPx("sms:code:register:13800138000", _, _))
        .WillOnce(Return(Result<void>::Ok()));
    
    EXPECT_CALL(*mock_redis_, SetPx("sms:interval:13800138000", "1", _))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 60);
}

TEST_F(SmsServiceTest, SendCaptcha_Locked) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, PTTL("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<int64_t>::Ok(300000)));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::RateLimited);
}

TEST_F(SmsServiceTest, SendCaptcha_IntervalLimit) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, Exists("sms:interval:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, PTTL("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<int64_t>::Ok(30000)));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::RateLimited);
}

TEST_F(SmsServiceTest, SendCaptcha_RedisError_CheckLock) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Fail(ErrorCode::ServiceUnavailable, "Redis error")));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::ServiceUnavailable);
}

TEST_F(SmsServiceTest, SendCaptcha_RedisError_StoreFailed) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Exists("sms:interval:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, SetPx("sms:code:register:13800138000", _, _))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::ServiceUnavailable, "Redis error")));
    
    auto result = sms_service_->SendCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::ServiceUnavailable);
}

// ============================================================================
// VerifyCaptcha 测试
// ============================================================================

TEST_F(SmsServiceTest, VerifyCaptcha_Success) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, Get("sms:code:register:13800138000"))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    
    // 修正：Del 返回 Result<bool>
    EXPECT_CALL(*mock_redis_, Del("sms:verify_count:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, "13800138000", "123456");
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(SmsServiceTest, VerifyCaptcha_WrongCode) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, Get("sms:code:register:13800138000"))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    
    EXPECT_CALL(*mock_redis_, Incr("sms:verify_count:register:13800138000"))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    
    // 修正：PExpire 返回 Result<bool>
    EXPECT_CALL(*mock_redis_, PExpire("sms:verify_count:register:13800138000", _))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, "13800138000", "000000");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
    EXPECT_THAT(result.message, HasSubstr("还剩2次机会"));
}

TEST_F(SmsServiceTest, VerifyCaptcha_Expired) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, Get("sms:code:register:13800138000"))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok(std::nullopt)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, "13800138000", "123456");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaExpired);
}

TEST_F(SmsServiceTest, VerifyCaptcha_Locked) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, PTTL("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<int64_t>::Ok(300000)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, "13800138000", "123456");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::RateLimited);
}

TEST_F(SmsServiceTest, VerifyCaptcha_MaxRetryExceeded_TriggerLock) {
    EXPECT_CALL(*mock_redis_, Exists("sms:lock:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    EXPECT_CALL(*mock_redis_, Get("sms:code:register:13800138000"))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    
    EXPECT_CALL(*mock_redis_, Incr("sms:verify_count:register:13800138000"))
        .WillOnce(Return(Result<int64_t>::Ok(3)));
    
    // 修正：PExpire 返回 Result<bool>
    EXPECT_CALL(*mock_redis_, PExpire(_, _))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, SetPx("sms:lock:register:13800138000", "1", _))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 修正：Del 返回 Result<bool>
    EXPECT_CALL(*mock_redis_, Del("sms:code:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    EXPECT_CALL(*mock_redis_, Del("sms:verify_count:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->VerifyCaptcha(SmsScene::Register, "13800138000", "wrong");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::AccountLocked);
}

// ============================================================================
// ConsumeCaptcha 测试
// ============================================================================

TEST_F(SmsServiceTest, ConsumeCaptcha_Success) {
    // 修正：Del 返回 Result<bool>
    EXPECT_CALL(*mock_redis_, Del("sms:code:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = sms_service_->ConsumeCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_TRUE(result.IsOk());
}

// 补充：测试 key 不存在时的删除（幂等性）
TEST_F(SmsServiceTest, ConsumeCaptcha_KeyNotExist_StillOk) {
    // key 不存在，Del 返回 false，但不是错误
    EXPECT_CALL(*mock_redis_, Del("sms:code:register:13800138000"))
        .WillOnce(Return(Result<bool>::Ok(false)));
    
    auto result = sms_service_->ConsumeCaptcha(SmsScene::Register, "13800138000");
    
    EXPECT_TRUE(result.IsOk());  // 应该仍然成功
}
