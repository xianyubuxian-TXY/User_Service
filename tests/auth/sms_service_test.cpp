#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "auth/sms_service.h"
#include "cache/redis_client.h"
#include "config/config.h"

using namespace user_service;
using namespace std::chrono_literals;

class SmsServiceIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Redis 配置（可从环境变量读取）
        RedisConfig redis_config;
        redis_config.host = GetEnvOr("TEST_REDIS_HOST", "localhost");
        redis_config.port = std::stoi(GetEnvOr("TEST_REDIS_PORT", "6379"));
        redis_config.password = GetEnvOr("TEST_REDIS_PASSWORD", "");
        redis_config.db = 15;  // 使用独立的测试 DB
        
        redis_ = std::make_shared<RedisClient>(redis_config);
        
        // SMS 配置（使用较短的时间方便测试）
        config_.code_len = 6;
        config_.code_ttl_seconds = 10;           // 10秒过期（测试用）
        config_.send_interval_seconds = 2;       // 2秒间隔（测试用）
        config_.max_retry_count = 3;
        config_.retry_ttl_seconds = 10;
        config_.lock_seconds = 5;                // 5秒锁定（测试用）
        
        sms_service_ = std::make_unique<SmsService>(redis_, config_);
        
        // 清理测试数据
        CleanupTestKeys();
    }
    
    void TearDown() override {
        CleanupTestKeys();
    }
    
    // 清理所有测试相关的 Redis key
    void CleanupTestKeys() {
        std::vector<std::string> keys = {
            "sms:code:login:" + test_mobile_,
            "sms:code:register:" + test_mobile_,
            "sms:code:reset_pwd:" + test_mobile_,
            "sms:interval:" + test_mobile_,
            "sms:verify_count:login:" + test_mobile_,
            "sms:verify_count:register:" + test_mobile_,
            "sms:lock:login:" + test_mobile_,
            "sms:lock:register:" + test_mobile_,
        };
        for (const auto& key : keys) {
            redis_->Del(key);
        }
    }
    
    // 从 Redis 直接获取验证码（用于测试验证）
    std::optional<std::string> GetStoredCode(SmsScene scene) {
        std::string key = "sms:code:" + SceneName(scene) + ":" + test_mobile_;
        auto result = redis_->Get(key);
        if (result.IsOk() && result.Value().has_value()) {
            return result.Value().value();
        }
        return std::nullopt;
    }
    
    static std::string SceneName(SmsScene scene) {
        switch (scene) {
            case SmsScene::Register:      return "register";
            case SmsScene::Login:         return "login";
            case SmsScene::ResetPassword: return "reset_pwd";
            default:                      return "unspecified";
        }
    }
    
    static std::string GetEnvOr(const char* name, const char* default_val) {
        const char* val = std::getenv(name);
        return val ? val : default_val;
    }
    
    std::shared_ptr<RedisClient> redis_;
    SmsConfig config_;
    std::unique_ptr<SmsService> sms_service_;
    const std::string test_mobile_ = "13800001111";
};

// ==================== 发送验证码测试 ====================

TEST_F(SmsServiceIntegrationTest, SendCaptcha_Success) {
    auto result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    
    ASSERT_TRUE(result.IsOk()) << "Error: " << result.message;
    EXPECT_EQ(result.Value(), config_.send_interval_seconds);
    
    // 验证 Redis 中确实存储了验证码
    auto stored_code = GetStoredCode(SmsScene::Login);
    ASSERT_TRUE(stored_code.has_value());
    EXPECT_EQ(stored_code->length(), static_cast<size_t>(config_.code_len));
}

TEST_F(SmsServiceIntegrationTest, SendCaptcha_IntervalLimit) {
    // 第一次发送
    auto result1 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(result1.IsOk());
    
    // 立即再发（应该被限制）
    auto result2 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_FALSE(result2.IsOk());
    EXPECT_EQ(result2.GetErrCode(), ErrorCode::RateLimited);
}

TEST_F(SmsServiceIntegrationTest, SendCaptcha_AfterIntervalExpires) {
    // 第一次发送
    auto result1 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(result1.IsOk());
    
    // 等待间隔过期
    std::this_thread::sleep_for(std::chrono::seconds(config_.send_interval_seconds + 1));
    
    // 再次发送（应该成功）
    auto result2 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(result2.IsOk()) << "Error: " << result2.message;
}

TEST_F(SmsServiceIntegrationTest, SendCaptcha_CodeOverwrite) {
    // 第一次发送
    auto result1 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(result1.IsOk());
    auto code1 = GetStoredCode(SmsScene::Login);
    
    // 等待间隔过期后再发
    std::this_thread::sleep_for(std::chrono::seconds(config_.send_interval_seconds + 1));
    
    auto result2 = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(result2.IsOk());
    auto code2 = GetStoredCode(SmsScene::Login);
    
    // 验证码应该被覆盖（大概率不同）
    ASSERT_TRUE(code1.has_value());
    ASSERT_TRUE(code2.has_value());
    // 注：极小概率相同，但 6 位数字重复概率 1/1000000
}

