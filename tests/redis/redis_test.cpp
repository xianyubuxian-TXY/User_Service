// tests/redis_test.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include "cache/redis_client.h"
#include "config/config.h"

using namespace std::chrono_literals;

class RedisTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // 从环境变量读取配置（支持 CI/CD）
        config_.redis.host = GetEnvOrDefault("TEST_REDIS_HOST", "localhost");
        config_.redis.port = std::stoi(GetEnvOrDefault("TEST_REDIS_PORT", "6379"));
        config_.redis.password = GetEnvOrDefault("TEST_REDIS_PASSWORD", "");
        config_.redis.db = std::stoi(GetEnvOrDefault("TEST_REDIS_DB", "15")); // 使用测试专用 DB
        config_.redis.pool_size = 5;
        config_.redis.wait_timeout_ms = 1000;
        config_.redis.connect_timeout_ms = 5000;
        config_.redis.socket_timeout_ms = 3000;

        try {
            client_ = std::make_unique<user::ClientRedis>(config_.redis);
            
            // ✅ 验证连接
            if (!client_->Ping()) {
                GTEST_SKIP() << "Redis server not available";
            }
            
            // ✅ 验证权限（尝试写入）
            std::string test_key = "__test_permission__";
            if (!client_->Set(test_key, "test")) {
                GTEST_SKIP() << "Redis write permission denied";
            }
            client_->Del(test_key);
            
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to connect to Redis: " << e.what();
        }
    }

    void TearDown() override {
        if (client_) {
            // ✅ 自动清理本次测试创建的所有 key
            CleanupTestKeys();
        }
    }

    // ✅ 线程安全的清理
    void CleanupTestKeys() {
        std::lock_guard<std::mutex> lock(keys_mutex_);
        for (const auto& key : test_keys_) {
            try {
                client_->Del(key);
            } catch (...) {
                // 忽略删除失败
            }
        }
        test_keys_.clear();
    }

    // ✅ 线程安全的 key 生成器
    std::string TestKey(const std::string& suffix = "") {
        thread_local static int thread_counter = 0;  // 线程本地计数器
        static std::atomic<int> global_counter{0};   // 全局计数器
        
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::string key = "test:key:" + std::to_string(now) + ":" + 
                         std::to_string(global_counter++) + ":" +
                         std::to_string(thread_counter++);
        
        if (!suffix.empty()) {
            key += ":" + suffix;
        }
        
        // ✅ 加锁保护 vector
        {
            std::lock_guard<std::mutex> lock(keys_mutex_);
            test_keys_.push_back(key);
        }
        
        return key;
    }

    std::string GetEnvOrDefault(const char* name, const std::string& default_val) {
        const char* val = std::getenv(name);
        return val ? val : default_val;
    }

    user::Config config_;
    std::unique_ptr<user::ClientRedis> client_;
    std::vector<std::string> test_keys_;
    std::mutex keys_mutex_;  // ✅ 保护 test_keys_ 的互斥锁
};

// ==================== 连接测试 ====================

TEST_F(RedisTestFixture, ConnectionSuccess) {
    ASSERT_NE(client_, nullptr);
    EXPECT_TRUE(client_->Ping());
}

TEST_F(RedisTestFixture, ConnectionWithInvalidHost) {
    user::RedisConfig bad_config = config_.redis;
    bad_config.host = "invalid.host.example.com";
    bad_config.connect_timeout_ms = 1000;  // 快速失败
    
    EXPECT_THROW({
        user::ClientRedis bad_client(bad_config);
        bad_client.Set("test_key", "test_value");  // ✅ 使用 Set() 触发连接
    }, sw::redis::Error);
}

TEST_F(RedisTestFixture, ConnectionWithInvalidPort) {
    user::RedisConfig bad_config = config_.redis;
    bad_config.port = 9999;  // 不存在的端口
    bad_config.connect_timeout_ms = 1000;
    
    EXPECT_THROW({
        user::ClientRedis bad_client(bad_config);
        bad_client.Set("test_key", "test_value");
    }, sw::redis::Error);
}

TEST_F(RedisTestFixture, ConnectionWithInvalidPassword) {
    user::RedisConfig bad_config = config_.redis;
    bad_config.password = "wrong_password";
    
    // 只有当 Redis 设置了密码时才会失败
    if (!config_.redis.password.empty()) {
        EXPECT_THROW({
            user::ClientRedis bad_client(bad_config);
            bad_client.Set("test_key", "test_value");
        }, sw::redis::Error);
    }
}

TEST_F(RedisTestFixture, ConnectionWithInvalidDB) {
    user::RedisConfig bad_config = config_.redis;
    bad_config.db = 999;  // 超出范围
    
    EXPECT_THROW({
        user::ClientRedis bad_client(bad_config);
        bad_client.Set("test_key", "test_value");
    }, sw::redis::Error);
}

