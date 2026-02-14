// tests/cache/redis_client_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <thread>
#include <random>

#include "cache/redis_client.h"
#include "common/error_codes.h"
#include "config/config.h"

using namespace user_service;
using namespace std::chrono_literals;

// ============================================================================
// 测试夹具：集成测试（需要真实 Redis）
// ============================================================================

class RedisClientIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 从环境变量或默认值获取 Redis 配置
        RedisConfig config;
        config.host = GetEnvOr("REDIS_HOST", "localhost");
        config.port = std::stoi(GetEnvOr("REDIS_PORT", "6380"));
        config.password = GetEnvOr("REDIS_PASSWORD", "");
        config.db = 15;  // 使用 db 15 进行测试，避免影响其他数据
        config.pool_size = 3;
        
        try {
            redis_ = std::make_unique<RedisClient>(config);
            
            // ★ 关键修复：立即验证连接是否真正成功
            auto ping_result = redis_->Ping();
            if (!ping_result.IsOk()) {
                GTEST_SKIP() << "无法连接到 Redis (" 
                             << config.host << ":" << config.port 
                             << "): " << ping_result.message;
            }
            
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Redis 初始化失败: " << e.what();
        }
        
        // 清理测试数据
        CleanupTestKeys();
    }

    void TearDown() override {
        if (redis_) {
            CleanupTestKeys();
        }
    }

    // 生成唯一的测试 key，避免并发测试冲突
    std::string TestKey(const std::string& suffix = "") {
        return "test:" + test_prefix_ + ":" + suffix;
    }

    // 清理本次测试的所有 key
    void CleanupTestKeys() {
        if (!redis_) return;
        
        auto result = redis_->Scan("test:" + test_prefix_ + ":*", 1000);
        if (result.IsOk()) {
            for (const auto& key : result.Value()) {
                redis_->Del(key);
            }
        }
    }

    static std::string GetEnvOr(const char* name, const std::string& default_val) {
        const char* val = std::getenv(name);
        return val ? val : default_val;
    }

protected:
    std::unique_ptr<RedisClient> redis_;
    
    // 每个测试类实例使用不同的前缀
    std::string test_prefix_ = [] {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        return std::to_string(dis(gen));
    }();
};

// ============================================================================
// 连接与健康检查测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, Ping_Success) {
    auto result = redis_->Ping();
    
    EXPECT_TRUE(result.IsOk()) << "Ping failed: " << result.message;
}

// ============================================================================
// 字符串操作测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, Set_And_Get_Success) {
    std::string key = TestKey("string1");
    std::string value = "hello_world";
    
    // Set
    auto set_result = redis_->Set(key, value);
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    // Get
    auto get_result = redis_->Get(key);
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    ASSERT_TRUE(get_result.Value().has_value());
    EXPECT_EQ(get_result.Value().value(), value);
}

