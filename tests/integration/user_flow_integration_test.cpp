// tests/integration/user_flow_integration_test.cpp

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <random>

// Service 层
#include "service/auth_service.h"
#include "service/user_service.h"

// Repository 层
#include "db/user_db.h"
#include "auth/token_repository.h"

// 基础设施
#include "pool/connection_pool.h"
#include "db/mysql_connection.h"
#include "cache/redis_client.h"
#include "auth/jwt_service.h"
#include "auth/sms_service.h"
#include "config/config.h"

namespace user_service {
namespace integration_test {

// ==================== 配置常量 ====================
static constexpr const char* TEST_DB_HOST = "localhost";
static constexpr int TEST_DB_PORT = 3306;
static constexpr const char* TEST_DB_NAME = "test_user_service";
static constexpr const char* TEST_DB_USER = "root";
static constexpr const char* TEST_DB_PASSWORD = "@txylj2864";

static constexpr const char* TEST_REDIS_HOST = "localhost";
static constexpr int TEST_REDIS_PORT = 6379;

// ==================== 测试夹具 ====================
class UserFlowIntegrationTest : public ::testing::Test {
protected:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    // ==================== 测试套件初始化 ====================
    static void SetUpTestSuite() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "[集成测试] 初始化测试环境..." << std::endl;
        std::cout << "========================================\n" << std::endl;
        
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
            
            std::cout << "[集成测试] 测试环境初始化完成\n" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "[集成测试] 初始化失败: " << e.what() << std::endl;
            throw;
        }
    }
    
    static void TearDownTestSuite() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "[集成测试] 清理测试环境..." << std::endl;
        std::cout << "========================================\n" << std::endl;
        
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
            
            std::cout << "[集成测试] 测试环境清理完成" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "[集成测试] 清理失败: " << e.what() << std::endl;
        }
    }
    
    // ==================== 每个测试的初始化 ====================
    void SetUp() override {
        try {
            InitializeServices();
            CleanupTestData();
            std::cout << "\n--- 测试开始 ---\n" << std::endl;
        } catch (const std::exception& e) {
            GTEST_SKIP() << "服务初始化失败: " << e.what();
        }
    }
    
    void TearDown() override {
        std::cout << "\n--- 测试结束 ---\n" << std::endl;
        CleanupTestData();
    }
    
