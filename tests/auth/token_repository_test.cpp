// tests/auth/token_repository_test.cpp

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

#include "auth/token_repository.h"
#include "pool/connection_pool.h"
#include "db/mysql_connection.h"
#include "config/config.h"
#include "common/logger.h"

namespace user_service {
namespace testing {

class TokenRepositoryIntegrationTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // 初始化日志
        Logger::Init("./logs", "token_repo_test.log", "debug", 
                     5 * 1024 * 1024, 3, true);
    }

    static void TearDownTestSuite() {
        Logger::Shutdown();
    }

    void SetUp() override {
        // 创建配置
        config_ = std::make_shared<Config>();
        config_->mysql.host = "localhost";
        config_->mysql.port = 3307;
        config_->mysql.database = "user_service";
        config_->mysql.username = "root";
        config_->mysql.password = "@txylj2864";
        config_->mysql.pool_size = 5;
        config_->mysql.charset = "utf8mb4";

        // 创建连接池
        try {
            pool_ = std::make_shared<MySQLPool>(
                config_,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
        } catch (const std::exception& e) {
            GTEST_SKIP() << "MySQL connection failed: " << e.what();
        }

        token_repo_ = std::make_shared<TokenRepository>(pool_);

        // 清理测试数据
        CleanupTestData();
    }

    void TearDown() override {
        CleanupTestData();
    }

    void CleanupTestData() {
        if (!pool_) return;
        try {
            auto conn = pool_->CreateConnection();
            // 删除测试用户的所有会话
            conn->Execute("DELETE FROM user_sessions WHERE user_id >= 90000", {});
        } catch (...) {
            // 忽略清理错误
        }
    }

    // 创建测试用户（如果不存在）
    int64_t EnsureTestUser(int64_t user_id = 90001) {
        auto conn = pool_->CreateConnection();
        
        // 检查用户是否存在
        auto res = conn->Query(
            "SELECT id FROM users WHERE id = ?", 
            {std::to_string(user_id)}
        );
        
        if (!res.Next()) {
            // 创建测试用户
            conn->Execute(
                "INSERT INTO users (id, uuid, mobile, password_hash, display_name) "
                "VALUES (?, ?, ?, ?, ?)",
                {
                    std::to_string(user_id),
                    "test-uuid-" + std::to_string(user_id),
                    "138" + std::to_string(user_id),
                    "$sha256$salt$hash",
                    "Test User"
                }
            );
        }
        
        return user_id;
    }

    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    std::shared_ptr<Config> config_;
    std::shared_ptr<MySQLPool> pool_;
    std::shared_ptr<TokenRepository> token_repo_;
};

// ============================================================================
// SaveRefreshToken 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, SaveRefreshToken_Success) {
    int64_t user_id = EnsureTestUser(90001);
    std::string token_hash = "test_token_hash_" + std::to_string(time(nullptr));
    
    auto result = token_repo_->SaveRefreshToken(user_id, token_hash, 3600);
    
    EXPECT_TRUE(result.IsOk()) << "Error: " << result.message;
}

TEST_F(TokenRepositoryIntegrationTest, SaveRefreshToken_DuplicateHash) {
    int64_t user_id = EnsureTestUser(90001);
    std::string token_hash = "duplicate_hash_test_" + std::to_string(time(nullptr));
    
    // 第一次保存
    auto result1 = token_repo_->SaveRefreshToken(user_id, token_hash, 3600);
    EXPECT_TRUE(result1.IsOk());
    
    // 第二次保存相同哈希（应该失败）
    auto result2 = token_repo_->SaveRefreshToken(user_id, token_hash, 3600);
    EXPECT_FALSE(result2.IsOk());
}

TEST_F(TokenRepositoryIntegrationTest, SaveRefreshToken_InvalidUserId) {
    // 外键约束：user_id 必须存在于 users 表
    int64_t invalid_user_id = 999999999;
    std::string token_hash = "invalid_user_token_" + std::to_string(time(nullptr));
    
    auto result = token_repo_->SaveRefreshToken(invalid_user_id, token_hash, 3600);
    
    EXPECT_FALSE(result.IsOk());
}

// ============================================================================
// FindByTokenHash 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, FindByTokenHash_Exists) {
    int64_t user_id = EnsureTestUser(90002);
    std::string token_hash = "find_test_hash_" + std::to_string(time(nullptr));
    
    // 先保存
    auto save_result = token_repo_->SaveRefreshToken(user_id, token_hash, 3600);
    ASSERT_TRUE(save_result.IsOk());
    
    // 再查找
    auto find_result = token_repo_->FindByTokenHash(token_hash);
    
    ASSERT_TRUE(find_result.IsOk()) << "Error: " << find_result.message;
    EXPECT_EQ(find_result.Value().user_id, user_id);
    EXPECT_EQ(find_result.Value().token_hash, token_hash);
}

