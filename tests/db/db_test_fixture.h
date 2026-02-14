// tests/db/db_test_fixture.h

#pragma once

#include <gtest/gtest.h>
#include <memory>
#include "db/mysql_connection.h"
#include "db/mysql_result.h"
#include "db/user_db.h"
#include "pool/connection_pool.h"
#include "config/config.h"
#include "common/logger.h"

namespace user_service {
namespace testing {

/**
 * @brief 数据库测试夹具基类
 * 
 * 提供数据库连接和清理功能
 */
class DBTestFixture : public ::testing::Test {
protected:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;

    static void SetUpTestSuite() {
        // 初始化日志（只初始化一次）
        if (!Logger::IsInitialized()) {
            Logger::Init("./logs", "db_test.log", "debug", 
                        5 * 1024 * 1024, 3, true);
        }
    }

    void SetUp() override {
        // 创建测试配置
        config_ = CreateTestConfig();
        
        // 创建连接池
        auto global_config = std::make_shared<Config>();
        global_config->mysql = config_;
        
        pool_ = std::make_shared<MySQLPool>(
            global_config,
            [](const MySQLConfig& cfg) {
                return std::make_unique<MySQLConnection>(cfg);
            }
        );

        // 清理测试数据
        CleanupTestData();
    }

    void TearDown() override {
        // 清理测试数据
        CleanupTestData();
    }

    /**
     * @brief 创建测试配置
     */
    static MySQLConfig CreateTestConfig() {
        MySQLConfig config;
        config.host = "localhost";
        config.port = 3307;
        config.database = "user_service";
        config.username = "root";
        config.password = "@txylj2864";
        config.pool_size = 5;
        config.connection_timeout_ms = 5000;
        config.read_timeout_ms = 30000;
        config.write_timeout_ms = 30000;
        config.auto_reconnect = true;
        config.charset = "utf8mb4";
        config.max_retries = 3;
        config.retry_interval_ms = 1000;
        return config;
    }

    /**
     * @brief 清理测试数据
     */
    void CleanupTestData() {
        try {
            auto conn = pool_->CreateConnection();
            // 删除测试用户（手机号以 199 开头的）
            conn->Execute("DELETE FROM users WHERE mobile LIKE '199%'", {});
            // 删除测试会话
            conn->Execute("DELETE FROM user_sessions WHERE user_id NOT IN (SELECT id FROM users)", {});
        } catch (const std::exception& e) {
            // 忽略清理错误
            std::cerr << "Cleanup warning: " << e.what() << std::endl;
        }
    }

    /**
     * @brief 创建测试用户
     */
    std::string CreateTestUser(const std::string& mobile, 
                               const std::string& display_name = "Test User") {
        auto conn = pool_->CreateConnection();
        
        std::string uuid = "test-uuid-" + mobile;
        std::string password_hash = "$sha256$salt$hash";
        
        conn->Execute(
            "INSERT INTO users (uuid, mobile, display_name, password_hash, role, disabled) "
            "VALUES (?, ?, ?, ?, 0, 0)",
            {uuid, mobile, display_name, password_hash}
        );
        
        return uuid;
    }

    /**
     * @brief 获取用户ID
     */
    int64_t GetUserIdByMobile(const std::string& mobile) {
        auto conn = pool_->CreateConnection();
        auto res = conn->Query("SELECT id FROM users WHERE mobile = ?", {mobile});
        if (res.Next()) {
            return res.GetInt("id").value_or(0);
        }
        return 0;
    }

protected:
    MySQLConfig config_;
    std::shared_ptr<MySQLPool> pool_;
};

} // namespace testing
} // namespace user_service