private:
    // ==================== 配置创建 ====================
    static std::shared_ptr<Config> CreateTestConfig() {
        auto config = std::make_shared<Config>();
        
        // MySQL 配置
        config->mysql.host = TEST_DB_HOST;
        config->mysql.port = TEST_DB_PORT;
        config->mysql.database = TEST_DB_NAME;
        config->mysql.username = TEST_DB_USER;
        config->mysql.password = TEST_DB_PASSWORD;
        config->mysql.pool_size = 5;
        
        // Redis 配置
        config->redis.host = TEST_REDIS_HOST;
        config->redis.port = TEST_REDIS_PORT;
        config->redis.pool_size = 3;
        
        // JWT 配置
        config->security.jwt_secret = "test-secret-key-32-bytes-long!!!";
        config->security.jwt_issuer = "test-user-service";
        config->security.access_token_ttl_seconds = 3600;
        config->security.refresh_token_ttl_seconds = 604800;
        
        // SMS 配置
        config->sms.code_len = 6;
        config->sms.code_ttl_seconds = 300;
        config->sms.send_interval_seconds = 60;
        config->sms.max_retry_count = 5;
        config->sms.retry_ttl_seconds = 300;
        config->sms.lock_seconds = 600;
        
        // 密码配置
        config->password.min_length = 8;
        config->password.max_length = 32;
        config->password.require_uppercase = true;
        config->password.require_lowercase = true;
        config->password.require_digit = true;
        
        // 登录限制配置
        config->login.max_failed_attempts = 5;
        config->login.lock_duration_seconds = 600;
        config->login.failed_attempts_window = 300;
        
        return config;
    }
    
    // ==================== 服务初始化 ====================
    void InitializeServices() {
        config_ = CreateTestConfig();
        
        // 创建连接池
        mysql_pool_ = std::make_shared<MySQLPool>(
            config_,
            [](const MySQLConfig& cfg) {
                return std::make_unique<MySQLConnection>(cfg);
            }
        );
        
        // 创建 Redis 客户端
        redis_client_ = std::make_shared<RedisClient>(config_->redis);
        
        // 创建 Repository
        user_db_ = std::make_shared<UserDB>(mysql_pool_);
        token_repo_ = std::make_shared<TokenRepository>(mysql_pool_);
        
        // 创建 JWT Service
        jwt_service_ = std::make_shared<JwtService>(config_->security);
        
        // 创建 SMS Service
        sms_service_ = std::make_shared<SmsService>(redis_client_, config_->sms);
        
        // 创建业务 Service
        auth_service_ = std::make_shared<AuthService>(
            config_,
            user_db_,
            redis_client_,
            token_repo_,
            jwt_service_,
            sms_service_
        );
        
        user_service_ = std::make_shared<UserService>(
            config_,
            user_db_,
            token_repo_,
            sms_service_
        );
    }
    
    // ==================== 表操作 ====================
    static void CreateTables(MySQLConnection* conn) {
        const char* create_users = R"(
            CREATE TABLE IF NOT EXISTS users (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                uuid VARCHAR(36) NOT NULL,
                mobile VARCHAR(20) NOT NULL,
                password_hash VARCHAR(255) NOT NULL,
                display_name VARCHAR(100) DEFAULT '',
                avatar_url VARCHAR(500) DEFAULT '',
                role TINYINT DEFAULT 0,
                disabled TINYINT DEFAULT 0,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                
                UNIQUE KEY uk_uuid (uuid),
                UNIQUE KEY uk_mobile (mobile),
                INDEX idx_created_at (created_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )";
        
        const char* create_sessions = R"(
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
        
        conn->Execute(create_users, {});
        conn->Execute(create_sessions, {});
        
        std::cout << "[CreateTables] 数据库表创建成功" << std::endl;
    }
    
    static void DropTables(MySQLConnection* conn) {
        conn->Execute("DROP TABLE IF EXISTS user_sessions", {});
        conn->Execute("DROP TABLE IF EXISTS users", {});
        std::cout << "[DropTables] 数据库表删除成功" << std::endl;
    }
    
    void CleanupTestData() {
        if (mysql_pool_) {
            try {
                auto conn = mysql_pool_->CreateConnection();
                conn->Execute("DELETE FROM user_sessions", {});
                conn->Execute("DELETE FROM users", {});
            } catch (...) {}
        }
    }
    
protected:
    // ==================== 辅助方法 ====================
    
    std::string GenerateTestMobile() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(10000000, 99999999);
        return "138" + std::to_string(dis(gen));
    }
    
    std::string GetSmsCodeFromRedis(const std::string& scene, const std::string& mobile) {
        std::string key = "sms:code:" + scene + ":" + mobile;
        auto result = redis_client_->Get(key);
        if (result.IsOk() && result.data.has_value() && result.data.value().has_value()) {
            return result.data.value().value();
        }
        return "";
    }
    
    // ✅ 清除所有 SMS 相关缓存（测试专用，绕过 60 秒间隔限制）
    void ClearAllSmsCache(const std::string& mobile) {
        // 清除间隔限制（全局，不分场景）
        redis_client_->Del("sms:interval:" + mobile);
        
        // 清除各场景的验证码
        redis_client_->Del("sms:code:register:" + mobile);
        redis_client_->Del("sms:code:login:" + mobile);
        redis_client_->Del("sms:code:reset_password:" + mobile);
        redis_client_->Del("sms:code:delete_user:" + mobile);
        
        // 清除各场景的验证次数
        redis_client_->Del("sms:verify_count:register:" + mobile);
        redis_client_->Del("sms:verify_count:login:" + mobile);
        redis_client_->Del("sms:verify_count:reset_password:" + mobile);
        redis_client_->Del("sms:verify_count:delete_user:" + mobile);
        
        // 清除各场景的锁定
        redis_client_->Del("sms:lock:register:" + mobile);
        redis_client_->Del("sms:lock:login:" + mobile);
        redis_client_->Del("sms:lock:reset_password:" + mobile);
        redis_client_->Del("sms:lock:delete_user:" + mobile);
    }
    
    void PrintStep(const std::string& step) {
        std::cout << "\n>>> " << step << std::endl;
    }
    
    void PrintSuccess(const std::string& msg) {
        std::cout << "    [OK] " << msg << std::endl;
    }
    
    void PrintInfo(const std::string& msg) {
        std::cout << "    [INFO] " << msg << std::endl;
    }
    
    void PrintError(const std::string& msg) {
        std::cerr << "    [ERROR] " << msg << std::endl;
    }
    
    // ==================== 成员变量 ====================
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