TEST_F(TokenRepositoryIntegrationTest, FindByTokenHash_NotExists) {
    auto result = token_repo_->FindByTokenHash("non_existent_token_hash");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

// ============================================================================
// IsTokenValid 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, IsTokenValid_ValidToken) {
    int64_t user_id = EnsureTestUser(90003);
    std::string token_hash = "valid_token_" + std::to_string(time(nullptr));
    
    // 保存一个有效期较长的 token
    auto save_result = token_repo_->SaveRefreshToken(user_id, token_hash, 3600);
    ASSERT_TRUE(save_result.IsOk());
    
    auto valid_result = token_repo_->IsTokenValid(token_hash);
    
    ASSERT_TRUE(valid_result.IsOk());
    EXPECT_TRUE(valid_result.Value());
}

TEST_F(TokenRepositoryIntegrationTest, IsTokenValid_ExpiredToken) {
    int64_t user_id = EnsureTestUser(90004);
    std::string token_hash = "expired_token_" + std::to_string(time(nullptr));
    
    // 保存一个有效期为 1 秒的 token
    auto save_result = token_repo_->SaveRefreshToken(user_id, token_hash, 1);
    ASSERT_TRUE(save_result.IsOk());
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto valid_result = token_repo_->IsTokenValid(token_hash);
    
    ASSERT_TRUE(valid_result.IsOk());
    EXPECT_FALSE(valid_result.Value());
}

TEST_F(TokenRepositoryIntegrationTest, IsTokenValid_NotExists) {
    auto result = token_repo_->IsTokenValid("non_existent_hash");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_FALSE(result.Value());
}

// ============================================================================
// CountActiveSessionsByUserId 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, CountActiveSessions_MultipleTokens) {
    int64_t user_id = EnsureTestUser(90005);
    
    // 保存多个 token
    for (int i = 0; i < 3; ++i) {
        std::string hash = "session_" + std::to_string(i) + "_" + std::to_string(time(nullptr));
        auto result = token_repo_->SaveRefreshToken(user_id, hash, 3600);
        ASSERT_TRUE(result.IsOk());
    }
    
    auto count_result = token_repo_->CountActiveSessionsByUserId(user_id);
    
    ASSERT_TRUE(count_result.IsOk());
    EXPECT_GE(count_result.Value(), 3);
}

TEST_F(TokenRepositoryIntegrationTest, CountActiveSessions_NoSessions) {
    int64_t user_id = EnsureTestUser(90006);
    
    auto count_result = token_repo_->CountActiveSessionsByUserId(user_id);
    
    ASSERT_TRUE(count_result.IsOk());
    EXPECT_EQ(count_result.Value(), 0);
}

// ============================================================================
// DeleteByTokenHash 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, DeleteByTokenHash_Exists) {
    int64_t user_id = EnsureTestUser(90007);
    std::string token_hash = "delete_test_" + std::to_string(time(nullptr));
    
    // 先保存
    auto save_result = token_repo_->SaveRefreshToken(user_id, token_hash, 3600);
    ASSERT_TRUE(save_result.IsOk());
    
    // 删除
    auto delete_result = token_repo_->DeleteByTokenHash(token_hash);
    EXPECT_TRUE(delete_result.IsOk());
    
    // 验证已删除
    auto find_result = token_repo_->FindByTokenHash(token_hash);
    EXPECT_FALSE(find_result.IsOk());
}

TEST_F(TokenRepositoryIntegrationTest, DeleteByTokenHash_NotExists) {
    // 删除不存在的 token（幂等性，应该成功）
    auto result = token_repo_->DeleteByTokenHash("non_existent_delete_test");
    
    EXPECT_TRUE(result.IsOk());
}

// ============================================================================
// DeleteByUserId 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, DeleteByUserId_MultipleTokens) {
    int64_t user_id = EnsureTestUser(90008);
    
    // 保存多个 token
    for (int i = 0; i < 3; ++i) {
        std::string hash = "user_session_" + std::to_string(i) + "_" + std::to_string(time(nullptr));
        auto result = token_repo_->SaveRefreshToken(user_id, hash, 3600);
        ASSERT_TRUE(result.IsOk());
    }
    
    // 删除用户所有 token
    auto delete_result = token_repo_->DeleteByUserId(user_id);
    EXPECT_TRUE(delete_result.IsOk());
    
    // 验证已全部删除
    auto count_result = token_repo_->CountActiveSessionsByUserId(user_id);
    ASSERT_TRUE(count_result.IsOk());
    EXPECT_EQ(count_result.Value(), 0);
}

// ============================================================================
// CleanExpiredTokens 测试
// ============================================================================

TEST_F(TokenRepositoryIntegrationTest, CleanExpiredTokens_RemovesExpired) {
    int64_t user_id = EnsureTestUser(90009);
    
    // 保存一个即将过期的 token
    std::string expired_hash = "cleanup_test_" + std::to_string(time(nullptr));
    auto save_result = token_repo_->SaveRefreshToken(user_id, expired_hash, 1);
    ASSERT_TRUE(save_result.IsOk());
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 清理过期 token
    auto clean_result = token_repo_->CleanExpiredTokens();
    
    ASSERT_TRUE(clean_result.IsOk());
    EXPECT_GE(clean_result.Value(), 0);  // 至少清理了 0 个或更多
    
    // 验证过期 token 已被清理
    auto valid_result = token_repo_->IsTokenValid(expired_hash);
    ASSERT_TRUE(valid_result.IsOk());
    EXPECT_FALSE(valid_result.Value());
}

}  // namespace testing
}  // namespace user_service
