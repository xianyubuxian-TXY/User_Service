#pragma once

#include <gmock/gmock.h>
#include "cache/redis_client.h"
#include "auth/token_repository.h"

namespace user_service::testing {

// ============================================================================
// 类型别名 - 解决 MOCK_METHOD 宏中的逗号问题
// ============================================================================
using StringPairVector = std::vector<std::pair<std::string, std::string>>;
using StringStringMap = std::unordered_map<std::string, std::string>;

// ============================================================================
// Mock Redis Client
// ============================================================================
class MockRedisClient : public RedisClient {
public:
    MockRedisClient() : RedisClient(CreateDummyConfig()) {}
    
private:
    static RedisConfig CreateDummyConfig() {
        RedisConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 6379;
        cfg.password = "";
        cfg.db = 0;
        return cfg;
    }
    
public:
    // 字符串操作
    MOCK_METHOD(Result<void>, Set, 
                (const std::string& key, const std::string& value), 
                (noexcept, override));
    
    MOCK_METHOD(Result<void>, SetPx,
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl),
                (noexcept, override));
    
    MOCK_METHOD((Result<std::optional<std::string>>), Get,
                (const std::string& key),
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, SetNx,
                (const std::string& key, const std::string& value),
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, SetNxPx,
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl),
                (noexcept, override));

    // 通用操作
    MOCK_METHOD(Result<bool>, Exists,
                (const std::string& key),
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, Del,
                (const std::string& key),
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, PExpire,
                (const std::string& key, std::chrono::milliseconds ttl),
                (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, PTTL,
                (const std::string& key),
                (noexcept, override));
    
    MOCK_METHOD((Result<std::vector<std::string>>), Keys,
                (const std::string& pattern),
                (noexcept, override));
    
    MOCK_METHOD((Result<std::vector<std::string>>), Scan,
                (const std::string& pattern, int64_t count),
                (noexcept, override));

    // Hash 操作 - 使用类型别名避免逗号问题
    MOCK_METHOD(Result<void>, HSet,
                (const std::string& key, const std::string& field, const std::string& value),
                (noexcept, override));
    
    // 关键修复：使用类型别名 StringPairVector
    MOCK_METHOD(Result<void>, HMSet,
                (const std::string& key, const StringPairVector& fields),
                (noexcept, override));
    
    MOCK_METHOD((Result<std::optional<std::string>>), HGet,
                (const std::string& key, const std::string& field),
                (noexcept, override));
    
    // 关键修复：使用类型别名 StringStringMap
    MOCK_METHOD((Result<StringStringMap>), HGetAll,
                (const std::string& key),
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, HDel,
                (const std::string& key, const std::string& field),
                (noexcept, override));
    
    MOCK_METHOD(Result<bool>, HExists,
                (const std::string& key, const std::string& field),
                (noexcept, override));

    // 原子操作
    MOCK_METHOD(Result<int64_t>, Incr,
                (const std::string& key),
                (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, IncrBy,
                (const std::string& key, int64_t increment),
                (noexcept, override));
    
    MOCK_METHOD(Result<int64_t>, Decr,
                (const std::string& key),
                (noexcept, override));

    // 健康检查
    MOCK_METHOD(Result<void>, Ping, (), (noexcept, override));
};

// ============================================================================
// Mock Token Repository
// ============================================================================
class MockTokenRepository : public TokenRepository {
public:
    MockTokenRepository() : TokenRepository(nullptr) {}
    
    MOCK_METHOD(Result<void>, SaveRefreshToken,
                (int64_t user_id, const std::string& token_hash, int expires_in_seconds),
                (override));
    
    MOCK_METHOD(Result<TokenSession>, FindByTokenHash,
                (const std::string& token_hash),
                (override));
    
    MOCK_METHOD(Result<bool>, IsTokenValid,
                (const std::string& token_hash),
                (override));
    
    MOCK_METHOD(Result<int64_t>, CountActiveSessionsByUserId,
                (int64_t user_id),
                (override));
    
    MOCK_METHOD(Result<void>, DeleteByTokenHash,
                (const std::string& token_hash),
                (override));
    
    MOCK_METHOD(Result<void>, DeleteByUserId,
                (int64_t user_id),
                (override));
    
    MOCK_METHOD(Result<int64_t>, CleanExpiredTokens, (), (override));
};

}  // namespace user_service::testing