// ============================================================================
// 诊断测试
// ============================================================================

TEST_F(UserFlowIntegrationTest, DiagnosticTest) {
    std::cout << "\n[DIAGNOSTIC] Testing Database Connection and Operations\n" << std::endl;
    
    // 1. 测试连接
    PrintStep("1. Test MySQL Connection");
    try {
        auto conn = mysql_pool_->CreateConnection();
        ASSERT_TRUE(conn->Valid()) << "Connection is not valid";
        PrintSuccess("Connection OK");
    } catch (const std::exception& e) {
        PrintError(std::string("Exception: ") + e.what());
        FAIL();
    }
    
    // 2. 测试简单查询
    PrintStep("2. Test Simple Query");
    try {
        auto conn = mysql_pool_->CreateConnection();
        auto res = conn->Query("SELECT 1 AS test", {});
        ASSERT_TRUE(res.Next()) << "No result from SELECT 1";
        PrintSuccess("Simple query OK");
    } catch (const std::exception& e) {
        PrintError(std::string("Exception: ") + e.what());
        FAIL();
    }
    
    // 3. 测试表结构
    PrintStep("3. Test Table Exists");
    try {
        auto conn = mysql_pool_->CreateConnection();
        auto res = conn->Query("DESCRIBE users", {});
        int col_count = 0;
        while (res.Next()) {
            col_count++;
        }
        PrintSuccess("Table 'users' has " + std::to_string(col_count) + " columns");
    } catch (const std::exception& e) {
        PrintError(std::string("Exception: ") + e.what());
        FAIL();
    }
    
    // 4. 测试直接插入
    PrintStep("4. Test Direct Insert");
    try {
        auto conn = mysql_pool_->CreateConnection();
        std::string sql = R"(
            INSERT INTO users (uuid, mobile, password_hash, display_name, role) 
            VALUES (?, ?, ?, ?, ?)
        )";
        conn->Execute(sql, {
            "test-uuid-12345",
            "13899999999",
            "hash123",
            "TestName",
            "0"
        });
        PrintSuccess("Direct insert OK");
        
        conn->Execute("DELETE FROM users WHERE uuid = ?", {"test-uuid-12345"});
        PrintSuccess("Cleanup OK");
    } catch (const std::exception& e) {
        PrintError(std::string("Exception: ") + e.what());
        FAIL();
    }
    
    // 5. 测试 UserDB
    PrintStep("5. Test UserDB::Create");
    try {
        UserEntity user;
        user.mobile = "13888888888";
        user.password_hash = "test_hash_value";
        user.display_name = "DirectTest";
        user.role = UserRole::User;
        
        auto result = user_db_->Create(user);
        ASSERT_TRUE(result.IsOk()) << "UserDB::Create failed: " << result.message;
        PrintSuccess("UserDB::Create OK, uuid=" + result.Value().uuid);
        
        user_db_->DeleteByUUID(result.Value().uuid);
    } catch (const std::exception& e) {
        PrintError(std::string("Exception: ") + e.what());
        FAIL();
    }
    
    // 6. 测试 JWT 生成
    PrintStep("6. Test JWT Generation");
    try {
        UserEntity user;
        user.id = 12345;
        user.uuid = "test-uuid-jwt";
        user.mobile = "13800000000";
        user.role = UserRole::User;
        
        auto token_pair = jwt_service_->GenerateTokenPair(user);
        
        ASSERT_FALSE(token_pair.access_token.empty()) << "Access token is empty!";
        ASSERT_FALSE(token_pair.refresh_token.empty()) << "Refresh token is empty!";
        
        PrintSuccess("JWT generation OK");
        PrintInfo("Access token length: " + std::to_string(token_pair.access_token.length()));
        PrintInfo("Refresh token length: " + std::to_string(token_pair.refresh_token.length()));
    } catch (const std::exception& e) {
        PrintError(std::string("Exception: ") + e.what());
        FAIL();
    }
    
    std::cout << "\n[DIAGNOSTIC] All tests passed!\n" << std::endl;
}