TEST_F(RedisClientIntegrationTest, Get_NonExistentKey_ReturnsNullopt) {
    std::string key = TestKey("non_existent_key_12345");
    
    auto result = redis_->Get(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_FALSE(result.Value().has_value()) << "Key should not exist";
}

TEST_F(RedisClientIntegrationTest, SetPx_WithTTL_ExpiresCorrectly) {
    std::string key = TestKey("ttl_key");
    std::string value = "temporary_value";
    
    // 设置 200ms 过期
    auto set_result = redis_->SetPx(key, value, 200ms);
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    // 立即获取应该存在
    auto get_result1 = redis_->Get(key);
    ASSERT_TRUE(get_result1.IsOk());
    ASSERT_TRUE(get_result1.Value().has_value());
    EXPECT_EQ(get_result1.Value().value(), value);
    
    // 等待过期
    std::this_thread::sleep_for(300ms);
    
    // 再次获取应该不存在
    auto get_result2 = redis_->Get(key);
    ASSERT_TRUE(get_result2.IsOk());
    EXPECT_FALSE(get_result2.Value().has_value()) << "Key should have expired";
}

TEST_F(RedisClientIntegrationTest, SetPx_InvalidTTL_ReturnsFail) {
    std::string key = TestKey("invalid_ttl");
    
    // TTL <= 0 应该失败
    auto result = redis_->SetPx(key, "value", 0ms);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(RedisClientIntegrationTest, SetNx_NewKey_Success) {
    std::string key = TestKey("setnx_new");
    
    auto result = redis_->SetNx(key, "value1");
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_TRUE(result.Value()) << "SetNx should succeed for new key";
}

TEST_F(RedisClientIntegrationTest, SetNx_ExistingKey_Fails) {
    std::string key = TestKey("setnx_existing");
    
    // 先设置一个值
    auto pre_set = redis_->Set(key, "original");
    ASSERT_TRUE(pre_set.IsOk()) << pre_set.message;
    
    // SetNx 应该失败（返回 false，但不是错误）
    auto result = redis_->SetNx(key, "new_value");
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_FALSE(result.Value()) << "SetNx should fail for existing key";
    
    // 原值不变
    auto get_result = redis_->Get(key);
    ASSERT_TRUE(get_result.IsOk());
    EXPECT_EQ(get_result.Value().value(), "original");
}

TEST_F(RedisClientIntegrationTest, SetNxPx_DistributedLock_Scenario) {
    std::string lock_key = TestKey("lock");
    
    // 获取锁（5秒过期）
    auto acquire1 = redis_->SetNxPx(lock_key, "owner1", 5000ms);
    ASSERT_TRUE(acquire1.IsOk()) << acquire1.message;
    EXPECT_TRUE(acquire1.Value()) << "First acquire should succeed";
    
    // 再次获取锁应该失败
    auto acquire2 = redis_->SetNxPx(lock_key, "owner2", 5000ms);
    ASSERT_TRUE(acquire2.IsOk()) << acquire2.message;
    EXPECT_FALSE(acquire2.Value()) << "Second acquire should fail";
    
    // 释放锁
    redis_->Del(lock_key);
    
    // 现在可以获取了
    auto acquire3 = redis_->SetNxPx(lock_key, "owner3", 5000ms);
    ASSERT_TRUE(acquire3.IsOk()) << acquire3.message;
    EXPECT_TRUE(acquire3.Value()) << "Third acquire should succeed after release";
}

// ============================================================================
// 通用操作测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, Exists_ExistingKey_ReturnsTrue) {
    std::string key = TestKey("exists_test");
    auto pre_set = redis_->Set(key, "value");
    ASSERT_TRUE(pre_set.IsOk()) << pre_set.message;
    
    auto result = redis_->Exists(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_TRUE(result.Value());
}

TEST_F(RedisClientIntegrationTest, Exists_NonExistentKey_ReturnsFalse) {
    std::string key = TestKey("not_exists_12345");
    
    auto result = redis_->Exists(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_FALSE(result.Value());
}

TEST_F(RedisClientIntegrationTest, Del_ExistingKey_ReturnsTrue) {
    std::string key = TestKey("del_test");
    auto pre_set = redis_->Set(key, "value");
    ASSERT_TRUE(pre_set.IsOk()) << pre_set.message;
    
    auto result = redis_->Del(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_TRUE(result.Value()) << "Del should return true for existing key";
    
    // 验证已删除
    auto exists = redis_->Exists(key);
    EXPECT_FALSE(exists.Value());
}

TEST_F(RedisClientIntegrationTest, Del_NonExistentKey_ReturnsFalse) {
    std::string key = TestKey("del_not_exists");
    
    auto result = redis_->Del(key);
    
    ASSERT_TRUE(result.IsOk()) << "Del should not fail for non-existent key: " << result.message;
    EXPECT_FALSE(result.Value()) << "Del should return false for non-existent key";
}

TEST_F(RedisClientIntegrationTest, PExpire_SetsTTL) {
    std::string key = TestKey("pexpire_test");
    auto pre_set = redis_->Set(key, "value");
    ASSERT_TRUE(pre_set.IsOk()) << pre_set.message;
    
    // 设置 500ms TTL
    auto expire_result = redis_->PExpire(key, 500ms);
    ASSERT_TRUE(expire_result.IsOk()) << expire_result.message;
    EXPECT_TRUE(expire_result.Value());
    
    // 检查 TTL
    auto ttl_result = redis_->PTTL(key);
    ASSERT_TRUE(ttl_result.IsOk()) << ttl_result.message;
    EXPECT_GT(ttl_result.Value(), 0) << "TTL should be positive";
    EXPECT_LE(ttl_result.Value(), 500) << "TTL should be <= 500ms";
}

TEST_F(RedisClientIntegrationTest, PTTL_NoExpire_ReturnsMinusOne) {
    std::string key = TestKey("no_expire");
    auto pre_set = redis_->Set(key, "value");  // 不设置过期时间
    ASSERT_TRUE(pre_set.IsOk()) << pre_set.message;
    
    auto result = redis_->PTTL(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), -1) << "Key without TTL should return -1";
}

TEST_F(RedisClientIntegrationTest, PTTL_NonExistentKey_ReturnsMinusTwo) {
    std::string key = TestKey("ttl_not_exists");
    
    auto result = redis_->PTTL(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), -2) << "Non-existent key should return -2";
}

TEST_F(RedisClientIntegrationTest, Keys_MatchesPattern) {
    // 创建多个 key
    ASSERT_TRUE(redis_->Set(TestKey("pattern:a"), "1").IsOk());
    ASSERT_TRUE(redis_->Set(TestKey("pattern:b"), "2").IsOk());
    ASSERT_TRUE(redis_->Set(TestKey("pattern:c"), "3").IsOk());
    ASSERT_TRUE(redis_->Set(TestKey("other:x"), "4").IsOk());
    
    auto result = redis_->Keys(TestKey("pattern:*"));
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value().size(), 3);
}

TEST_F(RedisClientIntegrationTest, Scan_IteratesKeys) {
    // 创建多个 key
    for (int i = 0; i < 10; ++i) {
        auto r = redis_->Set(TestKey("scan:" + std::to_string(i)), "value");
        ASSERT_TRUE(r.IsOk()) << r.message;
    }
    
    auto result = redis_->Scan(TestKey("scan:*"), 5);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value().size(), 10);
}

