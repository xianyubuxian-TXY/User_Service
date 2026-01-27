#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>
#include <fstream>
#include "db/mysql_connection.h"
#include "db/mysql_result.h"
#include "pool/connection_pool.h"
#include "config/config.h"
#include "server/exception.h"

namespace fs = std::filesystem;

class MySQLTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试配置
        CreateTestConfig();
        
        // 创建测试数据库和表
        CreateTestDatabase();
    }

    void TearDown() override {
        // 清理测试数据
        CleanupTestDatabase();
    }

    void CreateTestConfig() {
        config_.mysql.host = GetEnvOrDefault("TEST_MYSQL_HOST", "localhost");
        config_.mysql.port = std::stoi(GetEnvOrDefault("TEST_MYSQL_PORT", "3306"));
        config_.mysql.username = GetEnvOrDefault("TEST_MYSQL_USER", "test_user");
        config_.mysql.password = GetEnvOrDefault("TEST_MYSQL_PASSWORD", "@txylj2864");
        config_.mysql.database = "test_db";
        config_.mysql.pool_size = 5;
        config_.mysql.connection_timeout_ms = 5000;
        config_.mysql.read_timeout_ms = 3000;
        config_.mysql.write_timeout_ms = 3000;
        config_.mysql.max_retries = 3;
        config_.mysql.retry_interval_ms = 1000;
        config_.mysql.auto_reconnect = true;
        config_.mysql.charset = "utf8mb4";
    }

    void CreateTestDatabase() {
        // 先连接到 test_db 系统数据库创建测试库
        user::MySQLConfig sys_config = config_.mysql;
        sys_config.database = "test_db";
        
        user::MySQLConnection sys_conn(sys_config);
        
        // 创建测试数据库（如果不存在）
        sys_conn.Execute("CREATE DATABASE IF NOT EXISTS test_user_service "
                         "CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
        
        // 连接到测试数据库
        conn_ = std::make_unique<user::MySQLConnection>(config_.mysql);
        
        // 创建测试表
        conn_->Execute(R"(
            CREATE TABLE IF NOT EXISTS test_users (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(64) NOT NULL UNIQUE,
                email VARCHAR(128),
                age INT,
                score DOUBLE,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_username (username)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
    }

    void CleanupTestDatabase() {
        if (conn_) {
            // 清空测试表（保留结构）
            conn_->Execute("TRUNCATE TABLE test_users");
        }
    }

    std::string GetEnvOrDefault(const char* name, const std::string& default_val) {
        const char* val = std::getenv(name);
        return val ? val : default_val;
    }

    user::Config config_;
    std::unique_ptr<user::MySQLConnection> conn_;
};

// ==================== 连接测试 ====================

TEST_F(MySQLTestFixture, ConnectionSuccess) {
    ASSERT_NE(conn_, nullptr);
    EXPECT_TRUE(conn_->Valid());
}

TEST_F(MySQLTestFixture, ConnectionWithInvalidHost) {
    user::MySQLConfig bad_config = config_.mysql;
    bad_config.host = "invalid.host.example.com";
    bad_config.max_retries = 1;  // 快速失败
    
    EXPECT_THROW(
        user::MySQLConnection bad_conn(bad_config),
        user::MySQLException);
}

TEST_F(MySQLTestFixture, ConnectionWithInvalidCredentials) {
    user::MySQLConfig bad_config = config_.mysql;
    bad_config.password = "wrong_password";
    bad_config.max_retries = 0;
    
    EXPECT_THROW(
        user::MySQLConnection bad_conn(bad_config), 
        user::MySQLException);
}

TEST_F(MySQLTestFixture, ConnectionWithInvalidDatabase) {
    user::MySQLConfig bad_config = config_.mysql;
    bad_config.database = "non_existent_db_12345";
    bad_config.max_retries = 0;
    
    EXPECT_THROW(
        user::MySQLConnection bad_conn(bad_config),
        user::MySQLException);
}

TEST_F(MySQLTestFixture, AutoReconnect) {
    // 注意：这个测试需要手动模拟断线，实际环境中较难测试
    EXPECT_TRUE(conn_->Valid());
    
    // 模拟连接断开（需要 root 权限或手动重启 MySQL）
    // 这里只验证配置是否生效
    EXPECT_TRUE(config_.mysql.auto_reconnect.value_or(false));
}

// ==================== Execute 测试 ====================

TEST_F(MySQLTestFixture, InsertSingleRow) {
    uint64_t affected = conn_->Execute(
        "INSERT INTO test_users (username, email, age, score) VALUES (?, ?, ?, ?)",
        {"alice", "alice@example.com", 25, 95.5}
    );
    
    EXPECT_EQ(affected, 1);
    EXPECT_GT(conn_->LastInsertId(), 0);
}

TEST_F(MySQLTestFixture, InsertMultipleRows) {
    uint64_t affected1 = conn_->Execute(
        "INSERT INTO test_users (username, email, age) VALUES (?, ?, ?)",
        {"user1", "user1@example.com", 20}
    );
    
    uint64_t affected2 = conn_->Execute(
        "INSERT INTO test_users (username, email, age) VALUES (?, ?, ?)",
        {"user2", "user2@example.com", 30}
    );
    
    EXPECT_EQ(affected1, 1);
    EXPECT_EQ(affected2, 1);
}

TEST_F(MySQLTestFixture, InsertWithNullValues) {
    uint64_t affected = conn_->Execute(
        "INSERT INTO test_users (username, email, age, score) VALUES (?, ?, ?, ?)",
        {"bob", nullptr, nullptr, nullptr}
    );
    
    EXPECT_EQ(affected, 1);
}

TEST_F(MySQLTestFixture, UpdateRows) {
    // 先插入
    conn_->Execute(
        "INSERT INTO test_users (username, email, age) VALUES (?, ?, ?)",
        {"charlie", "charlie@example.com", 25}
    );
    
    // 更新
    uint64_t affected = conn_->Execute(
        "UPDATE test_users SET age = ? WHERE username = ?",
        {30, "charlie"}
    );
    
    EXPECT_EQ(affected, 1);
}

TEST_F(MySQLTestFixture, DeleteRows) {
    // 先插入
    conn_->Execute(
        "INSERT INTO test_users (username, email) VALUES (?, ?)",
        {"david", "david@example.com"}
    );
    
    // 删除
    uint64_t affected = conn_->Execute(
        "DELETE FROM test_users WHERE username = ?",
        {"david"}
    );
    
    EXPECT_EQ(affected, 1);
}

TEST_F(MySQLTestFixture, DuplicateKeyError) {
    conn_->Execute(
        "INSERT INTO test_users (username, email) VALUES (?, ?)",
        {"unique_user", "unique@example.com"}
    );
    
    // 尝试插入相同的 username（违反 UNIQUE 约束）
    EXPECT_THROW(
        conn_->Execute(
            "INSERT INTO test_users (username, email) VALUES (?, ?)",
            {"unique_user", "another@example.com"}
        ), 
        user::MySQLException);
}

// ==================== Query 测试 ====================

TEST_F(MySQLTestFixture, QueryEmptyResult) {
    auto result = conn_->Query("SELECT * FROM test_users WHERE username = ?", {"non_existent"});
    
    EXPECT_TRUE(result.Empty());
    EXPECT_EQ(result.RowCount(), 0);
}

TEST_F(MySQLTestFixture, QuerySingleRow) {
    conn_->Execute(
        "INSERT INTO test_users (username, email, age, score) VALUES (?, ?, ?, ?)",
        {"alice", "alice@example.com", 25, 95.5}
    );
    
    auto result = conn_->Query("SELECT * FROM test_users WHERE username = ?", {"alice"});
    
    EXPECT_FALSE(result.Empty());
    EXPECT_EQ(result.RowCount(), 1);
    EXPECT_TRUE(result.Next());
    
    auto username = result.GetString(1);  // username 是第 2 列（索引 1）
    ASSERT_TRUE(username.has_value());
    EXPECT_EQ(username.value(), "alice");
}

TEST_F(MySQLTestFixture, QueryMultipleRows) {
    conn_->Execute("INSERT INTO test_users (username, age) VALUES (?, ?)", {"user1", 20});
    conn_->Execute("INSERT INTO test_users (username, age) VALUES (?, ?)", {"user2", 30});
    conn_->Execute("INSERT INTO test_users (username, age) VALUES (?, ?)", {"user3", 40});
    
    auto result = conn_->Query("SELECT username, age FROM test_users ORDER BY age");
    
    EXPECT_EQ(result.RowCount(), 3);
    
    // 第一行
    ASSERT_TRUE(result.Next());
    EXPECT_EQ(result.GetString(0).value(), "user1");
    EXPECT_EQ(result.GetInt(1).value(), 20);
    
    // 第二行
    ASSERT_TRUE(result.Next());
    EXPECT_EQ(result.GetString(0).value(), "user2");
    EXPECT_EQ(result.GetInt(1).value(), 30);
    
    // 第三行
    ASSERT_TRUE(result.Next());
    EXPECT_EQ(result.GetString(0).value(), "user3");
    EXPECT_EQ(result.GetInt(1).value(), 40);
    
    // 没有更多行
    EXPECT_FALSE(result.Next());
}

TEST_F(MySQLTestFixture, QueryWithNullValues) {
    conn_->Execute(
        "INSERT INTO test_users (username, email, age, score) VALUES (?, ?, ?, ?)",
        {"null_user", nullptr, nullptr, nullptr}
    );
    
    auto result = conn_->Query("SELECT email, age, score FROM test_users WHERE username = ?", 
                                {"null_user"});
    
    ASSERT_TRUE(result.Next());
    EXPECT_TRUE(result.IsNull(0));  // email
    EXPECT_TRUE(result.IsNull(1));  // age
    EXPECT_TRUE(result.IsNull(2));  // score
}

TEST_F(MySQLTestFixture, QueryDifferentDataTypes) {
    conn_->Execute(
        "INSERT INTO test_users (username, email, age, score) VALUES (?, ?, ?, ?)",
        {"typed_user", "typed@example.com", 42, 88.88}
    );
    
    auto result = conn_->Query("SELECT username, age, score FROM test_users WHERE username = ?", 
                                {"typed_user"});
    
    ASSERT_TRUE(result.Next());
    
    // 字符串
    auto username = result.GetString(0);
    ASSERT_TRUE(username.has_value());
    EXPECT_EQ(username.value(), "typed_user");
    
    // 整数
    auto age = result.GetInt(1);
    ASSERT_TRUE(age.has_value());
    EXPECT_EQ(age.value(), 42);
    
    // 浮点数
    auto score = result.GetDouble(2);
    ASSERT_TRUE(score.has_value());
    EXPECT_NEAR(score.value(), 88.88, 0.01);
}

// ==================== SQL 注入防护测试 ====================

TEST_F(MySQLTestFixture, SQLInjectionPrevention) {
    // 尝试 SQL 注入
    std::string malicious_input = "'; DROP TABLE test_users; --";
    
    // 应该被正确转义，不会执行恶意 SQL
    uint64_t affected = conn_->Execute(
        "INSERT INTO test_users (username, email) VALUES (?, ?)",
        {malicious_input, "hacker@example.com"}
    );
    
    EXPECT_EQ(affected, 1);
    
    // 验证表仍然存在
    auto result = conn_->Query("SELECT COUNT(*) FROM test_users");
    ASSERT_TRUE(result.Next());
    EXPECT_GE(result.GetInt(0).value(), 1);
}

TEST_F(MySQLTestFixture, SpecialCharactersEscaping) {
    std::string special_chars = R"(Test's "quoted" \backslash\ and %wildcards%)";
    
    conn_->Execute(
        "INSERT INTO test_users (username, email) VALUES (?, ?)",
        {special_chars, "special@example.com"}
    );
    
    auto result = conn_->Query("SELECT username FROM test_users WHERE username = ?", 
                                {special_chars});
    
    ASSERT_TRUE(result.Next());
    EXPECT_EQ(result.GetString(0).value(), special_chars);
}

// ==================== 参数绑定测试 ====================

TEST_F(MySQLTestFixture, ParameterCountMismatch_TooFew) {
    EXPECT_THROW({
        conn_->Execute(
            "INSERT INTO test_users (username, email, age) VALUES (?, ?, ?)",
            {"user1", "user1@example.com"}  // 少了一个参数
        );
    }, std::runtime_error);
}

TEST_F(MySQLTestFixture, ParameterCountMismatch_TooMany) {
    EXPECT_THROW({
        conn_->Execute(
            "INSERT INTO test_users (username, email) VALUES (?, ?)",
            {"user1", "user1@example.com", 25}  // 多了一个参数
        );
    }, std::runtime_error);
}

TEST_F(MySQLTestFixture, NoPlaceholdersWithParams) {
    // SQL 中没有占位符但传了参数
    EXPECT_THROW({
        conn_->Execute(
            "INSERT INTO test_users (username, email) VALUES ('user1', 'user1@example.com')",
            {"extra_param"}
        );
    }, std::runtime_error);
}

TEST_F(MySQLTestFixture, NoPlaceholdersNoParams) {
    // 正常情况：没有占位符也没有参数
    uint64_t affected = conn_->Execute(
        "INSERT INTO test_users (username, email) VALUES ('direct_user', 'direct@example.com')"
    );
    
    EXPECT_EQ(affected, 1);
}

// ==================== MySQLResult 基础测试 ====================

TEST_F(MySQLTestFixture, ResultMoveConstructor) {
    conn_->Execute("INSERT INTO test_users (username) VALUES (?)", {"move_test"});
    
    auto result1 = conn_->Query("SELECT * FROM test_users WHERE username = ?", {"move_test"});
    EXPECT_FALSE(result1.Empty());
    
    // 移动构造
    user::MySQLResult result2(std::move(result1));
    EXPECT_FALSE(result2.Empty());
    
    // result1 已失效
    EXPECT_TRUE(result1.Empty());
}

TEST_F(MySQLTestFixture, ResultMoveAssignment) {
    conn_->Execute("INSERT INTO test_users (username) VALUES (?)", {"move_test"});
    
    auto result1 = conn_->Query("SELECT * FROM test_users WHERE username = ?", {"move_test"});
    auto result2 = conn_->Query("SELECT * FROM test_users WHERE id = ?", {999});  // 空结果
    
    EXPECT_FALSE(result1.Empty());
    EXPECT_TRUE(result2.Empty());
    
    // 移动赋值
    result2 = std::move(result1);
    EXPECT_FALSE(result2.Empty());
    EXPECT_TRUE(result1.Empty());
}

TEST_F(MySQLTestFixture, ResultFieldCount) {
    conn_->Execute(
        "INSERT INTO test_users (username, email, age) VALUES (?, ?, ?)",
        {"field_test", "field@example.com", 25}
    );
    
    auto result = conn_->Query("SELECT username, email, age FROM test_users WHERE username = ?", 
                                {"field_test"});
    
    EXPECT_EQ(result.FieldCount(), 3);
}

TEST_F(MySQLTestFixture, ResultColumnOutOfRange) {
    conn_->Execute("INSERT INTO test_users (username) VALUES (?)", {"col_test"});
    
    auto result = conn_->Query("SELECT username FROM test_users WHERE username = ?", {"col_test"});
    ASSERT_TRUE(result.Next());
    
    // 访问超出范围的列
    EXPECT_THROW({
        result.GetString(10);
    }, std::out_of_range);
}

TEST_F(MySQLTestFixture, ResultNoCurrentRow) {
    conn_->Execute("INSERT INTO test_users (username) VALUES (?)", {"no_row_test"});
    
    auto result = conn_->Query("SELECT username FROM test_users WHERE username = ?", {"no_row_test"});
    
    // 还没调用 Next()
    EXPECT_THROW({
        result.GetString(0);
    }, std::runtime_error);
}

TEST_F(MySQLTestFixture, ResultBinaryData) {
    // 插入包含二进制数据的字符串（含 \0）
    std::string binary_data = "binary\0data\0test";
    binary_data.resize(17);  // 确保包含 \0
    
    conn_->Execute(
        "INSERT INTO test_users (username, email) VALUES (?, ?)",
        {"binary_user", binary_data}
    );
    
    auto result = conn_->Query("SELECT email FROM test_users WHERE username = ?", {"binary_user"});
    ASSERT_TRUE(result.Next());
    
    auto retrieved = result.GetString(0);
    ASSERT_TRUE(retrieved.has_value());
    
    // 验证长度（不应被 \0 截断）
    EXPECT_EQ(retrieved.value().size(), binary_data.size());
}


// ==================== 连接池基础测试 ====================

class ConnectionPoolTest : public MySQLTestFixture {
    protected:
        void SetUp() override {
            MySQLTestFixture::SetUp();
            
            // 创建连接池
            pool_ = std::make_unique<user::TemplateConnectionPool<user::MySQLConnection>>(
                std::make_shared<user::Config>(config_),
                [](const user::MySQLConfig& cfg) {
                    return std::make_unique<user::MySQLConnection>(cfg);
                }
            );
        }
    
        std::unique_ptr<user::TemplateConnectionPool<user::MySQLConnection>> pool_;
    };
    
    TEST_F(ConnectionPoolTest, AcquireAndRelease) {
        auto guard = pool_->CreateConnection();
        EXPECT_NE(guard.operator->(), nullptr);
        
        // guard 析构时自动归还
    }
    
    TEST_F(ConnectionPoolTest, MultipleAcquire) {
        std::vector<user::TemplateConnectionPool<user::MySQLConnection>::ConnectionGuard> guards;
        
        // 获取多个连接（不超过池大小）
        for (int i = 0; i < config_.mysql.pool_size; ++i) {
            guards.push_back(pool_->CreateConnection());
        }
        
        EXPECT_EQ(guards.size(), config_.mysql.pool_size);
    }
    
    TEST_F(ConnectionPoolTest, PoolExhaustion) {
        std::vector<user::TemplateConnectionPool<user::MySQLConnection>::ConnectionGuard> guards;
        
        // 耗尽连接池
        for (int i = 0; i < config_.mysql.pool_size; ++i) {
            guards.push_back(pool_->CreateConnection());
        }
        
        // 再次获取应该超时（等待 5 秒）
        auto start = std::chrono::steady_clock::now();
        
        EXPECT_THROW({
            auto guard = pool_->CreateConnection();
        }, std::runtime_error);
        
        auto duration = std::chrono::steady_clock::now() - start;
        EXPECT_GE(duration, std::chrono::seconds(5));
    }
    
    TEST_F(ConnectionPoolTest, ConcurrentAccess) {
        const int num_threads = 10;
        const int ops_per_thread = 100;
        std::atomic<int> success_count{0};
        
        std::vector<std::thread> threads;
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < ops_per_thread; ++j) {
                    try {
                        auto guard = pool_->CreateConnection();
                        
                        // 执行操作
                        guard->Execute(
                            "INSERT INTO test_users (username) VALUES (?)",
                            {"thread_" + std::to_string(i) + "_op_" + std::to_string(j)}
                        );
                        
                        ++success_count;
                    } catch (const std::exception& e) {
                        // 记录失败但不影响其他线程
                        std::cerr << "Thread " << i << " failed: " << e.what() << std::endl;
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        EXPECT_EQ(success_count, num_threads * ops_per_thread);
    }
    
    TEST_F(ConnectionPoolTest, ConnectionValidation) {
        auto guard = pool_->CreateConnection();
        
        // 验证连接有效
        EXPECT_TRUE(guard->Valid());
        
        // 执行查询验证
        auto result = guard->Query("SELECT 1");
        EXPECT_TRUE(result.Next());
        EXPECT_EQ(result.GetInt(0).value(), 1);
    }
    
    TEST_F(ConnectionPoolTest, InvalidConnectionRebuild) {
        // 这个测试需要模拟连接失效，实际环境中较难测试
        // 可以通过重启 MySQL 或手动 kill 连接来模拟
        
        auto guard = pool_->CreateConnection();
        EXPECT_TRUE(guard->Valid());
        
        // 模拟连接失效（需要手动操作）
        // 然后归还，连接池应该重建它
    }

    