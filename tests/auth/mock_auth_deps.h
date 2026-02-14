// tests/auth/mock_auth_deps.h

#pragma once

#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <optional>
#include <chrono>

#include "cache/redis_client.h"
#include "auth/token_repository.h"
#include "auth/jwt_service.h"
#include "common/result.h"
#include "common/auth_type.h"
#include "entity/token.h"
#include "entity/user_entity.h"

namespace user_service {
namespace testing {

// ============================================================================
// Mock RedisClient
// ============================================================================
class MockRedisClient : public RedisClient {
public:
    MockRedisClient() : RedisClient("localhost", 6379, "", 0) {}

    MOCK_METHOD(Result<void>, Set, 
                (const std::string& key, const std::string& value), (noexcept, override));
    
    MOCK_METHOD(Result<void>, SetPx, 
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl), 
                (noexcept, override));
    
    MOCK_METHOD(Result<std::optional<std::string>>, Get, 
                (const std::string& key), (noexcept, override));
    
    MOCK_METHOD(Result<bool>, SetNx, 
                (const std::string& key, const std::string& value), (noexcept, override));
    
    MOCK_METHOD(Result<bool>, SetNxPx, 
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl), 
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, Exists, 
                (const std::string& key), (noexcept, override));
    
    MOCK_METHOD(Result<bool>, Del, 
                (const std::string& key), (noexcept, override));
    
    MOCK_METHOD(Result<bool>, PExpire, 
                (const std::string& key, std::chrono::milliseconds ttl), (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, PTTL, 
                (const std::string& key), (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, Incr, 
                (const std::string& key), (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, IncrBy, 
                (const std::string& key, int64_t increment), (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, Decr, 
                (const std::string& key), (noexcept, override));
    
    MOCK_METHOD(Result<void>, Ping, (), (noexcept, override));
};

// ============================================================================
// Mock TokenRepository
// ============================================================================
class MockTokenRepository : public TokenRepository {
public:
    MockTokenRepository() : TokenRepository(nullptr) {}

    MOCK_METHOD(Result<void>, SaveRefreshToken,
                (int64_t user_id, const std::string& token_hash, int expires_in_seconds),
                (override));

    MOCK_METHOD(Result<TokenSession>, FindByTokenHash,
                (const std::string& token_hash), (override));

    MOCK_METHOD(Result<bool>, IsTokenValid,
                (const std::string& token_hash), (override));

    MOCK_METHOD(Result<int64_t>, CountActiveSessionsByUserId,
                (int64_t user_id), (override));

    MOCK_METHOD(Result<void>, DeleteByTokenHash,
                (const std::string& token_hash), (override));

    MOCK_METHOD(Result<void>, DeleteByUserId,
                (int64_t user_id), (override));

    MOCK_METHOD(Result<int64_t>, CleanExpiredTokens, (), (override));
};

// ============================================================================
// Mock JwtService
// ============================================================================
class MockJwtService : public JwtService {
public:
    MockJwtService() : JwtService(SecurityConfig{}) {}

    MOCK_METHOD(TokenPair, GenerateTokenPair,
                (const UserEntity& user), (override));

    MOCK_METHOD(Result<AccessTokenPayload>, VerifyAccessToken,
                (const std::string& token), (override));

    MOCK_METHOD(Result<std::string>, ParseRefreshToken,
                (const std::string& token), (override));
};

// ============================================================================
// 测试辅助函数
// ============================================================================

/// 创建测试用的 UserEntity
inline UserEntity CreateTestUser(int64_t id = 1, 
                                  const std::string& uuid = "test-uuid-123",
                                  const std::string& mobile = "13800138000") {
    UserEntity user;
    user.id = id;
    user.uuid = uuid;
    user.mobile = mobile;
    user.display_name = "Test User";
    user.password_hash = "$sha256$salt$hash";
    user.role = UserRole::User;
    user.disabled = false;
    user.created_at = "2024-01-01 00:00:00";
    user.updated_at = "2024-01-01 00:00:00";
    return user;
}

/// 创建测试用的 TokenPair
inline TokenPair CreateTestTokenPair() {
    return TokenPair{
        .access_token = "test_access_token_xxx",
        .refresh_token = "test_refresh_token_xxx",
        .expires_in = 900
    };
}

/// 创建测试用的 SecurityConfig
inline SecurityConfig CreateTestSecurityConfig() {
    SecurityConfig config;
    config.jwt_secret = "test-secret-key-at-least-32-bytes-long!";
    config.jwt_issuer = "test-issuer";
    config.access_token_ttl_seconds = 900;      // 15 分钟
    config.refresh_token_ttl_seconds = 604800;  // 7 天
    return config;
}

/// 创建测试用的 SmsConfig
inline SmsConfig CreateTestSmsConfig() {
    SmsConfig config;
    config.code_len = 6;
    config.code_ttl_seconds = 300;
    config.send_interval_seconds = 60;
    config.max_retry_count = 5;
    config.retry_ttl_seconds = 300;
    config.lock_seconds = 1800;
    return config;
}

}  // namespace testing
}  // namespace user_service