// ============================================================================
// 完整用户生命周期测试
// ============================================================================

TEST_F(UserFlowIntegrationTest, CompleteUserLifecycle) {
    std::string test_mobile = GenerateTestMobile();
    std::string test_password = "TestPassword123";
    std::string test_display_name = "TestUser";
    
    std::cout << "\n[TEST] Complete User Lifecycle Test" << std::endl;
    std::cout << "     Test Mobile: " << test_mobile << std::endl;
    
    // ==================== 1. 发送注册验证码 ====================
    PrintStep("1. Send Register Verify Code");
    {
        auto result = auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
        ASSERT_TRUE(result.IsOk()) << "Send code failed: " << result.message;
        PrintSuccess("Code sent, retry after " + std::to_string(result.Value()) + "s");
    }
    
    // ==================== 2. 用户注册 ====================
    PrintStep("2. User Register");
    std::string access_token, refresh_token, user_uuid;
    {
        std::string verify_code = GetSmsCodeFromRedis("register", test_mobile);
        ASSERT_FALSE(verify_code.empty()) << "Cannot get verify code from Redis";
        PrintInfo("Verify Code: " + verify_code);
        
        auto result = auth_service_->Register(test_mobile, verify_code, test_password, test_display_name);
        ASSERT_TRUE(result.IsOk()) << "Register failed: " << result.message;
        
        const auto& auth_result = result.Value();
        access_token = auth_result.tokens.access_token;
        refresh_token = auth_result.tokens.refresh_token;
        user_uuid = auth_result.user.uuid;
        
        PrintSuccess("Register success");
        PrintInfo("User UUID: " + user_uuid);
        PrintInfo("Access Token empty: " + std::string(access_token.empty() ? "YES (ERROR!)" : "NO"));
        PrintInfo("Refresh Token empty: " + std::string(refresh_token.empty() ? "YES (ERROR!)" : "NO"));
        
        if (!access_token.empty()) {
            PrintInfo("Access Token: " + access_token.substr(0, std::min(size_t(50), access_token.length())) + "...");
        }
        
        ASSERT_FALSE(access_token.empty()) << "Access token is empty after registration!";
        ASSERT_FALSE(refresh_token.empty()) << "Refresh token is empty after registration!";
    }
    
    // ==================== 3. 验证 Token ====================
    PrintStep("3. Validate Access Token");
    {
        auto result = auth_service_->ValidateAccessToken(access_token);
        ASSERT_TRUE(result.IsOk()) << "Token validation failed: " << result.message;
        
        const auto& validation = result.Value();
        EXPECT_EQ(validation.user_uuid, user_uuid);
        EXPECT_EQ(validation.mobile, test_mobile);
        
        PrintSuccess("Token validation success");
        PrintInfo("User ID: " + std::to_string(validation.user_id));
    }
    
    // ==================== 4. 获取用户信息 ====================
    PrintStep("4. Get Current User");
    {
        auto result = user_service_->GetCurrentUser(user_uuid);
        ASSERT_TRUE(result.IsOk()) << "Get user failed: " << result.message;
        
        const auto& user = result.Value();
        EXPECT_EQ(user.mobile, test_mobile);
        EXPECT_EQ(user.display_name, test_display_name);
        EXPECT_EQ(user.role, UserRole::User);
        EXPECT_FALSE(user.disabled);
        
        PrintSuccess("Get user success");
        PrintInfo("Display Name: " + user.display_name);
        PrintInfo("Role: " + UserRoleToString(user.role));
    }
    
    // ==================== 5. 修改用户信息 ====================
    PrintStep("5. Update User Info");
    {
        std::string new_display_name = "UpdatedName";
        
        auto result = user_service_->UpdateUser(user_uuid, new_display_name);
        ASSERT_TRUE(result.IsOk()) << "Update user failed: " << result.message;
        
        // 验证修改结果
        auto user_result = user_service_->GetCurrentUser(user_uuid);
        ASSERT_TRUE(user_result.IsOk());
        EXPECT_EQ(user_result.Value().display_name, new_display_name);
        
        PrintSuccess("User info updated");
        PrintInfo("New Display Name: " + new_display_name);
    }
    
    // ==================== 6. 刷新 Token ====================
    PrintStep("6. Refresh Token");
    std::string new_access_token, new_refresh_token;
    {
        auto result = auth_service_->RefreshToken(refresh_token);
        ASSERT_TRUE(result.IsOk()) << "Refresh token failed: " << result.message;
        
        const auto& tokens = result.Value();
        new_access_token = tokens.access_token;
        new_refresh_token = tokens.refresh_token;
        
        EXPECT_NE(new_access_token, access_token);
        EXPECT_NE(new_refresh_token, refresh_token);
        
        PrintSuccess("Token refreshed");
        PrintInfo("New Access Token: " + new_access_token.substr(0, std::min(size_t(50), new_access_token.length())) + "...");
    }
    
    // ==================== 7. 修改密码 ====================
    PrintStep("7. Change Password");
    std::string new_password = "NewPassword456";
    {
        auto result = user_service_->ChangePassword(user_uuid, test_password, new_password);
        ASSERT_TRUE(result.IsOk()) << "Change password failed: " << result.message;
        
        PrintSuccess("Password changed");
    }
    
    // ==================== 8. 使用新密码登录 ====================
    PrintStep("8. Login with New Password");
    {
        auto result = auth_service_->LoginByPassword(test_mobile, new_password);
        ASSERT_TRUE(result.IsOk()) << "Login with new password failed: " << result.message;
        
        EXPECT_EQ(result.Value().user.uuid, user_uuid);
        
        PrintSuccess("Login with new password success");
    }
    
    // ==================== 9. 旧密码登录应该失败 ====================
    PrintStep("9. Verify Old Password Invalid");
    {
        auto result = auth_service_->LoginByPassword(test_mobile, test_password);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.code, ErrorCode::WrongPassword);
        
        PrintSuccess("Old password is invalid (expected)");
    }
    
    // ==================== 10. 登出 ====================
    PrintStep("10. Logout");
    {
        auto result = auth_service_->Logout(new_refresh_token);
        ASSERT_TRUE(result.IsOk()) << "Logout failed: " << result.message;
        
        PrintSuccess("Logout success");
    }
    
    // ==================== 11. 登出后 Token 应该失效 ====================
    PrintStep("11. Verify Token Invalid After Logout");
    {
        auto result = auth_service_->RefreshToken(new_refresh_token);
        EXPECT_FALSE(result.IsOk());
        
        PrintSuccess("Refresh Token is invalid after logout (expected)");
    }
    
    // ==================== 12. 重新登录 ====================
    PrintStep("12. Re-login");
    {
        auto result = auth_service_->LoginByPassword(test_mobile, new_password);
        ASSERT_TRUE(result.IsOk()) << "Re-login failed: " << result.message;
        
        refresh_token = result.Value().tokens.refresh_token;
        
        PrintSuccess("Re-login success");
    }
    
    // ==================== 13. 发送注销验证码 ====================
    PrintStep("13. Send Delete Account Verify Code");
    {
        // ✅ 清除 SMS 缓存，绕过 60 秒间隔限制
        ClearAllSmsCache(test_mobile);
        
        auto result = auth_service_->SendVerifyCode(test_mobile, SmsScene::DeleteUser);
        ASSERT_TRUE(result.IsOk()) << "Send delete code failed: " << result.message 
                                   << " (code: " << static_cast<int>(result.code) << ")";
        
        PrintSuccess("Delete account verify code sent");
    }
    
    // ==================== 14. 注销账户 ====================
    PrintStep("14. Delete Account");
    {
        std::string verify_code = GetSmsCodeFromRedis("delete_user", test_mobile);
        ASSERT_FALSE(verify_code.empty()) << "Cannot get delete verify code";
        PrintInfo("Verify Code: " + verify_code);
        
        auto result = user_service_->DeleteUser(user_uuid, verify_code);
        ASSERT_TRUE(result.IsOk()) << "Delete account failed: " << result.message;
        
        PrintSuccess("Account deleted");
    }
    
    // ==================== 15. 验证账户已删除 ====================
    PrintStep("15. Verify Account Deleted");
    {
        auto user_result = user_service_->GetCurrentUser(user_uuid);
        if (user_result.IsOk()) {
            EXPECT_TRUE(user_result.Value().disabled);
            PrintInfo("User found but disabled");
        } else {
            PrintInfo("User not found (expected for hard delete)");
        }
        
        auto login_result = auth_service_->LoginByPassword(test_mobile, new_password);
        EXPECT_FALSE(login_result.IsOk());
        
        PrintSuccess("Account is deleted, cannot access");
    }
    
    std::cout << "\n[PASS] Complete User Lifecycle Test Passed!\n" << std::endl;
}


