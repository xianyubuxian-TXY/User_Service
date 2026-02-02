#include "cache/redis_client.h"
#include "common/logger.h"
#include <iostream>

namespace user_service {


// 类型别名（避免宏参数中的逗号问题）
namespace{
    using StringMap = std::unordered_map<std::string, std::string>;
}



// ============================================================================
// 辅助宏：统一错误处理
// ============================================================================

#define REDIS_CATCH_RETURN(ResultType, operation, key)                         \
    catch (const sw::redis::Error& e) {                                        \
        LOG_WARN("Redis {} failed: key={}, err={}", operation, key, e.what()); \
        return ResultType::Fail(ErrorCode::ServiceUnavailable, e.what());      \
    }

#define REDIS_CATCH_RETURN_2(ResultType, operation, key, field)                \
    catch (const sw::redis::Error& e) {                                        \
        LOG_WARN("Redis {} failed: key={}, field={}, err={}",                  \
                 operation, key, field, e.what());                             \
        return ResultType::Fail(ErrorCode::ServiceUnavailable, e.what());      \
    }

// ============================================================================
// 构造函数
// ============================================================================

RedisClient::RedisClient(const std::string& host, int port,
                         const std::string& password, int db) {
    sw::redis::ConnectionOptions conn_opt;
    conn_opt.host = host;
    conn_opt.port = port;

    if (!password.empty()) {
        conn_opt.password = password;
    }

    conn_opt.db = db;
    
    sw::redis::ConnectionPoolOptions pool_opt;
    pool_opt.size = 5;
    pool_opt.wait_timeout = std::chrono::milliseconds(100);

    try {
        redis_ = std::make_unique<sw::redis::Redis>(conn_opt, pool_opt);
        std::cout << "✓ Redis 连接成功" << std::endl;
    } catch (const sw::redis::Error& e) {
        std::cerr << "✗ Redis 连接失败: " << e.what() << std::endl;
        LOG_ERROR("Redis connection failed: {}", e.what());
        throw;  // 构造函数是唯一允许抛异常的地方
    }
}

RedisClient::RedisClient(const RedisConfig& config) {
    sw::redis::ConnectionOptions conn_opt;
    conn_opt.host = config.host;
    conn_opt.port = config.port;
    
    if (!config.password.empty()) {
        conn_opt.password = config.password;
    }
    
    conn_opt.db = config.db;
    
    if (config.connect_timeout_ms) {
        conn_opt.connect_timeout = std::chrono::milliseconds(*config.connect_timeout_ms);
    }
    if (config.socket_timeout_ms) {
        conn_opt.socket_timeout = std::chrono::milliseconds(*config.socket_timeout_ms);
    }

    sw::redis::ConnectionPoolOptions pool_opt;
    pool_opt.size = config.pool_size;
    pool_opt.wait_timeout = std::chrono::milliseconds(config.wait_timeout_ms);

    try {
        redis_ = std::make_unique<sw::redis::Redis>(conn_opt, pool_opt);
        std::cout << "✓ Redis 连接成功" << std::endl;
    } catch (const sw::redis::Error& e) {
        std::cerr << "✗ Redis 连接失败: " << e.what() << std::endl;
        LOG_ERROR("Redis connection failed: {}", e.what());
        throw;
    }
}

// ============================================================================
// 字符串操作
// ============================================================================

Result<void> RedisClient::Set(const std::string& key, const std::string& value) noexcept {
    try {
        redis_->set(key, value);
        return Result<void>::Ok();
    }
    REDIS_CATCH_RETURN(Result<void>, "SET", key)
}

Result<void> RedisClient::SetPx(const std::string& key, const std::string& value,
                                 std::chrono::milliseconds ttl) noexcept {
    if (ttl.count() <= 0) {
        LOG_WARN("Redis PSETEX: invalid ttl={}ms for key={}", ttl.count(), key);
        return Result<void>::Fail(ErrorCode::InvalidArgument, "TTL must be positive");
    }

    try {
        redis_->psetex(key, ttl, value);
        return Result<void>::Ok();
    }
    REDIS_CATCH_RETURN(Result<void>, "PSETEX", key)
}

Result<std::optional<std::string>> RedisClient::Get(const std::string& key) noexcept {
    try {
        // redis_->get() 返回 std::optional<std::string>
        // 无论有值还是无值，都是"执行成功"
        auto val = redis_->get(key);
        return Result<std::optional<std::string>>::Ok(std::move(val));
    }
    REDIS_CATCH_RETURN(Result<std::optional<std::string>>, "GET", key)
}

Result<bool> RedisClient::SetNx(const std::string& key, const std::string& value) noexcept {
    try {
        bool set = redis_->setnx(key, value);
        return Result<bool>::Ok(set);
    }
    REDIS_CATCH_RETURN(Result<bool>, "SETNX", key)
}

Result<bool> RedisClient::SetNxPx(const std::string& key, const std::string& value,
                                   std::chrono::milliseconds ttl) noexcept {
    if (ttl.count() <= 0) {
        return Result<bool>::Fail(ErrorCode::InvalidArgument, "TTL must be positive");
    }

    try {
        // SET key value NX PX milliseconds
        bool set = redis_->set(key, value, ttl, sw::redis::UpdateType::NOT_EXIST);
        return Result<bool>::Ok(set);
    }
    REDIS_CATCH_RETURN(Result<bool>, "SET NX PX", key)
}

// ============================================================================
// 通用操作
// ============================================================================

Result<bool> RedisClient::Exists(const std::string& key) noexcept {
    try {
        bool exists = redis_->exists(key) > 0;
        return Result<bool>::Ok(exists);
    }
    REDIS_CATCH_RETURN(Result<bool>, "EXISTS", key)
}

Result<bool> RedisClient::Del(const std::string& key) noexcept {
    try {
        // 返回是否真的删除了（key 不存在时返回 false，但这不是错误）
        bool deleted = redis_->del(key) > 0;
        return Result<bool>::Ok(deleted);
    }
    REDIS_CATCH_RETURN(Result<bool>, "DEL", key)
}

Result<bool> RedisClient::PExpire(const std::string& key, 
                                   std::chrono::milliseconds ttl) noexcept {
    try {
        bool set = redis_->pexpire(key, ttl);
        return Result<bool>::Ok(set);
    }
    REDIS_CATCH_RETURN(Result<bool>, "PEXPIRE", key)
}

Result<int64_t> RedisClient::PTTL(const std::string& key) noexcept {
    try {
        // pttl 返回：
        // -2: key 不存在
        // -1: key 存在但无过期时间
        // >0: 剩余毫秒数
        long long ttl = redis_->pttl(key);
        return Result<int64_t>::Ok(static_cast<int64_t>(ttl));
    }
    REDIS_CATCH_RETURN(Result<int64_t>, "PTTL", key)
}

Result<std::vector<std::string>> RedisClient::Keys(const std::string& pattern) noexcept {
    try {
        std::vector<std::string> keys;
        redis_->keys(pattern, std::back_inserter(keys));
        return Result<std::vector<std::string>>::Ok(std::move(keys));
    } catch (const sw::redis::Error& e) {
        LOG_WARN("Redis KEYS failed: pattern={}, err={}", pattern, e.what());
        return Result<std::vector<std::string>>::Fail(ErrorCode::ServiceUnavailable, e.what());
    }
}

Result<std::vector<std::string>> RedisClient::Scan(const std::string& pattern,
                                                    int64_t count) noexcept {
    try {
        std::vector<std::string> keys;
        long long cursor = 0;
        
        // SCAN 是增量迭代，需要循环直到 cursor 返回 0
        do {
            cursor = redis_->scan(cursor, pattern, count, std::back_inserter(keys));
        } while (cursor != 0);
        
        return Result<std::vector<std::string>>::Ok(std::move(keys));
    } catch (const sw::redis::Error& e) {
        LOG_WARN("Redis SCAN failed: pattern={}, err={}", pattern, e.what());
        return Result<std::vector<std::string>>::Fail(ErrorCode::ServiceUnavailable, e.what());
    }
}

// ============================================================================
// Hash 操作
// ============================================================================

Result<void> RedisClient::HSet(const std::string& key,
                                const std::string& field,
                                const std::string& value) noexcept {
    try {
        redis_->hset(key, field, value);
        return Result<void>::Ok();
    }
    REDIS_CATCH_RETURN_2(Result<void>, "HSET", key, field)
}

Result<void> RedisClient::HMSet(
    const std::string& key,
    const std::vector<std::pair<std::string, std::string>>& fields) noexcept {
    
    if (fields.empty()) {
        return Result<void>::Ok();  // 空操作视为成功
    }

    try {
        redis_->hset(key, fields.begin(), fields.end());
        return Result<void>::Ok();
    }
    REDIS_CATCH_RETURN(Result<void>, "HMSET", key)
}

Result<std::optional<std::string>> RedisClient::HGet(const std::string& key,
                                                      const std::string& field) noexcept {
    try {
        auto val = redis_->hget(key, field);
        return Result<std::optional<std::string>>::Ok(std::move(val));
    }
    REDIS_CATCH_RETURN_2(Result<std::optional<std::string>>, "HGET", key, field)
}

Result<StringMap> RedisClient::HGetAll(const std::string& key) noexcept {
    try {
        std::unordered_map<std::string, std::string> result;
        redis_->hgetall(key, std::inserter(result, result.begin()));
        // key 不存在时返回空 map，这是正常情况
        return Result<std::unordered_map<std::string, std::string>>::Ok(std::move(result));
    }
    REDIS_CATCH_RETURN(Result<StringMap>, "HGETALL",key)
}

Result<bool> RedisClient::HDel(const std::string& key, const std::string& field) noexcept {
    try {
        bool deleted = redis_->hdel(key, field) > 0;
        return Result<bool>::Ok(deleted);
    }
    REDIS_CATCH_RETURN_2(Result<bool>, "HDEL", key, field)
}

Result<bool> RedisClient::HExists(const std::string& key, const std::string& field) noexcept {
    try {
        bool exists = redis_->hexists(key, field);
        return Result<bool>::Ok(exists);
    }
    REDIS_CATCH_RETURN_2(Result<bool>, "HEXISTS", key, field)
}

// ============================================================================
// 原子操作
// ============================================================================

Result<int64_t> RedisClient::Incr(const std::string& key) noexcept {
    try {
        long long val = redis_->incr(key);
        return Result<int64_t>::Ok(static_cast<int64_t>(val));
    }
    REDIS_CATCH_RETURN(Result<int64_t>, "INCR", key)
}

Result<int64_t> RedisClient::IncrBy(const std::string& key, int64_t increment) noexcept {
    try {
        long long val = redis_->incrby(key, increment);
        return Result<int64_t>::Ok(static_cast<int64_t>(val));
    } catch (const sw::redis::Error& e) {
        LOG_WARN("Redis INCRBY failed: key={}, increment={}, err={}", 
                 key, increment, e.what());
        return Result<int64_t>::Fail(ErrorCode::ServiceUnavailable, e.what());
    }
}

Result<int64_t> RedisClient::Decr(const std::string& key) noexcept {
    try {
        long long val = redis_->decr(key);
        return Result<int64_t>::Ok(static_cast<int64_t>(val));
    }
    REDIS_CATCH_RETURN(Result<int64_t>, "DECR", key)
}

// ============================================================================
// 健康检查
// ============================================================================

Result<void> RedisClient::Ping() noexcept {
    try {
        redis_->ping();
        return Result<void>::Ok();
    } catch (const sw::redis::Error& e) {
        LOG_WARN("Redis PING failed: {}", e.what());
        return Result<void>::Fail(ErrorCode::ServiceUnavailable, e.what());
    }
}

#undef REDIS_CATCH_RETURN
#undef REDIS_CATCH_RETURN_2

} // namespace user_service