TEST_F(RedisTestFixture, PingTest) {
    EXPECT_TRUE(client_->Ping());
    
    // 多次 Ping
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(client_->Ping());
    }
}

// ==================== String 操作测试 ====================

TEST_F(RedisTestFixture, SetAndGet) {
    std::string key = TestKey();
    std::string value = "test_value";
    
    EXPECT_TRUE(client_->Set(key, value));
    
    auto result = client_->Get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), value);
}

TEST_F(RedisTestFixture, GetNonExistentKey) {
    std::string key = TestKey("non_existent");
    
    auto result = client_->Get(key);
    EXPECT_FALSE(result.has_value());
}

TEST_F(RedisTestFixture, SetEmptyValue) {
    std::string key = TestKey();
    std::string value = "";
    
    EXPECT_TRUE(client_->Set(key, value));
    
    auto result = client_->Get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "");
}

TEST_F(RedisTestFixture, SetLargeValue) {
    std::string key = TestKey();
    std::string value(1024 * 1024, 'x');  // 1MB 数据
    
    EXPECT_TRUE(client_->Set(key, value));
    
    auto result = client_->Get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), value.size());
}

TEST_F(RedisTestFixture, SetWithBinaryData) {
    std::string key = TestKey();
    std::string value = "binary\0data\0test";
    value.resize(17);  // 包含 \0
    
    EXPECT_TRUE(client_->Set(key, value));
    
    auto result = client_->Get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), value.size());
    EXPECT_EQ(result.value(), value);
}

TEST_F(RedisTestFixture, SetOverwrite) {
    std::string key = TestKey();
    
    EXPECT_TRUE(client_->Set(key, "value1"));
    EXPECT_TRUE(client_->Set(key, "value2"));
    
    auto result = client_->Get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value2");
}

TEST_F(RedisTestFixture, SetPxWithExpiration) {
    std::string key = TestKey();
    std::string value = "expiring_value";
    
    EXPECT_TRUE(client_->SetPx(key, value, 500ms));
    
    // 立即读取应该存在
    auto result1 = client_->Get(key);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), value);
    
    // 等待过期
    std::this_thread::sleep_for(600ms);
    
    auto result2 = client_->Get(key);
    EXPECT_FALSE(result2.has_value());
}

TEST_F(RedisTestFixture, SetPxWithZeroTTL) {
    std::string key = TestKey();
    
    // TTL 为 0 应该返回 false
    bool result = client_->SetPx(key, "value", 0ms);
    EXPECT_FALSE(result);
    
    // 验证 key 不存在
    EXPECT_FALSE(client_->Exists(key));
}

// ==================== 键管理测试 ====================

TEST_F(RedisTestFixture, ExistsTrue) {
    std::string key = TestKey();
    
    client_->Set(key, "value");
    EXPECT_TRUE(client_->Exists(key));
}

TEST_F(RedisTestFixture, ExistsFalse) {
    std::string key = TestKey("non_existent");
    
    EXPECT_FALSE(client_->Exists(key));
}

TEST_F(RedisTestFixture, DelExistingKey) {
    std::string key = TestKey();
    
    client_->Set(key, "value");
    EXPECT_TRUE(client_->Del(key));
    EXPECT_FALSE(client_->Exists(key));
}

TEST_F(RedisTestFixture, DelNonExistentKey) {
    std::string key = TestKey("non_existent");
    
    // Redis DEL 对不存在的 key 返回 0
    EXPECT_FALSE(client_->Del(key));
}

TEST_F(RedisTestFixture, PExpireExistingKey) {
    std::string key = TestKey();
    
    client_->Set(key, "value");
    EXPECT_TRUE(client_->PExpire(key, 500ms));
    
    // 立即检查应该存在
    EXPECT_TRUE(client_->Exists(key));
    
    // 等待过期
    std::this_thread::sleep_for(600ms);
    EXPECT_FALSE(client_->Exists(key));
}

TEST_F(RedisTestFixture, PExpireNonExistentKey) {
    std::string key = TestKey("non_existent");
    
    // 对不存在的 key 设置过期时间应该失败
    EXPECT_FALSE(client_->PExpire(key, 1000ms));
}

TEST_F(RedisTestFixture, PExpireUpdateTTL) {
    std::string key = TestKey();
    
    client_->SetPx(key, "value", 2000ms);
    
    // 更新 TTL 为 500ms
    EXPECT_TRUE(client_->PExpire(key, 500ms));
    
    std::this_thread::sleep_for(600ms);
    EXPECT_FALSE(client_->Exists(key));
}