// ============================================================================
// 短信验证码登录流程测试
// ============================================================================

TEST_F(UserFlowIntegrationTest, LoginByCodeFlow) {
    std::string test_mobile = GenerateTestMobile();
    std::string test_password = "TestPassword123";
    
    std::cout << "\n[TEST] Login By Code Flow Test" << std::endl;
    std::cout << "     Test Mobile: " << test_mobile << std::endl;
    
    // 先注册一个用户
    PrintStep("Prepare: Register Test User");
    {
        auto send_result = auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
        ASSERT_TRUE(send_result.IsOk()) << "Send register code failed: " << send_result.message;
        
        std::string code = GetSmsCodeFromRedis("register", test_mobile);
        ASSERT_FALSE(code.empty()) << "Cannot get register code from Redis";
        
        auto result = auth_service_->Register(test_mobile, code, test_password, "TestUser");
        ASSERT_TRUE(result.IsOk()) << "Register failed: " << result.message;
        PrintSuccess("Test user registered");
    }
    
    // 发送登录验证码
    PrintStep("1. Send Login Verify Code");
    {
        // ✅ 清除 SMS 缓存，绕过 60 秒间隔限制
        ClearAllSmsCache(test_mobile);
        
        auto result = auth_service_->SendVerifyCode(test_mobile, SmsScene::Login);
        ASSERT_TRUE(result.IsOk()) << "Send login code failed: " << result.message 
                                   << " (code: " << static_cast<int>(result.code) << ")";
        PrintSuccess("Login verify code sent");
    }
    
    // 验证码登录
    PrintStep("2. Login By Code");
    {
        std::string code = GetSmsCodeFromRedis("login", test_mobile);
        ASSERT_FALSE(code.empty()) << "Cannot get login code from Redis";
        PrintInfo("Verify Code: " + code);
        
        auto result = auth_service_->LoginByCode(test_mobile, code);
        ASSERT_TRUE(result.IsOk()) << "Login by code failed: " << result.message;
        
        EXPECT_FALSE(result.Value().tokens.access_token.empty());
        
        PrintSuccess("Login by code success");
    }
    
    std::cout << "\n[PASS] Login By Code Flow Test Passed!\n" << std::endl;
}


