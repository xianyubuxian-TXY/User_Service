#pragma once

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdlib>

#include "config/config.h"
#include "db/user_db.h"
#include "db/mysql_connection.h"
#include "pool/connection_pool.h"
#include "cache/redis_client.h"
#include "auth/token_repository.h"
#include "auth/jwt_service.h"
#include "auth/sms_service.h"
#include "service/auth_service.h"
#include "service/user_service.h"
#include "common/password_helper.h"
#include "common/auth_type.h"

namespace user_service {
namespace test {

// ============================================================================
// 测试基类 Fixture
// ============================================================================

class ServiceTestFixture : public ::testing::Test {
protected:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;

    void SetUp() override {
        CreateConfig();
        CreateMySQLPool();
        CreateRedisClient();
        CreateServices();
        CreateTestTables();
        CleanupTestData();
    }

    void TearDown() override {
        CleanupTestData();
    }

    // ==================== 配置初始化 ====================

    void CreateConfig() {
        config_ = std::make_shared<Config>();
        
        // MySQL 配置
        config_->mysql.host = GetEnv("TEST_MYSQL_HOST", "localhost");
        config_->mysql.port = std::stoi(GetEnv("TEST_MYSQL_PORT", "3306"));
        config_->mysql.username = "root";
        config_->mysql.password = "@txylj2864";
        config_->mysql.database = "test_user_service";
        config_->mysql.pool_size = 5;
        config_->mysql.connection_timeout_ms = 5000;
        config_->mysql.read_timeout_ms = 3000;
        config_->mysql.write_timeout_ms = 3000;
        config_->mysql.charset = "utf8mb4";
        
        // Redis 配置
        config_->redis.host = GetEnv("TEST_REDIS_HOST", "localhost");
        config_->redis.port = std::stoi(GetEnv("TEST_REDIS_PORT", "6379"));
        config_->redis.password = "";
        config_->redis.db = 0;
        config_->redis.pool_size = 5;
        
        // 安全配置
        config_->security.jwt_secret = "test_jwt_secret_key_for_unit_testing_12345";
        config_->security.access_token_ttl_seconds = 7200;
        config_->security.refresh_token_ttl_seconds = 604800;
        
        // 密码配置
        config_->password.min_length = 6;
        config_->password.max_length = 32;
        config_->password.require_digit = false;
        config_->password.require_lowercase = false;
        config_->password.require_uppercase = false;
        config_->password.require_special_char = false;
        
        // 登录限制配置
        config_->login.max_failed_attempts = 5;
        config_->login.lock_duration_seconds = 1800;
        config_->login.failed_attempts_window = 300;
        
        // 短信配置
        config_->sms.code_len = 6;
        config_->sms.code_ttl_seconds = 300;
        config_->sms.send_interval_seconds = 60;
        config_->sms.max_retry_count = 5;
        config_->sms.retry_ttl_seconds = 300;
        config_->sms.lock_seconds = 1800;
    }

    void CreateMySQLPool() {
        mysql_pool_ = std::make_shared<MySQLPool>(
            config_,
            [](const MySQLConfig& cfg) {
                return std::make_unique<MySQLConnection>(cfg);
            }
        );
    }

    void CreateRedisClient() {
        redis_client_ = std::make_shared<RedisClient>(config_->redis);
    }

    void CreateServices() {
        user_db_ = std::make_shared<UserDB>(mysql_pool_);
        token_repo_ = std::make_shared<TokenRepository>(mysql_pool_);
        jwt_service_ = std::make_shared<JwtService>(config_->security);
        sms_service_ = std::make_shared<SmsService>(redis_client_, config_->sms);
        
        auth_service_ = std::make_shared<AuthService>(
            config_, user_db_, redis_client_, token_repo_, jwt_service_, sms_service_
        );
        
        user_service_ = std::make_shared<UserService>(
            config_, user_db_, token_repo_, sms_service_
        );
    }