// ==================== Hash 操作测试 ====================

TEST_F(RedisTestFixture, HSetAndHGet) {
    std::string key = TestKey();
    
    EXPECT_TRUE(client_->HSet(key, {"field1", "value1"}));
    
    auto result = client_->HGet(key, "field1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value1");
}

TEST_F(RedisTestFixture, HGetNonExistentField) {
    std::string key = TestKey();
    
    client_->HSet(key, {"field1", "value1"});
    
    auto result = client_->HGet(key, "non_existent_field");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RedisTestFixture, HMSetAndHGetAll) {
    std::string key = TestKey();
    
    std::vector<std::pair<std::string, std::string>> fields = {
        {"field1", "value1"},
        {"field2", "value2"},
        {"field3", "value3"}
    };
    
    EXPECT_TRUE(client_->HMSet(key, fields));
    
    auto result = client_->HGetAll(key);
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result["field1"], "value1");
    EXPECT_EQ(result["field2"], "value2");
    EXPECT_EQ(result["field3"], "value3");
}

TEST_F(RedisTestFixture, HGetAllEmptyHash) {
    std::string key = TestKey("non_existent_hash");
    
    auto result = client_->HGetAll(key);
    EXPECT_TRUE(result.empty());
}

TEST_F(RedisTestFixture, HDelExistingField) {
    std::string key = TestKey();
    
    client_->HSet(key, {"field1", "value1"});
    EXPECT_TRUE(client_->HDel(key, "field1"));
    
    auto result = client_->HGet(key, "field1");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RedisTestFixture, HDelNonExistentField) {
    std::string key = TestKey();
    
    client_->HSet(key, {"field1", "value1"});
    
    // 删除不存在的字段应该返回 false
    EXPECT_FALSE(client_->HDel(key, "non_existent_field"));
}

TEST_F(RedisTestFixture, HSetOverwrite) {
    std::string key = TestKey();
    
    client_->HSet(key, {"field1", "value1"});
    client_->HSet(key, {"field1", "value2"});
    
    auto result = client_->HGet(key, "field1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value2");
}

TEST_F(RedisTestFixture, HMSetEmptyFields) {
    std::string key = TestKey();
    
    std::vector<std::pair<std::string, std::string>> empty_fields;
    
    // 空字段列表应该成功但不做任何事
    EXPECT_TRUE(client_->HMSet(key, empty_fields));
    
    auto result = client_->HGetAll(key);
    EXPECT_TRUE(result.empty());
}

TEST_F(RedisTestFixture, HashWithBinaryData) {
    std::string key = TestKey();
    std::string field = "binary_field";
    std::string value = "binary\0value\0test";
    value.resize(18);
    
    EXPECT_TRUE(client_->HSet(key, {field, value}));
    
    auto result = client_->HGet(key, field);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), value.size());
    EXPECT_EQ(result.value(), value);
}

// ==================== 原子操作测试 ====================

TEST_F(RedisTestFixture, IncrNewKey) {
    std::string key = TestKey();
    
    // 对不存在的 key 执行 INCR，应该从 0 开始
    int64_t result = client_->Incr(key);
    EXPECT_EQ(result, 1);
}

TEST_F(RedisTestFixture, IncrExistingKey) {
    std::string key = TestKey();
    
    client_->Set(key, "10");
    
    int64_t result = client_->Incr(key);
    EXPECT_EQ(result, 11);
}

TEST_F(RedisTestFixture, IncrMultipleTimes) {
    std::string key = TestKey();
    
    for (int i = 1; i <= 10; ++i) {
        int64_t result = client_->Incr(key);
        EXPECT_EQ(result, i);
    }
}

TEST_F(RedisTestFixture, IncrByPositive) {
    std::string key = TestKey();
    
    int64_t result = client_->IncrBy(key, 100);
    EXPECT_EQ(result, 100);
}

TEST_F(RedisTestFixture, IncrByNegative) {
    std::string key = TestKey();
    
    client_->Set(key, "100");
    
    int64_t result = client_->IncrBy(key, -30);
    EXPECT_EQ(result, 70);
}

TEST_F(RedisTestFixture, IncrByZero) {
    std::string key = TestKey();
    
    client_->Set(key, "42");
    
    int64_t result = client_->IncrBy(key, 0);
    EXPECT_EQ(result, 42);
}

TEST_F(RedisTestFixture, IncrNonIntegerValue) {
    std::string key = TestKey();
    
    client_->Set(key, "not_a_number");
    
    // ✅ 应该抛出异常
    EXPECT_THROW({
        client_->Incr(key);
    }, sw::redis::Error);
}