// ============================================================================
// Hash 操作测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, HSet_And_HGet_Success) {
    std::string key = TestKey("hash1");
    
    // HSet
    auto set_result = redis_->HSet(key, "field1", "value1");
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    // HGet
    auto get_result = redis_->HGet(key, "field1");
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    ASSERT_TRUE(get_result.Value().has_value());
    EXPECT_EQ(get_result.Value().value(), "value1");
}

TEST_F(RedisClientIntegrationTest, HGet_NonExistentField_ReturnsNullopt) {
    std::string key = TestKey("hash2");
    auto pre_set = redis_->HSet(key, "existing_field", "value");
    ASSERT_TRUE(pre_set.IsOk()) << pre_set.message;
    
    auto result = redis_->HGet(key, "non_existent_field");
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_FALSE(result.Value().has_value());
}

TEST_F(RedisClientIntegrationTest, HMSet_And_HGetAll_Success) {
    std::string key = TestKey("hash_multi");
    std::vector<std::pair<std::string, std::string>> fields = {
        {"name", "Alice"},
        {"age", "25"},
        {"city", "Beijing"}
    };
    
    // HMSet
    auto set_result = redis_->HMSet(key, fields);
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    // HGetAll
    auto get_result = redis_->HGetAll(key);
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    
    auto& hash = get_result.Value();
    EXPECT_EQ(hash.size(), 3);
    EXPECT_EQ(hash["name"], "Alice");
    EXPECT_EQ(hash["age"], "25");
    EXPECT_EQ(hash["city"], "Beijing");
}

TEST_F(RedisClientIntegrationTest, HGetAll_NonExistentKey_ReturnsEmptyMap) {
    std::string key = TestKey("hash_not_exists");
    
    auto result = redis_->HGetAll(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_TRUE(result.Value().empty());
}

TEST_F(RedisClientIntegrationTest, HDel_ExistingField_ReturnsTrue) {
    std::string key = TestKey("hash_del");
    ASSERT_TRUE(redis_->HSet(key, "field1", "value1").IsOk());
    ASSERT_TRUE(redis_->HSet(key, "field2", "value2").IsOk());
    
    auto result = redis_->HDel(key, "field1");
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_TRUE(result.Value());
    
    // 验证已删除
    auto get = redis_->HGet(key, "field1");
    EXPECT_FALSE(get.Value().has_value());
    
    // field2 仍然存在
    auto get2 = redis_->HGet(key, "field2");
    EXPECT_TRUE(get2.Value().has_value());
}

TEST_F(RedisClientIntegrationTest, HExists_ExistingField_ReturnsTrue) {
    std::string key = TestKey("hash_exists");
    ASSERT_TRUE(redis_->HSet(key, "field1", "value1").IsOk());
    
    auto result = redis_->HExists(key, "field1");
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_TRUE(result.Value());
}

TEST_F(RedisClientIntegrationTest, HExists_NonExistentField_ReturnsFalse) {
    std::string key = TestKey("hash_exists2");
    ASSERT_TRUE(redis_->HSet(key, "field1", "value1").IsOk());
    
    auto result = redis_->HExists(key, "field2");
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_FALSE(result.Value());
}

// ============================================================================
// 原子操作测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, Incr_NewKey_StartsFromZero) {
    std::string key = TestKey("incr_new");
    
    auto result = redis_->Incr(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), 1);
}

