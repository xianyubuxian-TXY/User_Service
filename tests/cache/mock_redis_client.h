// tests/cache/mock_redis_client.h

#pragma once

#include <gmock/gmock.h>
#include "cache/redis_client.h"

namespace user_service {
namespace testing {

/**
 * @brief RedisClient 的 Mock 实现
 * 
 * 使用类型别名解决 GMock 宏中逗号解析问题
 */
class MockRedisClient : public RedisClient {
public:
    // ==================== 类型别名（解决宏中逗号问题） ====================
    using OptionalString = std::optional<std::string>;
    using StringVector = std::vector<std::string>;
    using StringMap = std::unordered_map<std::string, std::string>;
    using StringPairVector = std::vector<std::pair<std::string, std::string>>;
    
    // Result 类型别名
    using VoidResult = Result<void>;
    using BoolResult = Result<bool>;
    using Int64Result = Result<int64_t>;
    using OptStringResult = Result<OptionalString>;
    using StringVectorResult = Result<StringVector>;
    using StringMapResult = Result<StringMap>;

    // 使用默认构造（不连接真实Redis）
    // 注意：需要修改基类或使用其他方式处理构造
    MockRedisClient() : RedisClient(CreateDummyConfig()) {}

    // ==================== 字符串操作 ====================
    MOCK_METHOD(VoidResult, Set, 
                (const std::string& key, const std::string& value), 
                (noexcept, override));
    
    MOCK_METHOD(VoidResult, SetPx, 
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl), 
                (noexcept, override));
    
    MOCK_METHOD(OptStringResult, Get, 
                (const std::string& key), 
                (noexcept, override));
    
    MOCK_METHOD(BoolResult, SetNx, 
                (const std::string& key, const std::string& value), 
                (noexcept, override));
    
    MOCK_METHOD(BoolResult, SetNxPx, 
                (const std::string& key, const std::string& value, std::chrono::milliseconds ttl), 
                (noexcept, override));

    // ==================== 通用操作 ====================
    MOCK_METHOD(BoolResult, Exists, 
                (const std::string& key), 
                (noexcept, override));
    
    MOCK_METHOD(BoolResult, Del, 
                (const std::string& key), 
                (noexcept, override));
    
    MOCK_METHOD(BoolResult, PExpire, 
                (const std::string& key, std::chrono::milliseconds ttl), 
                (noexcept, override));
    
    MOCK_METHOD(Int64Result, PTTL, 
                (const std::string& key), 
                (noexcept, override));
    
    MOCK_METHOD(StringVectorResult, Keys, 
                (const std::string& pattern), 
                (noexcept, override));
    
    MOCK_METHOD(StringVectorResult, Scan, 
                (const std::string& pattern, int64_t count), 
                (noexcept, override));

    // ==================== Hash 操作 ====================
    MOCK_METHOD(VoidResult, HSet, 
                (const std::string& key, const std::string& field, const std::string& value), 
                (noexcept, override));
    
    MOCK_METHOD(VoidResult, HMSet, 
                (const std::string& key, const StringPairVector& fields), 
                (noexcept, override));
    
    MOCK_METHOD(OptStringResult, HGet, 
                (const std::string& key, const std::string& field), 
                (noexcept, override));
    
    MOCK_METHOD(StringMapResult, HGetAll, 
                (const std::string& key), 
                (noexcept, override));
    
    MOCK_METHOD(BoolResult, HDel, 
                (const std::string& key, const std::string& field), 
                (noexcept, override));
    
    MOCK_METHOD(BoolResult, HExists, 
                (const std::string& key, const std::string& field), 
                (noexcept, override));

    // ==================== 原子操作 ====================
    MOCK_METHOD(Int64Result, Incr, 
                (const std::string& key), 
                (noexcept, override));
    
    MOCK_METHOD(Int64Result, IncrBy, 
                (const std::string& key, int64_t increment), 
                (noexcept, override));
    
    MOCK_METHOD(Int64Result, Decr, 
                (const std::string& key), 
                (noexcept, override));

    // ==================== 健康检查 ====================
    MOCK_METHOD(VoidResult, Ping, (), (noexcept, override));

private:
    // 创建一个假配置（Mock 不会真正连接）
    static RedisConfig CreateDummyConfig() {
        RedisConfig config;
        config.host = "127.0.0.1";
        config.port = 6379;
        config.password = "";
        config.db = 0;
        config.pool_size = 1;
        return config;
    }
};

}  // namespace testing
}  // namespace user_service
