// tests/auth/token_cleanup_task_test.cpp

#include <gtest/gtest.h>
#include "auth/token_cleanup_task.h"
#include "auth/token_repository.h"
#include "pool/connection_pool.h"
#include "db/mysql_connection.h"
#include "config/config.h"
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>

namespace user_service {
namespace test {

// ==================== 数据库配置常量 ====================
static constexpr const char* TEST_DB_HOST = "localhost";
static constexpr int TEST_DB_PORT = 3306;
static constexpr const char* TEST_DB_NAME = "test_user_service";
static constexpr const char* TEST_DB_USER = "root";
static constexpr const char* TEST_DB_PASSWORD = "@txylj2864";

// ==================== 辅助函数 ====================
static std::shared_ptr<Config> CreateTestConfig(int pool_size = 2) {
    auto config = std::make_shared<Config>();
    config->mysql.host = TEST_DB_HOST;
    config->mysql.port = TEST_DB_PORT;
    config->mysql.database = TEST_DB_NAME;
    config->mysql.username = TEST_DB_USER;
    config->mysql.password = TEST_DB_PASSWORD;
    config->mysql.pool_size = pool_size;
    return config;
}

// ==================== 测试夹具 ====================
class TokenCleanupTaskTest : public ::testing::Test {
protected:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    // 在所有测试开始前执行一次（创建表）
    static void SetUpTestSuite() {
        std::cout << "[TestSuite] 初始化测试环境..." << std::endl;
        
        try {
            auto config = CreateTestConfig();
            
            auto pool = std::make_shared<MySQLPool>(
                config,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
            
            auto conn = pool->CreateConnection();
            CreateTables(conn.get());
            
            std::cout << "[TestSuite] 测试环境初始化完成" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "[TestSuite] 初始化失败: " << e.what() << std::endl;
            throw;
        }
    }
    
    // 在所有测试结束后执行一次（删除表）
    static void TearDownTestSuite() {
        std::cout << "[TestSuite] 清理测试环境..." << std::endl;
        
        try {
            auto config = CreateTestConfig();
            
            auto pool = std::make_shared<MySQLPool>(
                config,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
            
            auto conn = pool->CreateConnection();
            DropTables(conn.get());
            
            std::cout << "[TestSuite] 测试环境清理完成" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "[TestSuite] 清理失败: " << e.what() << std::endl;
        }
    }
    
    void SetUp() override {
        try {
            auto config = CreateTestConfig();
            
            pool_ = std::make_shared<MySQLPool>(
                config,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
            
            repo_ = std::make_shared<TokenRepository>(pool_);
            CleanupTestData();
            
        } catch (const std::exception& e) {
            GTEST_SKIP() << "数据库连接失败: " << e.what();
        }
    }
    
    void TearDown() override {
        if (pool_) {
            CleanupTestData();
        }
    }
    
private:
    // ==================== 表操作 ====================
    
    static void CreateTables(MySQLConnection* conn) {
        const char* create_user_sessions = R"(
            CREATE TABLE IF NOT EXISTS user_sessions (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                user_id BIGINT NOT NULL,
                token_hash VARCHAR(255) NOT NULL,
                expires_at DATETIME NOT NULL,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                
                UNIQUE KEY uk_token_hash (token_hash),
                INDEX idx_user_id (user_id),
                INDEX idx_expires_at (expires_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )";
        
        conn->Execute(create_user_sessions, {});
        std::cout << "[CreateTables] user_sessions 表创建成功" << std::endl;
    }
    
    static void DropTables(MySQLConnection* conn) {
        conn->Execute("DROP TABLE IF EXISTS user_sessions", {});
        std::cout << "[DropTables] user_sessions 表删除成功" << std::endl;
    }
    
    void CleanupTestData() {
        if (!pool_) return;
        
        try {
            auto conn = pool_->CreateConnection();
            conn->Execute("TRUNCATE TABLE user_sessions", {});
        } catch (const std::exception& e) {
            std::cerr << "[Cleanup] 警告: " << e.what() << std::endl;
        }
    }
    
protected:
    // ==================== 辅助方法 ====================
    
    std::string GenerateTokenHash() {
        static int counter = 0;
        return "cleanup_test_hash_" + std::to_string(++counter) + "_" + 
               std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    std::shared_ptr<MySQLPool> pool_;
    std::shared_ptr<TokenRepository> repo_;
};


// ==================== 基本功能测试 ====================

TEST_F(TokenCleanupTaskTest, StartAndStop) {
    TokenCleanupTask task(repo_, 60);
    
    task.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    task.Stop();
    
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, MultipleStartStop) {
    TokenCleanupTask task(repo_, 60);
    
    for (int i = 0; i < 3; ++i) {
        task.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        task.Stop();
    }
    
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, StopWithoutStart) {
    TokenCleanupTask task(repo_, 60);
    task.Stop();
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, DoubleStart) {
    TokenCleanupTask task(repo_, 60);
    
    task.Start();
    task.Start();  // 第二次 Start 应该无害
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.Stop();
    
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, DoubleStop) {
    TokenCleanupTask task(repo_, 60);
    
    task.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    task.Stop();
    task.Stop();  // 第二次 Stop 应该无害
    
    SUCCEED();
}


// ==================== 清理功能测试 ====================

TEST_F(TokenCleanupTaskTest, CleansExpiredTokens) {
    // 插入一个即将过期的 token
    int64_t user_id = 99999;
    std::string expired_hash = GenerateTokenHash();
    
    repo_->SaveRefreshToken(user_id, expired_hash, 1);  // 1秒后过期
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 启动清理任务（间隔设为1秒以便快速触发）
    TokenCleanupTask task(repo_, 1);
    task.Start();
    
    // 等待清理执行
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    task.Stop();
    
    // 验证已过期的 token 被清理
    auto result = repo_->IsTokenValid(expired_hash);
    ASSERT_TRUE(result.Success());
    EXPECT_FALSE(*result.data);
}

TEST_F(TokenCleanupTaskTest, PreservesValidTokens) {
    int64_t user_id = 99998;
    std::string valid_hash = GenerateTokenHash();
    
    // 插入一个长期有效的 token
    repo_->SaveRefreshToken(user_id, valid_hash, 3600);  // 1小时后过期
    
    // 启动清理任务
    TokenCleanupTask task(repo_, 1);
    task.Start();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    task.Stop();
    
    // 验证有效的 token 仍然存在
    auto result = repo_->IsTokenValid(valid_hash);
    ASSERT_TRUE(result.Success());
    EXPECT_TRUE(*result.data);
}

TEST_F(TokenCleanupTaskTest, MixedTokensCleanup) {
    int64_t user_id = 99997;
    
    std::string valid_hash = GenerateTokenHash();
    std::string expired_hash = GenerateTokenHash();
    
    repo_->SaveRefreshToken(user_id, valid_hash, 3600);   // 有效
    repo_->SaveRefreshToken(user_id, expired_hash, 1);    // 即将过期
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 启动清理
    TokenCleanupTask task(repo_, 1);
    task.Start();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    task.Stop();
    
    // 验证：有效的保留，过期的删除
    EXPECT_TRUE(repo_->IsTokenValid(valid_hash).data.value_or(false));
    EXPECT_FALSE(repo_->IsTokenValid(expired_hash).data.value_or(true));
}

}  // namespace test
}  // namespace user_service