TEST_F(RedisClientIntegrationTest, Incr_ExistingKey_Increments) {
    std::string key = TestKey("incr_existing");
    ASSERT_TRUE(redis_->Set(key, "10").IsOk());
    
    auto result = redis_->Incr(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), 11);
}

TEST_F(RedisClientIntegrationTest, IncrBy_AddsValue) {
    std::string key = TestKey("incrby");
    ASSERT_TRUE(redis_->Set(key, "100").IsOk());
    
    auto result = redis_->IncrBy(key, 50);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), 150);
}

TEST_F(RedisClientIntegrationTest, IncrBy_NegativeValue_Decrements) {
    std::string key = TestKey("incrby_neg");
    ASSERT_TRUE(redis_->Set(key, "100").IsOk());
    
    auto result = redis_->IncrBy(key, -30);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), 70);
}

TEST_F(RedisClientIntegrationTest, Decr_Decrements) {
    std::string key = TestKey("decr");
    ASSERT_TRUE(redis_->Set(key, "10").IsOk());
    
    auto result = redis_->Decr(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), 9);
}

TEST_F(RedisClientIntegrationTest, Decr_BelowZero_GoesNegative) {
    std::string key = TestKey("decr_neg");
    ASSERT_TRUE(redis_->Set(key, "0").IsOk());
    
    auto result = redis_->Decr(key);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value(), -1);
}