TEST_F(RedisTestFixture, IncrOverflow) {
    std::string key = TestKey();
    
    // 设置为接近 int64_t 最大值
    client_->Set(key, std::to_string(INT64_MAX - 1));
    
    int64_t result1 = client_->Incr(key);
    EXPECT_EQ(result1, INT64_MAX);
    
    // ✅ 再次 INCR 应该溢出并抛出异常
    EXPECT_THROW({
        client_->Incr(key);
    }, sw::redis::Error);
}

// ==================== 并发测试 ====================

TEST_F(RedisTestFixture, ConcurrentSet) {
    const int num_threads = 10;
    const int ops_per_thread = 100;
    
    // ✅ 预生成所有 key（避免竞争）
    std::vector<std::vector<std::string>> thread_keys(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        for (int j = 0; j < ops_per_thread; ++j) {
            thread_keys[i].push_back(TestKey("thread_" + std::to_string(i) + 
                                            "_op_" + std::to_string(j)));
        }
    }
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                if (client_->Set(thread_keys[i][j], "value")) {
                    ++success_count;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, num_threads * ops_per_thread);
}

TEST_F(RedisTestFixture, ConcurrentIncr) {
    std::string key = TestKey();  // ✅ 在主线程中生成 key
    const int num_threads = 10;
    const int ops_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                client_->Incr(key);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto result = client_->Get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::stoi(result.value()), num_threads * ops_per_thread);
}

TEST_F(RedisTestFixture, ConcurrentHashOperations) {
    std::string key = TestKey();  // ✅ 在主线程中生成 key
    const int num_threads = 10;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            std::string field = "field_" + std::to_string(i);
            std::string value = "value_" + std::to_string(i);
            client_->HSet(key, {field, value});
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto result = client_->HGetAll(key);
    EXPECT_EQ(result.size(), num_threads);
}

// ==================== 错误处理测试 ====================

TEST_F(RedisTestFixture, HandleRedisErrorStringAsHash) {
    std::string key = TestKey();
    
    // 设置为 String 类型
    client_->Set(key, "string_value");
    
    // 尝试作为 Hash 操作（应该抛出 WRONGTYPE 异常）
    EXPECT_THROW({
        client_->HGet(key, "field");
    }, sw::redis::Error);
    
    EXPECT_THROW({
        client_->HSet(key, {"field", "value"});
    }, sw::redis::Error);
    
    EXPECT_THROW({
        client_->HGetAll(key);
    }, sw::redis::Error);
}

TEST_F(RedisTestFixture, HandleRedisErrorHashAsString) {
    std::string key = TestKey();
    
    // 设置为 Hash 类型
    client_->HSet(key, {"field1", "value1"});
    
    // 尝试作为 String 操作（应该抛出异常）
    EXPECT_THROW({
        client_->Get(key);
    }, sw::redis::Error);
}

TEST_F(RedisTestFixture, HandleRedisErrorIncrOnNonInteger) {
    std::string key = TestKey();
    
    // 设置为非整数字符串
    client_->Set(key, "not_a_number");
    
    // 尝试 INCR（应该抛出异常）
    EXPECT_THROW({
        client_->Incr(key);
    }, sw::redis::Error);
}

// ==================== 性能测试 ====================

TEST_F(RedisTestFixture, PerformanceSetGet) {
    const int num_operations = 1000;  // ✅ 减少操作数（避免生成过多 key）
    
    // ✅ 预生成 key
    std::vector<std::string> keys;
    for (int i = 0; i < num_operations; ++i) {
        keys.push_back(TestKey(std::to_string(i)));
    }
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        client_->Set(keys[i], "value_" + std::to_string(i));
    }
    
    for (int i = 0; i < num_operations; ++i) {
        client_->Get(keys[i]);
    }
    
    auto duration = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    std::cout << "Performed " << (num_operations * 2) 
              << " operations in " << ms << " ms" << std::endl;
    
    // 性能断言（可选）
    EXPECT_LT(ms, 5000);  // 应该在 5 秒内完成
}

TEST_F(RedisTestFixture, PerformanceHashOperations) {
    std::string key = TestKey();
    const int num_fields = 1000;
    
    auto start = std::chrono::steady_clock::now();
    
    std::vector<std::pair<std::string, std::string>> fields;
    for (int i = 0; i < num_fields; ++i) {
        fields.emplace_back("field_" + std::to_string(i), "value_" + std::to_string(i));
    }
    
    client_->HMSet(key, fields);
    client_->HGetAll(key);
    
    auto duration = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    std::cout << "Hash operations with " << num_fields 
              << " fields took " << ms << " ms" << std::endl;
    
    EXPECT_LT(ms, 2000);  // 应该在 2 秒内完成
}