// ============================================================================
// 重置密码流程测试
// ============================================================================

TEST_F(UserFlowIntegrationTest, ResetPasswordFlow) {
    std::string test_mobile = GenerateTestMobile();
    std::string original_password = "OriginalPassword123";
    std::string new_password = "NewResetPassword456";
    
    std::cout << "\n[TEST] Reset Password Flow Test" << std::endl;
    std::cout << "     Test Mobile: " << test_mobile << std::endl;
    
    // 先注册一个用户
    PrintStep("Prepare: Register Test User");
    {
        auto send_result = auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
        ASSERT_TRUE(send_result.IsOk()) << "Send register code failed: " << send_result.message;
        
        std::string code = GetSmsCodeFromRedis("register", test_mobile);
        ASSERT_FALSE(code.empty()) << "Cannot get register code from Redis";
        
        auto result = auth_service_->Register(test_mobile, code, original_password, "TestUser");
        ASSERT_TRUE(result.IsOk()) << "Register failed: " << result.message;
        PrintSuccess("Test user registered");
    }
    
    // 发送重置密码验证码
    PrintStep("1. Send Reset Password Verify Code");
    {
        // ✅ 清除 SMS 缓存，绕过 60 秒间隔限制
        ClearAllSmsCache(test_mobile);
        
        auto result = auth_service_->SendVerifyCode(test_mobile, SmsScene::ResetPassword);
        ASSERT_TRUE(result.IsOk()) << "Send reset code failed: " << result.message
                                   << " (code: " << static_cast<int>(result.code) << ")";
        PrintSuccess("Reset password verify code sent");
    }
    
    // 重置密码
    PrintStep("2. Reset Password");
    {
        std::string code = GetSmsCodeFromRedis("reset_password", test_mobile);
        ASSERT_FALSE(code.empty()) << "Cannot get reset code from Redis";
        PrintInfo("Verify Code: " + code);
        
        auto result = auth_service_->ResetPassword(test_mobile, code, new_password);
        ASSERT_TRUE(result.IsOk()) << "Reset password failed: " << result.message;
        
        PrintSuccess("Password reset success");
    }
    
    // 使用新密码登录
    PrintStep("3. Login with New Password");
    {
        auto result = auth_service_->LoginByPassword(test_mobile, new_password);
        ASSERT_TRUE(result.IsOk()) << "Login with new password failed: " << result.message;
        
        PrintSuccess("Login with new password success");
    }
    
    // 旧密码应该失效
    PrintStep("4. Verify Old Password Invalid");
    {
        auto result = auth_service_->LoginByPassword(test_mobile, original_password);
        EXPECT_FALSE(result.IsOk());
        
        PrintSuccess("Old password is invalid (expected)");
    }
    
    std::cout << "\n[PASS] Reset Password Flow Test Passed!\n" << std::endl;
}