// ============================================================================
// 并发测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, Concurrent_Incr_IsAtomic) {
    std::string key = TestKey("concurrent_incr");
    ASSERT_TRUE(redis_->Set(key, "0").IsOk());
    
    const int num_threads = 10;
    const int increments_per_thread = 100;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &key, increments_per_thread]() {
            for (int j = 0; j < increments_per_thread; ++j) {
                redis_->Incr(key);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto result = redis_->Get(key);
    ASSERT_TRUE(result.IsOk()) << result.message;
    EXPECT_EQ(result.Value().value(), std::to_string(num_threads * increments_per_thread));
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, Set_EmptyValue_Success) {
    std::string key = TestKey("empty_value");
    
    auto set_result = redis_->Set(key, "");
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    auto get_result = redis_->Get(key);
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    ASSERT_TRUE(get_result.Value().has_value());
    EXPECT_EQ(get_result.Value().value(), "");
}

TEST_F(RedisClientIntegrationTest, Set_BinaryData_Success) {
    std::string key = TestKey("binary");
    std::string binary_value = std::string("hello\0world", 11);  // 包含 null 字符
    
    auto set_result = redis_->Set(key, binary_value);
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    auto get_result = redis_->Get(key);
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    ASSERT_TRUE(get_result.Value().has_value());
    EXPECT_EQ(get_result.Value().value(), binary_value);
}

TEST_F(RedisClientIntegrationTest, Set_LargeValue_Success) {
    std::string key = TestKey("large_value");
    std::string large_value(1024 * 1024, 'x');  // 1MB
    
    auto set_result = redis_->Set(key, large_value);
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    auto get_result = redis_->Get(key);
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    ASSERT_TRUE(get_result.Value().has_value());
    EXPECT_EQ(get_result.Value().value().size(), large_value.size());
}

TEST_F(RedisClientIntegrationTest, HMSet_EmptyFields_Success) {
    std::string key = TestKey("hash_empty");
    std::vector<std::pair<std::string, std::string>> empty_fields;
    
    // 空字段列表应该成功（no-op）
    auto result = redis_->HMSet(key, empty_fields);
    EXPECT_TRUE(result.IsOk());
}

// ============================================================================
// 业务场景测试
// ============================================================================

TEST_F(RedisClientIntegrationTest, SmsCodeScenario) {
    std::string mobile = "13800138000";
    std::string code_key = TestKey("sms:code:register:" + mobile);
    std::string interval_key = TestKey("sms:interval:" + mobile);
    std::string verify_count_key = TestKey("sms:verify_count:register:" + mobile);
    
    // 1. 检查发送间隔
    auto interval_exists = redis_->Exists(interval_key);
    ASSERT_TRUE(interval_exists.IsOk()) << interval_exists.message;
    EXPECT_FALSE(interval_exists.Value()) << "初始不应有发送间隔";
    
    // 2. 存储验证码（5分钟有效）
    auto code = "123456";
    auto set_code = redis_->SetPx(code_key, code, 300000ms);
    ASSERT_TRUE(set_code.IsOk()) << set_code.message;
    
    // 3. 设置发送间隔（60秒）
    auto set_interval = redis_->SetPx(interval_key, "1", 60000ms);
    ASSERT_TRUE(set_interval.IsOk()) << set_interval.message;
    
    // 4. 模拟验证错误，增加计数
    auto count1 = redis_->Incr(verify_count_key);
    ASSERT_TRUE(count1.IsOk()) << count1.message;
    EXPECT_EQ(count1.Value(), 1);
    
    auto count2 = redis_->Incr(verify_count_key);
    ASSERT_TRUE(count2.IsOk()) << count2.message;
    EXPECT_EQ(count2.Value(), 2);
    
    // 5. 验证成功，清除计数
    redis_->Del(verify_count_key);
    
    auto verify_exists = redis_->Exists(verify_count_key);
    ASSERT_TRUE(verify_exists.IsOk());
    EXPECT_FALSE(verify_exists.Value());
}

TEST_F(RedisClientIntegrationTest, LoginFailureCountScenario) {
    std::string mobile = "13800138000";
    std::string fail_key = TestKey("login:fail:" + mobile);
    const int max_attempts = 5;
    const int lock_duration_ms = 1800000;  // 30分钟
    
    // 模拟多次登录失败
    for (int i = 1; i <= max_attempts; ++i) {
        auto count = redis_->Incr(fail_key);
        ASSERT_TRUE(count.IsOk()) << "Incr failed at iteration " << i << ": " << count.message;
        
        if (count.Value() == max_attempts) {
            // 达到上限，设置锁定时间
            auto expire = redis_->PExpire(fail_key, std::chrono::milliseconds(lock_duration_ms));
            ASSERT_TRUE(expire.IsOk()) << expire.message;
        }
    }
    
    // 检查计数
    auto get_count = redis_->Get(fail_key);
    ASSERT_TRUE(get_count.IsOk()) << get_count.message;
    EXPECT_EQ(get_count.Value().value(), std::to_string(max_attempts));
    
    // 检查 TTL
    auto ttl = redis_->PTTL(fail_key);
    ASSERT_TRUE(ttl.IsOk()) << ttl.message;
    EXPECT_GT(ttl.Value(), 0) << "应该设置了过期时间";
    
    // 登录成功后清除
    redis_->Del(fail_key);
}

TEST_F(RedisClientIntegrationTest, UserSessionScenario) {
    std::string session_id = "sess_abc123";
    std::string session_key = TestKey("session:" + session_id);
    
    // 存储会话数据
    std::vector<std::pair<std::string, std::string>> session_data = {
        {"user_id", "12345"},
        {"user_uuid", "550e8400-e29b-41d4-a716-446655440000"},
        {"mobile", "13800138000"},
        {"role", "0"},
        {"login_time", "2024-01-15 10:30:00"}
    };
    
    auto set_result = redis_->HMSet(session_key, session_data);
    ASSERT_TRUE(set_result.IsOk()) << set_result.message;
    
    // 设置会话过期时间（24小时）
    auto expire = redis_->PExpire(session_key, std::chrono::milliseconds(24 * 3600 * 1000));
    ASSERT_TRUE(expire.IsOk()) << expire.message;
    
    // 读取会话
    auto get_result = redis_->HGetAll(session_key);
    ASSERT_TRUE(get_result.IsOk()) << get_result.message;
    
    auto& session = get_result.Value();
    EXPECT_EQ(session["user_id"], "12345");
    EXPECT_EQ(session["mobile"], "13800138000");
    
    // 更新单个字段
    auto update = redis_->HSet(session_key, "last_access", "2024-01-15 11:00:00");
    ASSERT_TRUE(update.IsOk()) << update.message;
    
    // 检查字段存在
    auto has_last_access = redis_->HExists(session_key, "last_access");
    ASSERT_TRUE(has_last_access.IsOk()) << has_last_access.message;
    EXPECT_TRUE(has_last_access.Value());
    
    // 注销会话
    redis_->Del(session_key);
    auto exists = redis_->Exists(session_key);
    ASSERT_TRUE(exists.IsOk());
    EXPECT_FALSE(exists.Value());
}

// ============================================================================
// 运行测试
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