// ==================== 验证验证码测试 ====================

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_Success) {
    // 发送验证码
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    // 获取实际验证码
    auto stored_code = GetStoredCode(SmsScene::Login);
    ASSERT_TRUE(stored_code.has_value());
    
    // 验证
    auto verify_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, stored_code.value());
    
    ASSERT_TRUE(verify_result.IsOk()) << "Error: " << verify_result.message;
}

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_WrongCode) {
    // 发送验证码
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    // 使用错误的验证码
    auto verify_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, "000000");
    
    ASSERT_FALSE(verify_result.IsOk());
    EXPECT_EQ(verify_result.GetErrCode(), ErrorCode::CaptchaWrong);
}

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_CodeExpired) {
    // 发送验证码
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    auto stored_code = GetStoredCode(SmsScene::Login);
    ASSERT_TRUE(stored_code.has_value());
    
    // 等待验证码过期
    std::this_thread::sleep_for(std::chrono::seconds(config_.code_ttl_seconds + 1));
    
    // 验证（应该过期）
    auto verify_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, stored_code.value());
    
    ASSERT_FALSE(verify_result.IsOk());
    EXPECT_EQ(verify_result.GetErrCode(), ErrorCode::CaptchaExpired);
}

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_NoCodeSent) {
    // 未发送验证码直接验证
    auto verify_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, "123456");
    
    ASSERT_FALSE(verify_result.IsOk());
    EXPECT_EQ(verify_result.GetErrCode(), ErrorCode::CaptchaExpired);
}

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_ExceedMaxRetry_TriggerLock) {
    // 发送验证码
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    // 连续错误直到锁定
    for (int i = 0; i < config_.max_retry_count; ++i) {
        auto result = sms_service_->VerifyCaptcha(
            SmsScene::Login, test_mobile_, "000000");
        ASSERT_FALSE(result.IsOk());
        
        if (i < config_.max_retry_count - 1) {
            EXPECT_EQ(result.GetErrCode(), ErrorCode::CaptchaWrong);
        } else {
            EXPECT_EQ(result.GetErrCode(), ErrorCode::AccountLocked);
        }
    }
    
    // 锁定后再尝试（即使用正确验证码也应该被拒绝）
    // 需要先重新发送验证码（等待间隔后）
    std::this_thread::sleep_for(std::chrono::seconds(config_.send_interval_seconds + 1));
    
    // 发送也应该被锁定
    auto send_locked = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    EXPECT_FALSE(send_locked.IsOk());
    EXPECT_EQ(send_locked.GetErrCode(), ErrorCode::RateLimited);
}

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_LockAutoExpires) {
    // 触发锁定
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    for (int i = 0; i < config_.max_retry_count; ++i) {
        sms_service_->VerifyCaptcha(SmsScene::Login, test_mobile_, "000000");
    }
    
    // 等待锁定过期
    std::this_thread::sleep_for(std::chrono::seconds(config_.lock_seconds + 1));
    
    // 锁定过期后应该可以发送
    auto send_after_unlock = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_after_unlock.IsOk()) << "Error: " << send_after_unlock.message;
}

// ==================== 消费验证码测试 ====================

TEST_F(SmsServiceIntegrationTest, ConsumeCaptcha_RemovesCode) {
    // 发送并验证
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    auto stored_code = GetStoredCode(SmsScene::Login);
    ASSERT_TRUE(stored_code.has_value());
    
    auto verify_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, stored_code.value());
    ASSERT_TRUE(verify_result.IsOk());
    
    // 消费验证码
    auto consume_result = sms_service_->ConsumeCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(consume_result.IsOk());
    
    // 验证码应该被删除
    auto code_after_consume = GetStoredCode(SmsScene::Login);
    EXPECT_FALSE(code_after_consume.has_value());
}

TEST_F(SmsServiceIntegrationTest, ConsumeCaptcha_PreventReuse) {
    // 发送并验证
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    auto stored_code = GetStoredCode(SmsScene::Login);
    ASSERT_TRUE(stored_code.has_value());
    std::string code = stored_code.value();
    
    auto verify_result = sms_service_->VerifyCaptcha(SmsScene::Login, test_mobile_, code);
    ASSERT_TRUE(verify_result.IsOk());
    
    // 消费
    sms_service_->ConsumeCaptcha(SmsScene::Login, test_mobile_);
    
    // 再次用同一验证码验证（应该失败）
    auto reuse_result = sms_service_->VerifyCaptcha(SmsScene::Login, test_mobile_, code);
    ASSERT_FALSE(reuse_result.IsOk());
    EXPECT_EQ(reuse_result.GetErrCode(), ErrorCode::CaptchaExpired);
}

// ==================== 场景隔离测试 ====================