// ============================================================================
// 错误处理测试
// ============================================================================

TEST_F(UserFlowIntegrationTest, ErrorHandling) {
    std::string test_mobile = GenerateTestMobile();
    
    std::cout << "\n[TEST] Error Handling Test" << std::endl;
    
    // 1. 未注册手机号登录
    PrintStep("1. Login with Unregistered Mobile");
    {
        auto result = auth_service_->LoginByPassword(test_mobile, "AnyPassword123");
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.code, ErrorCode::WrongPassword);
        PrintSuccess("Unregistered mobile login rejected");
    }
    
    // 2. 错误验证码注册
    PrintStep("2. Register with Wrong Verify Code");
    {
        auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
        
        auto result = auth_service_->Register(test_mobile, "000000", "Password123", "Test");
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
        PrintSuccess("Wrong verify code register rejected");
    }
    
    // 3. 重复注册
    PrintStep("3. Duplicate Register");
    {
        // ✅ 清除 SMS 缓存，绕过 60 秒间隔限制
        ClearAllSmsCache(test_mobile);
        
        // 先正常注册
        auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
        std::string code = GetSmsCodeFromRedis("register", test_mobile);
        auto first_result = auth_service_->Register(test_mobile, code, "Password123", "TestUser");
        ASSERT_TRUE(first_result.IsOk()) << "First register failed: " << first_result.message;
        
        // 尝试再次注册
        auto send_result = auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
        EXPECT_FALSE(send_result.IsOk());
        EXPECT_EQ(send_result.code, ErrorCode::MobileTaken);
        
        PrintSuccess("Duplicate register blocked");
    }
    
    // 4. 弱密码
    PrintStep("4. Register with Weak Password");
    {
        std::string new_mobile = GenerateTestMobile();
        auth_service_->SendVerifyCode(new_mobile, SmsScene::Register);
        std::string code = GetSmsCodeFromRedis("register", new_mobile);
        
        auto result = auth_service_->Register(new_mobile, code, "123", "Test");
        EXPECT_FALSE(result.IsOk());
        PrintSuccess("Weak password rejected");
    }
    
    // 5. 无效 Token
    PrintStep("5. Validate Invalid Token");
    {
        auto result = auth_service_->ValidateAccessToken("invalid.token.here");
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
        PrintSuccess("Invalid token rejected");
    }
    
    std::cout << "\n[PASS] Error Handling Test Passed!\n" << std::endl;
}


