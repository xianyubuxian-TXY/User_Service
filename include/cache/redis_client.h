#pragma once

#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <unordered_map>
#include "config/config.h"
#include "common/result.h"

namespace user_service {

// ============================================================================
// Redis 客户端
// 
// 设计原则：
//   - 使用 Result<T> 区分"执行失败"与"数据不存在"
//   - 所有操作都是 noexcept，通过 Result 返回错误信息
//   - Result 成功 + optional 无值 = key 不存在（正常业务情况）
//   - Result 失败 = Redis 执行出错（网络/超时等）
//
// 使用示例：
//   auto result = redis->Get("user:123");
//   if (!result.IsOk()) {
//       // Redis 执行失败
//       LOG_ERROR("Redis error: {}", result.message);
//   } else if (result.Value().has_value()) {
//       // Key 存在，获取到值
//       auto& value = result.Value().value();
//   } else {
//       // Key 不存在（正常情况）
//   }
// ============================================================================
class RedisClient {
public:
    /// @brief 构造函数（详细参数版本）
    RedisClient(const std::string& host, int port,
                const std::string& password, int db);
    
    /// @brief 构造函数（配置结构体版本）
    explicit RedisClient(const RedisConfig& config);

    // ==================== 字符串操作 ====================
    
    /// @brief 设置键值对（无过期时间）
    /// @return 成功返回 Ok，失败返回错误信息
    Result<void> Set(const std::string& key, const std::string& value) noexcept;
    
    /// @brief 设置键值对 + 过期时间（毫秒）
    /// @param ttl 过期时间（毫秒），必须 > 0
    /// @return 成功返回 Ok，失败返回错误信息
    Result<void> SetPx(const std::string& key, const std::string& value,
                       std::chrono::milliseconds ttl) noexcept;
    
    /// @brief 获取值
    /// @return 成功+有值=存在，成功+无值=不存在，失败=Redis错误
    Result<std::optional<std::string>> Get(const std::string& key) noexcept;

    /// @brief 仅当 key 不存在时设置（分布式锁常用）
    /// @return 成功时 bool 表示是否设置成功（true=设置了，false=key已存在）
    Result<bool> SetNx(const std::string& key, const std::string& value) noexcept;

    /// @brief SETNX + PEXPIRE 原子操作（分布式锁推荐）
    Result<bool> SetNxPx(const std::string& key, const std::string& value,
                         std::chrono::milliseconds ttl) noexcept;

    // ==================== 通用操作 ====================
    
    /// @brief 判断键是否存在
    /// @return 成功时 bool 表示是否存在
    Result<bool> Exists(const std::string& key) noexcept;
    
    /// @brief 删除键
    /// @return 成功时 bool 表示是否真的删除了（false=key本不存在）
    Result<bool> Del(const std::string& key) noexcept;
    
    /// @brief 设置键的过期时间（毫秒）
    /// @return 成功时 bool 表示是否设置成功（false=key不存在）
    Result<bool> PExpire(const std::string& key, std::chrono::milliseconds ttl) noexcept;

    /// @brief 获取键的剩余过期时间（毫秒）
    /// @return 成功时返回 int64_t：>0=剩余毫秒，-1=无过期时间，-2=key不存在
    Result<int64_t> PTTL(const std::string& key) noexcept;

    /// @brief 获取匹配的所有 Key（KEYS 命令）——>（开发调试）
    /// @param pattern 匹配模式，如 "user:*"、"sms:code:*" 等
    /// @warning 生产环境慎用！大数据量时会阻塞 Redis
    /// @return 成功时返回匹配的 key 列表
    Result<std::vector<std::string>> Keys(const std::string& pattern) noexcept;

    /// @brief 增量迭代获取 Key（SCAN 命令，推荐）
    /// @param pattern 匹配模式
    /// @param count 每次迭代建议返回的数量（仅为提示，实际可能不同）
    /// @return 成功时返回匹配的 key 列表
    Result<std::vector<std::string>> Scan(const std::string& pattern, 
                                          int64_t count = 100) noexcept;


    // ==================== Hash 操作 ====================
    
    /// @brief 设置 Hash 字段
    Result<void> HSet(const std::string& key, 
                      const std::string& field,
                      const std::string& value) noexcept;
    
    /// @brief 批量设置 Hash 字段
    Result<void> HMSet(const std::string& key,
                       const std::vector<std::pair<std::string, std::string>>& fields) noexcept;
    
    /// @brief 获取 Hash 字段值
    /// @return 成功+有值=存在，成功+无值=字段不存在
    Result<std::optional<std::string>> HGet(const std::string& key,
                                            const std::string& field) noexcept;
    
    /// @brief 获取 Hash 所有字段
    /// @return 成功时返回 map（key不存在时为空map，这是正常情况）
    Result<std::unordered_map<std::string, std::string>> HGetAll(const std::string& key) noexcept;
    
    /// @brief 删除 Hash 字段
    /// @return 成功时 bool 表示是否真的删除了
    Result<bool> HDel(const std::string& key, const std::string& field) noexcept;

    /// @brief 判断 Hash 字段是否存在
    Result<bool> HExists(const std::string& key, const std::string& field) noexcept;

    // ==================== 原子操作 ====================
    
    /// @brief 自增 1（key不存在时从0开始）
    /// @return 成功时返回自增后的值
    Result<int64_t> Incr(const std::string& key) noexcept;
    
    /// @brief 自增指定值（可为负数）
    /// @return 成功时返回自增后的值
    Result<int64_t> IncrBy(const std::string& key, int64_t increment) noexcept;

    /// @brief 自减 1
    Result<int64_t> Decr(const std::string& key) noexcept;

    // ==================== 健康检查 ====================
    
    /// @brief Ping 测试连接
    Result<void> Ping() noexcept;

private:
    std::unique_ptr<sw::redis::Redis> redis_;
};

} // namespace user_service