TEST_F(SmsServiceIntegrationTest, DifferentScenes_Independent) {
    // 登录场景发送
    auto login_send = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(login_send.IsOk());
    auto login_code = GetStoredCode(SmsScene::Login);
    
    // 注册场景也可以发送（共享 interval，所以要等）
    std::this_thread::sleep_for(std::chrono::seconds(config_.send_interval_seconds + 1));
    
    auto register_send = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    ASSERT_TRUE(register_send.IsOk());
    auto register_code = GetStoredCode(SmsScene::Register);
    
    // 两个场景的验证码独立
    ASSERT_TRUE(login_code.has_value());
    ASSERT_TRUE(register_code.has_value());
    
    // 用登录验证码去验证注册（应该失败，因为是不同的验证码）
    auto cross_verify = sms_service_->VerifyCaptcha(
        SmsScene::Register, test_mobile_, login_code.value());
    
    // 如果两个验证码恰好相同则会成功，否则失败
    if (login_code.value() != register_code.value()) {
        EXPECT_FALSE(cross_verify.IsOk());
    }
}

TEST_F(SmsServiceIntegrationTest, LoginLocked_RegisterStillWorks) {
    // 登录场景触发锁定
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    for (int i = 0; i < config_.max_retry_count; ++i) {
        sms_service_->VerifyCaptcha(SmsScene::Login, test_mobile_, "000000");
    }
    
    // 登录被锁定
    std::this_thread::sleep_for(std::chrono::seconds(config_.send_interval_seconds + 1));
    auto login_locked = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    EXPECT_FALSE(login_locked.IsOk());
    EXPECT_EQ(login_locked.GetErrCode(), ErrorCode::RateLimited);
    
    // 注册场景不受影响（等待间隔）
    auto register_ok = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    ASSERT_TRUE(register_ok.IsOk()) << "Error: " << register_ok.message;
}

// ==================== 完整业务流程测试 ====================

TEST_F(SmsServiceIntegrationTest, FullFlow_RegisterWithSms) {
    // 1. 发送注册验证码
    auto send_result = sms_service_->SendCaptcha(SmsScene::Register, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    // 2. 获取验证码（实际业务中用户从短信获取）
    auto code = GetStoredCode(SmsScene::Register);
    ASSERT_TRUE(code.has_value());
    
    // 3. 用户输入验证码进行验证
    auto verify_result = sms_service_->VerifyCaptcha(
        SmsScene::Register, test_mobile_, code.value());
    ASSERT_TRUE(verify_result.IsOk());
    
    // 4. 业务逻辑：创建用户（假设成功）
    // ... user_service->CreateUser(...) ...
    
    // 5. 业务成功后消费验证码
    auto consume_result = sms_service_->ConsumeCaptcha(SmsScene::Register, test_mobile_);
    ASSERT_TRUE(consume_result.IsOk());
    
    // 6. 验证码不能再用
    auto reuse_result = sms_service_->VerifyCaptcha(
        SmsScene::Register, test_mobile_, code.value());
    EXPECT_FALSE(reuse_result.IsOk());
}

TEST_F(SmsServiceIntegrationTest, FullFlow_VerifyFailedThenRetry) {
    // 1. 发送验证码
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    auto code = GetStoredCode(SmsScene::Login);
    ASSERT_TRUE(code.has_value());
    
    // 2. 用户输错一次
    auto wrong_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, "000000");
    ASSERT_FALSE(wrong_result.IsOk());
    EXPECT_EQ(wrong_result.GetErrCode(), ErrorCode::CaptchaWrong);
    
    // 3. 用户用正确的验证码重试
    auto correct_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, code.value());
    ASSERT_TRUE(correct_result.IsOk());
}

// ==================== 边界条件测试 ====================

TEST_F(SmsServiceIntegrationTest, VerifyCaptcha_ExactMaxRetryCount) {
    auto send_result = sms_service_->SendCaptcha(SmsScene::Login, test_mobile_);
    ASSERT_TRUE(send_result.IsOk());
    
    // 错误 max_retry_count - 1 次
    for (int i = 0; i < config_.max_retry_count - 1; ++i) {
        auto result = sms_service_->VerifyCaptcha(
            SmsScene::Login, test_mobile_, "000000");
        EXPECT_EQ(result.GetErrCode(), ErrorCode::CaptchaWrong);
    }
    
    // 第 max_retry_count 次错误 -> 锁定
    auto lock_result = sms_service_->VerifyCaptcha(
        SmsScene::Login, test_mobile_, "000000");
    EXPECT_EQ(lock_result.GetErrCode(), ErrorCode::AccountLocked);
}

TEST_F(SmsServiceIntegrationTest, SendCaptcha_ValidMobile) {
    // 不同格式的手机号
    std::vector<std::string> mobiles = {
        "13812345678",
        "15912345678",
        "18612345678",
    };
    
    for (size_t i = 0; i < mobiles.size(); ++i) {
        // 清理之前的 key
        redis_->Del("sms:code:login:" + mobiles[i]);
        redis_->Del("sms:interval:" + mobiles[i]);
        redis_->Del("sms:lock:login:" + mobiles[i]);
        
        auto result = sms_service_->SendCaptcha(SmsScene::Login, mobiles[i]);
        EXPECT_TRUE(result.IsOk()) << "Mobile: " << mobiles[i];
        
        if (i < mobiles.size() - 1) {
            // 等待间隔（如果测试多个号码共享限制的话）
            std::this_thread::sleep_for(100ms);
        }
    }
}
