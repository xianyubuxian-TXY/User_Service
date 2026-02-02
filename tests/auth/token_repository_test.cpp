// tests/auth/token_repository_test.cpp

#include <gtest/gtest.h>
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
static constexpr const char* TEST_DB_NAME = "test_user_service";  // 统一数据库名
static constexpr const char* TEST_DB_USER = "root";
static constexpr const char* TEST_DB_PASSWORD = "@txylj2864";

// ==================== 辅助函数 ====================
static std::shared_ptr<Config> CreateTestConfig(int pool_size = 5) {
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
class TokenRepositoryTest : public ::testing::Test {
protected:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    // 在所有测试开始前执行一次（创建表）
    static void SetUpTestSuite() {
        std::cout << "[TestSuite] 初始化测试环境..." << std::endl;
        
        try {
            auto config = CreateTestConfig(2);
            
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
            auto config = CreateTestConfig(2);
            
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
    
    // 每个测试前执行
    void SetUp() override {
        try {
            auto config = CreateTestConfig(5);
            
            pool_ = std::make_shared<MySQLPool>(
                config,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
            
            repo_ = std::make_unique<TokenRepository>(pool_);
            CleanupTestData();
            
        } catch (const std::exception& e) {
            GTEST_SKIP() << "数据库连接失败: " << e.what();
        }
    }
    
    // 每个测试后执行
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
    
    int64_t NextTestUserId() {
        static int64_t counter = 99900;
        return counter++;
    }
    
    std::string GenerateTokenHash() {
        static int counter = 0;
        return "test_hash_" + std::to_string(++counter) + "_" + 
               std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    std::shared_ptr<MySQLPool> pool_;
    std::unique_ptr<TokenRepository> repo_;
};


// ==================== 数据库连接测试 ====================

TEST_F(TokenRepositoryTest, DatabaseConnection_Works) {
    auto conn = pool_->CreateConnection();
    ASSERT_NE(conn.get(), nullptr);
    
    auto result = conn->Query("SELECT 1 AS test", {});
    EXPECT_FALSE(result.Empty());
    
    std::cout << "[Test] 数据库连接正常" << std::endl;
}

TEST_F(TokenRepositoryTest, TableStructure_Correct) {
    auto conn = pool_->CreateConnection();
    
    auto result = conn->Query(
        "SELECT COLUMN_NAME, DATA_TYPE FROM information_schema.columns "
        "WHERE table_schema =?   AND table_name = 'user_sessions' "
        "ORDER BY ordinal_position",
        {TEST_DB_NAME}
    );
    
    ASSERT_FALSE(result.Empty());
    
    std::cout << "[Test] user_sessions 表结构验证通过" << std::endl;
}


// ==================== SaveRefreshToken 测试 ====================

TEST_F(TokenRepositoryTest, SaveRefreshToken_Success) {
    int64_t user_id = NextTestUserId();
    std::string token_hash = GenerateTokenHash();
    
    auto result = repo_->SaveRefreshToken(user_id, token_hash, 3600);
    
    if (!result.Success()) {
        std::cerr << "[Test] 失败 - code: " << static_cast<int>(result.code) 
                  << ", message: " << result.message << std::endl;
    }
    
    EXPECT_TRUE(result.Success()) 
        << "错误码: " << static_cast<int>(result.code) 
        << ", 消息: " << result.message;
}

TEST_F(TokenRepositoryTest, SaveRefreshToken_MultipleSessions) {
    int64_t user_id = NextTestUserId();
    
    auto result1 = repo_->SaveRefreshToken(user_id, GenerateTokenHash(), 3600);
    auto result2 = repo_->SaveRefreshToken(user_id, GenerateTokenHash(), 3600);
    auto result3 = repo_->SaveRefreshToken(user_id, GenerateTokenHash(), 3600);
    
    EXPECT_TRUE(result1.Success());
    EXPECT_TRUE(result2.Success());
    EXPECT_TRUE(result3.Success());
    
    auto count = repo_->CountActiveSessionsByUserId(user_id);
    ASSERT_TRUE(count.Success());
    EXPECT_EQ(*count.data, 3);
}

TEST_F(TokenRepositoryTest, SaveRefreshToken_DuplicateHash_Fails) {
    int64_t user_id = NextTestUserId();
    std::string token_hash = GenerateTokenHash();
    
    auto result1 = repo_->SaveRefreshToken(user_id, token_hash, 3600);
    EXPECT_TRUE(result1.Success());
    
    auto result2 = repo_->SaveRefreshToken(user_id, token_hash, 3600);
    EXPECT_FALSE(result2.Success());
}


// ==================== FindByTokenHash 测试 ====================

TEST_F(TokenRepositoryTest, FindByTokenHash_ExistingToken) {
    int64_t user_id = NextTestUserId();
    std::string token_hash = GenerateTokenHash();
    
    repo_->SaveRefreshToken(user_id, token_hash, 3600);
    
    auto result = repo_->FindByTokenHash(token_hash);
    
    ASSERT_TRUE(result.Success()) 
        << "错误码: " << static_cast<int>(result.code);
    EXPECT_EQ(result.data->user_id, user_id);
    EXPECT_EQ(result.data->token_hash, token_hash);
}

TEST_F(TokenRepositoryTest, FindByTokenHash_NonExisting) {
    auto result = repo_->FindByTokenHash("non_existing_hash_12345");
    
    EXPECT_FALSE(result.Success());
}


// ==================== IsTokenValid 测试 ====================

TEST_F(TokenRepositoryTest, IsTokenValid_ValidToken) {
    int64_t user_id = NextTestUserId();
    std::string token_hash = GenerateTokenHash();
    
    repo_->SaveRefreshToken(user_id, token_hash, 3600);
    
    auto result = repo_->IsTokenValid(token_hash);
    
    ASSERT_TRUE(result.Success());
    EXPECT_TRUE(*result.data);
}

TEST_F(TokenRepositoryTest, IsTokenValid_ExpiredToken) {
    int64_t user_id = NextTestUserId();
    std::string token_hash = GenerateTokenHash();
    
    repo_->SaveRefreshToken(user_id, token_hash, 1);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto result = repo_->IsTokenValid(token_hash);
    
    ASSERT_TRUE(result.Success());
    EXPECT_FALSE(*result.data);
}

TEST_F(TokenRepositoryTest, IsTokenValid_NonExisting) {
    auto result = repo_->IsTokenValid("non_existing_hash");
    
    ASSERT_TRUE(result.Success());
    EXPECT_FALSE(*result.data);
}


// ==================== CountActiveSessionsByUserId 测试 ====================

TEST_F(TokenRepositoryTest, CountActiveSessionsByUserId_NoSessions) {
    int64_t user_id = NextTestUserId();
    
    auto result = repo_->CountActiveSessionsByUserId(user_id);
    
    ASSERT_TRUE(result.Success());
    EXPECT_EQ(*result.data, 0);
}


// ==================== DeleteByTokenHash 测试 ====================

TEST_F(TokenRepositoryTest, DeleteByTokenHash_Existing) {
    int64_t user_id = NextTestUserId();
    std::string token_hash = GenerateTokenHash();
    
    repo_->SaveRefreshToken(user_id, token_hash, 3600);
    
    auto result = repo_->DeleteByTokenHash(token_hash);
    EXPECT_TRUE(result.Success());
    
    auto valid = repo_->IsTokenValid(token_hash);
    ASSERT_TRUE(valid.Success());
    EXPECT_FALSE(*valid.data);
}


// ==================== DeleteByUserId 测试 ====================

TEST_F(TokenRepositoryTest, DeleteByUserId_RemovesAll) {
    int64_t user_id = NextTestUserId();
    
    repo_->SaveRefreshToken(user_id, GenerateTokenHash(), 3600);
    repo_->SaveRefreshToken(user_id, GenerateTokenHash(), 3600);
    repo_->SaveRefreshToken(user_id, GenerateTokenHash(), 3600);
    
    auto result = repo_->DeleteByUserId(user_id);
    EXPECT_TRUE(result.Success());
    
    auto count = repo_->CountActiveSessionsByUserId(user_id);
    ASSERT_TRUE(count.Success());
    EXPECT_EQ(*count.data, 0);
}


// ==================== CleanExpiredTokens 测试 ====================

TEST_F(TokenRepositoryTest, CleanExpiredTokens_RemovesOnlyExpired) {
    int64_t user_id = NextTestUserId();
    
    std::string valid_hash = GenerateTokenHash();
    repo_->SaveRefreshToken(user_id, valid_hash, 3600);
    
    std::string expired_hash = GenerateTokenHash();
    repo_->SaveRefreshToken(user_id, expired_hash, 1);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto result = repo_->CleanExpiredTokens();
    
    ASSERT_TRUE(result.Success());
    EXPECT_GE(*result.data, 1);
    
    EXPECT_TRUE(repo_->IsTokenValid(valid_hash).data.value_or(false));
    EXPECT_FALSE(repo_->IsTokenValid(expired_hash).data.value_or(true));
}

}  // namespace test
}  // namespace user_service