// ============================================================================
// 并发测试
// ============================================================================

TEST_F(UserFlowIntegrationTest, ConcurrentOperations) {
    std::cout << "\n[TEST] Concurrent Operations Test" << std::endl;
    
    // 准备：创建一个用户
    std::string test_mobile = GenerateTestMobile();
    std::cout << "     Test Mobile: " << test_mobile << std::endl;
    
    auto send_result = auth_service_->SendVerifyCode(test_mobile, SmsScene::Register);
    ASSERT_TRUE(send_result.IsOk()) << "Send code failed: " << send_result.message;
    
    std::string code = GetSmsCodeFromRedis("register", test_mobile);
    ASSERT_FALSE(code.empty()) << "Cannot get code from Redis";
    
    auto reg_result = auth_service_->Register(test_mobile, code, "Password123", "TestUser");
    ASSERT_TRUE(reg_result.IsOk()) << "Register failed: " << reg_result.message;
    
    std::string user_uuid = reg_result.Value().user.uuid;
    PrintSuccess("Test user created: " + user_uuid);
    
    PrintStep("1. Concurrent Get User Info");
    {
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int i = 0; i < 20; ++i) {
            threads.emplace_back([&]() {
                auto result = user_service_->GetCurrentUser(user_uuid);
                if (result.IsOk()) {
                    success_count++;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        EXPECT_EQ(success_count.load(), 20);
        PrintSuccess("20 concurrent requests all success");
    }
    
    PrintStep("2. Concurrent Login");
    {
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&]() {
                auto result = auth_service_->LoginByPassword(test_mobile, "Password123");
                if (result.IsOk()) {
                    success_count++;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        EXPECT_EQ(success_count.load(), 10);
        PrintSuccess("10 concurrent logins all success");
    }
    
    std::cout << "\n[PASS] Concurrent Operations Test Passed!\n" << std::endl;
}


}  // namespace integration_test
}  // namespace user_service
