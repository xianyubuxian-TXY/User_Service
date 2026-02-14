// tests/server/mock_dependencies.h

#pragma once

#include <gmock/gmock.h>
#include <memory>
#include <string>

#include "cache/redis_client.h"
#include "db/user_db.h"
#include "auth/token_repository.h"
#include "auth/jwt_service.h"
#include "auth/sms_service.h"
#include "service/auth_service.h"
#include "service/user_service.h"
#include "discovery/zk_client.h"
#include "discovery/service_registry.h"

namespace user_service {
namespace testing {

// ============================================================================
// Mock Redis Client
// ============================================================================

class MockRedisClient : public RedisClient {
public:
    MockRedisClient() : RedisClient() {}
    
    MOCK_METHOD(Result<void>, Set, 
        (const std::string& key, const std::string& value), (noexcept, override));
    MOCK_METHOD(Result<void>, SetPx, 
        (const std::string& key, const std::string& value, std::chrono::milliseconds ttl), 
        (noexcept, override));
    MOCK_METHOD(Result<std::optional<std::string>>, Get, 
        (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<bool>, Exists, 
        (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<bool>, Del, 
        (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<void>, Ping, (), (noexcept, override));
    MOCK_METHOD(Result<int64_t>, Incr, 
        (const std::string& key), (noexcept, override));
    MOCK_METHOD(Result<bool>, PExpire, 
        (const std::string& key, std::chrono::milliseconds ttl), (noexcept, override));
    MOCK_METHOD(Result<int64_t>, PTTL, 
        (const std::string& key), (noexcept, override));
};

// ============================================================================
// Mock ZooKeeper Client
// ============================================================================

class MockZooKeeperClient : public ZooKeeperClient {
public:
    MockZooKeeperClient() : ZooKeeperClient("localhost:2181", 5000) {}
    
    MOCK_METHOD(bool, Connect, (int timeout_ms), (override));
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(bool, IsConnected, (), (const, override));
    MOCK_METHOD(bool, CreateNode, 
        (const std::string& path, const std::string& data, bool ephemeral), (override));
    MOCK_METHOD(bool, CreateServicePath, (const std::string& path), (override));
    MOCK_METHOD(bool, DeleteNode, (const std::string& path), (override));
    MOCK_METHOD(bool, Exists, (const std::string& path), (override));
    MOCK_METHOD(bool, SetData, 
        (const std::string& path, const std::string& data), (override));
    MOCK_METHOD(std::string, GetData, (const std::string& path), (override));
    MOCK_METHOD(std::vector<std::string>, GetChildren, 
        (const std::string& path), (override));
    MOCK_METHOD(void, WatchChildren, 
        (const std::string& path, WatchCallback callback), (override));
    MOCK_METHOD(void, UnwatchChildren, (const std::string& path), (override));
};

// ============================================================================
// Mock Token Repository
// ============================================================================

class MockTokenRepository : public TokenRepository {
public:
    MockTokenRepository() : TokenRepository(nullptr) {}
    
    MOCK_METHOD(Result<void>, SaveRefreshToken,
        (int64_t user_id, const std::string& token_hash, int expires_in_seconds), (override));
    MOCK_METHOD(Result<TokenSession>, FindByTokenHash, 
        (const std::string& token_hash), (override));
    MOCK_METHOD(Result<bool>, IsTokenValid, 
        (const std::string& token_hash), (override));
    MOCK_METHOD(Result<int64_t>, CountActiveSessionsByUserId, 
        (int64_t user_id), (override));
    MOCK_METHOD(Result<void>, DeleteByTokenHash, 
        (const std::string& token_hash), (override));
    MOCK_METHOD(Result<void>, DeleteByUserId, (int64_t user_id), (override));
    MOCK_METHOD(Result<int64_t>, CleanExpiredTokens, (), (override));
};

// ============================================================================
// Mock JWT Service
// ============================================================================

class MockJwtService : public JwtService {
public:
    MockJwtService() : JwtService(SecurityConfig{}) {}
    
    MOCK_METHOD(TokenPair, GenerateTokenPair, (const UserEntity& user), (override));
    MOCK_METHOD(Result<AccessTokenPayload>, VerifyAccessToken, 
        (const std::string& token), (override));
    MOCK_METHOD(Result<std::string>, ParseRefreshToken, 
        (const std::string& token), (override));
};

// ============================================================================
// Mock Auth Service
// ============================================================================

class MockAuthService : public AuthService {
public:
    MockAuthService() 
        : AuthService(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}
    
    MOCK_METHOD(Result<int32_t>, SendVerifyCode, 
        (const std::string& mobile, SmsScene scene), (override));
    MOCK_METHOD(Result<AuthResult>, Register,
        (const std::string& mobile, const std::string& verify_code,
         const std::string& password, const std::string& display_name), (override));
    MOCK_METHOD(Result<AuthResult>, LoginByPassword,
        (const std::string& mobile, const std::string& password), (override));
    MOCK_METHOD(Result<AuthResult>, LoginByCode,
        (const std::string& mobile, const std::string& verify_code), (override));
    MOCK_METHOD(Result<void>, ResetPassword,
        (const std::string& mobile, const std::string& verify_code,
         const std::string& new_password), (override));
    MOCK_METHOD(Result<TokenPair>, RefreshToken, 
        (const std::string& refresh_token), (override));
    MOCK_METHOD(Result<void>, Logout, 
        (const std::string& refresh_token), (override));
    MOCK_METHOD(Result<void>, LogoutAll, 
        (const std::string& user_uuid), (override));
    MOCK_METHOD(Result<TokenValidationResult>, ValidateAccessToken, 
        (const std::string& access_token), (override));
};

// ============================================================================
// Mock User Service
// ============================================================================

class MockUserService : public UserService {
public:
    MockUserService() : UserService(nullptr, nullptr, nullptr, nullptr) {}
    
    MOCK_METHOD(Result<UserEntity>, GetCurrentUser, 
        (const std::string& user_uuid), (override));
    MOCK_METHOD(Result<UserEntity>, UpdateUser,
        (const std::string& user_uuid, std::optional<std::string> display_name), (override));
    MOCK_METHOD(Result<void>, ChangePassword,
        (const std::string& user_uuid, const std::string& old_password,
         const std::string& new_password), (override));
    MOCK_METHOD(Result<void>, DeleteUser,
        (const std::string& user_uuid, const std::string verify_code), (override));
    MOCK_METHOD(Result<UserEntity>, GetUser, 
        (const std::string& user_uuid), (override));
    MOCK_METHOD(Result<ListUsersResult>, ListUsers,
        (std::optional<std::string> mobile_filter, std::optional<bool> disabled_filter,
         int32_t page, int32_t page_size), (override));
    MOCK_METHOD(Result<void>, SetUserDisabled,
        (const std::string& user_uuid, bool disabled), (override));
};

// ============================================================================
// Helper Functions
// ============================================================================

/// 创建测试用 UserEntity
inline UserEntity CreateTestUser(int64_t id = 1) {
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

/// 创建测试用 TokenPair
inline TokenPair CreateTestTokenPair() {
    TokenPair tokens;
    tokens.access_token = "test-access-token";
    tokens.refresh_token = "test-refresh-token";
    tokens.expires_in = 900;
    return tokens;
}

/// 创建测试用 AuthResult
inline AuthResult CreateTestAuthResult(int64_t user_id = 1) {
    AuthResult result;
    result.user = CreateTestUser(user_id);
    result.tokens = CreateTestTokenPair();
    return result;
}

}  // namespace testing
}  // namespace user_service