    void CreateTestTables() {
        auto conn = mysql_pool_->CreateConnection();
        
        conn->Execute(R"(
            CREATE TABLE IF NOT EXISTS users (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                uuid VARCHAR(36) NOT NULL,
                mobile VARCHAR(20) NOT NULL,
                password_hash VARCHAR(128) NOT NULL,
                display_name VARCHAR(64),
                disabled TINYINT(1) DEFAULT 0,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                UNIQUE KEY uk_uuid (uuid),
                UNIQUE KEY uk_mobile (mobile),
                INDEX idx_created_at (created_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        
        // 用户会话表（存储 refresh token）
        conn->Execute(R"(
            CREATE TABLE IF NOT EXISTS user_sessions (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                user_id BIGINT NOT NULL,
                token_hash VARCHAR(64) NOT NULL,
                expires_at TIMESTAMP NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_token_hash (token_hash),
                INDEX idx_user_id (user_id),
                INDEX idx_expires_at (expires_at),
                FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
        
    }

    void CleanupTestData() {
        auto conn = mysql_pool_->CreateConnection();
        
        // 注意清理顺序：先清理有外键依赖的表
        conn->Execute("SET FOREIGN_KEY_CHECKS = 0");
        conn->Execute("TRUNCATE TABLE user_sessions");
        conn->Execute("TRUNCATE TABLE users");
        conn->Execute("SET FOREIGN_KEY_CHECKS = 1");
        
        CleanupRedisTestData();
    }
    
    void CleanupRedisTestData() {
        // 清理所有测试相关的 Redis key
        std::vector<std::string> patterns = {
            "sms:code:*",
            "sms:interval:*",
            "sms:verify_count:*",
            "sms:lock:*",
            "login:fail:*"
        };
        
        for (const auto& pattern : patterns) {
            auto keys_res = redis_client_->Keys(pattern);
            if (keys_res.IsOk() && keys_res.data.has_value()) {
                for (const auto& key : keys_res.data.value()) {
                    redis_client_->Del(key);
                }
            }
        }
    }

    // ==================== 辅助方法 ====================

    std::string GetEnv(const char* name, const std::string& default_val) {
        const char* val = std::getenv(name);
        return val ? val : default_val;
    }

    // 创建测试用户（直接插入数据库）
    void CreateTestUser(const std::string& mobile, 
                        const std::string& password,
                        const std::string& display_name,
                        bool disabled = false) {
        auto conn = mysql_pool_->CreateConnection();
        std::string uuid = "test_uuid_" + mobile;
        std::string password_hash = PasswordHelper::Hash(password);
        
        std::string sql = "INSERT INTO users (uuid, mobile, password_hash, display_name, disabled) "
                          "VALUES ('" + uuid + "', '" + mobile + "', '" + password_hash + 
                          "', '" + display_name + "', " + (disabled ? "1" : "0") + ")";
        conn->Execute(sql);
    }

    // ==================== 验证码辅助方法 ====================
    
    /// @brief 直接在 Redis 中设置测试验证码（绕过真实发送）
    void SendTestVerifyCode(SmsScene scene, const std::string& mobile, 
                            const std::string& code = "123456") {
        // 清除可能存在的限制
        ClearSendInterval(mobile);
        ClearVerifyFailCount(scene, mobile);
        ClearSceneLock(scene, mobile);
        
        // 设置验证码
        std::string scene_str = GetSceneString(scene);
        std::string key = "sms:code:" + scene_str + ":" + mobile;
        
        redis_client_->SetPx(key, code, std::chrono::seconds(300));
    }
    
    /// @brief 获取测试验证码（固定值）
    std::string GetTestVerifyCode() {
        return "123456";
    }
    
    /// @brief 清除发送间隔限制（允许立即重发）
    void ClearSendInterval(const std::string& mobile) {
        std::string key = "sms:interval:" + mobile;
        redis_client_->Del(key);
    }
    
    /// @brief 清除验证失败次数
    void ClearVerifyFailCount(SmsScene scene, const std::string& mobile) {
        std::string scene_str = GetSceneString(scene);
        std::string key = "sms:verify_count:" + scene_str + ":" + mobile;
        redis_client_->Del(key);
    }
    
    /// @brief 清除场景锁定
    void ClearSceneLock(SmsScene scene, const std::string& mobile) {
        std::string scene_str = GetSceneString(scene);
        std::string key = "sms:lock:" + scene_str + ":" + mobile;
        redis_client_->Del(key);
    }
    
    /// @brief 清除登录失败记录
    void ClearLoginFailure(const std::string& mobile) {
        std::string key = "login:fail:" + mobile;
        redis_client_->Del(key);
    }

private:
    std::string GetSceneString(SmsScene scene) {
        switch (scene) {
            case SmsScene::Register:      return "register";
            case SmsScene::Login:         return "login";
            case SmsScene::ResetPassword: return "reset_password";
            case SmsScene::DeleteUser:    return "delete_user";
            default:                      return "unknown";
        }
    }

protected:
    std::shared_ptr<Config> config_;
    std::shared_ptr<MySQLPool> mysql_pool_;
    std::shared_ptr<RedisClient> redis_client_;
    std::shared_ptr<UserDB> user_db_;
    std::shared_ptr<TokenRepository> token_repo_;
    std::shared_ptr<JwtService> jwt_service_;
    std::shared_ptr<SmsService> sms_service_;
    std::shared_ptr<AuthService> auth_service_;
    std::shared_ptr<UserService> user_service_;
};

}  // namespace test
}  // namespace user_service
