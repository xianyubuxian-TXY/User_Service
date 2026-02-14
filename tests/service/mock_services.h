// tests/service/mock_services.h

#pragma once

#include <gmock/gmock.h>
#include "db/user_db.h"
#include "auth/token_repository.h"
#include "auth/jwt_service.h"
#include "auth/sms_service.h"
#include "cache/redis_client.h"

namespace user_service {
namespace testing {

// ==================== Mock UserDB ====================
class MockUserDB : public UserDB {
public:
    MockUserDB() : UserDB(nullptr) {}
    
    MOCK_METHOD(Result<UserEntity>, Create, (const UserEntity& user), (override));
    MOCK_METHOD(Result<UserEntity>, FindById, (int64_t id), (override));
    MOCK_METHOD(Result<UserEntity>, FindByUUID, (const std::string& uuid), (override));
    MOCK_METHOD(Result<UserEntity>, FindByMobile, (const std::string& mobile), (override));
    MOCK_METHOD(Result<void>, Update, (const UserEntity& user), (override));
    MOCK_METHOD(Result<void>, Delete, (int64_t id), (override));
    MOCK_METHOD(Result<void>, DeleteByUUID, (const std::string& uuid), (override));
    MOCK_METHOD(Result<bool>, ExistsByMobile, (const std::string& mobile), (override));
    MOCK_METHOD(Result<std::vector<UserEntity>>, FindAll, (const UserQueryParams& params), (override));
    MOCK_METHOD(Result<int64_t>, Count, (const UserQueryParams& params), (override));
};

// ==================== Mock TokenRepository ====================
class MockTokenRepository : public TokenRepository {
public:
    MockTokenRepository() : TokenRepository(nullptr) {}
    
    MOCK_METHOD(Result<void>, SaveRefreshToken, 
                (int64_t user_id, const std::string& token_hash, int expires_in_seconds), (override));
    MOCK_METHOD(Result<TokenSession>, FindByTokenHash, (const std::string& token_hash), (override));
    MOCK_METHOD(Result<bool>, IsTokenValid, (const std::string& token_hash), (override));
    MOCK_METHOD(Result<int64_t>, CountActiveSessionsByUserId, (int64_t user_id), (override));
    MOCK_METHOD(Result<void>, DeleteByTokenHash, (const std::string& token_hash), (override));
    MOCK_METHOD(Result<void>, DeleteByUserId, (int64_t user_id), (override));
    MOCK_METHOD(Result<int64_t>, CleanExpiredTokens, (), (override));
};

// ==================== Mock JwtService ====================
class MockJwtService : public JwtService {
public:
    MockJwtService() : JwtService(SecurityConfig{}) {}
    
    MOCK_METHOD(TokenPair, GenerateTokenPair, (const UserEntity& user), (override));
    MOCK_METHOD(Result<AccessTokenPayload>, VerifyAccessToken, (const std::string& token), (override));
    MOCK_METHOD(Result<std::string>, ParseRefreshToken, (const std::string& token), (override));
};

// ==================== Mock SmsService ====================
class MockSmsService {
public:
    MOCK_METHOD(Result<int32_t>, SendCaptcha, (SmsScene scene, const std::string& mobile));
    MOCK_METHOD(Result<void>, VerifyCaptcha, 
                (SmsScene scene, const std::string& mobile, const std::string& code));
    MOCK_METHOD(Result<void>, ConsumeCaptcha, (SmsScene scene, const std::string& mobile));
};

// 包装类，用于注入 Mock
class MockSmsServiceWrapper : public SmsService {
public:
    MockSmsServiceWrapper(std::shared_ptr<RedisClient> redis, const SmsConfig& config)
        : SmsService(redis, config) {}
    
    MockSmsService mock;
    
    Result<int32_t> SendCaptcha(SmsScene scene, const std::string& mobile) override {
        return mock.SendCaptcha(scene, mobile);
    }
    
    Result<void> VerifyCaptcha(SmsScene scene, const std::string& mobile, 
                               const std::string& code) override {
        return mock.VerifyCaptcha(scene, mobile, code);
    }
    
    Result<void> ConsumeCaptcha(SmsScene scene, const std::string& mobile) override {
        return mock.ConsumeCaptcha(scene, mobile);
    }
};

// ==================== Mock RedisClient ====================
class MockRedisClient : public RedisClient {
public:
    MockRedisClient() : RedisClient() {}
    
    MOCK_METHOD(Result<void>, Set, (const std::string& key, const std::string& value), (noexcept, override));
    MOCK_METHOD(Result<void>, SetPx, 
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl), 
                (noexcept, override));
    MOCK_METHOD(Result<std::optional<std::string>>, Get, (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<bool>, Exists, (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<bool>, Del, (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<bool>, PExpire, 
                (const std::string& key, std::chrono::milliseconds ttl), (noexcept, override));
    MOCK_METHOD(Result<int64_t>, PTTL, (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<int64_t>, Incr, (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<void>, Ping, (), (noexcept, override));
};

// ==================== 测试数据工厂 ====================
class TestDataFactory {
public:
    static UserEntity CreateTestUser(int64_t id = 1) {
        UserEntity user;
        user.id = id;
        user.uuid = "test-uuid-" + std::to_string(id);
        user.mobile = "1380013800" + std::to_string(id % 10);
        user.display_name = "TestUser" + std::to_string(id);
        user.password_hash = "$sha256$salt$hash";
        user.role = UserRole::User;
        user.disabled = false;
        user.created_at = "2024-01-01 00:00:00";
        user.updated_at = "2024-01-01 00:00:00";
        return user;
    }
    
    static TokenPair CreateTestTokenPair() {
        return TokenPair{
            .access_token = "test_access_token",
            .refresh_token = "test_refresh_token",
            .expires_in = 900
        };
    }
    
    static std::shared_ptr<Config> CreateTestConfig() {
        auto config = std::make_shared<Config>();
        config->security.jwt_secret = "test-secret-key-at-least-32-bytes!";
        config->security.jwt_issuer = "test-issuer";
        config->security.access_token_ttl_seconds = 900;
        config->security.refresh_token_ttl_seconds = 604800;
        config->sms.code_len = 6;
        config->sms.code_ttl_seconds = 300;
        config->sms.send_interval_seconds = 60;
        config->sms.max_retry_count = 5;
        config->sms.retry_ttl_seconds = 300;
        config->sms.lock_seconds = 1800;
        config->login.max_failed_attempts = 5;
        config->login.failed_attempts_window = 900;
        config->login.lock_duration_seconds = 1800;
        config->password.min_length = 8;
        config->password.max_length = 32;
        config->password.require_digit = true;
        return config;
    }
};

}  // namespace testing
}  // namespace user_service
