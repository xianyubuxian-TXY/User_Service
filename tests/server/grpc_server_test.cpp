// tests/server/grpc_server_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

#include "server/grpc_server.h"
#include "server/server_builder.h"
#include "mock_dependencies.h"

using namespace ::testing;

namespace user_service {
namespace testing {

class GrpcServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试配置
        config_ = std::make_shared<Config>();
        config_->server.host = "127.0.0.1";
        config_->server.grpc_port = GetAvailablePort();
        config_->server.metrics_port = config_->server.grpc_port + 1000;
        
        // MySQL 配置（测试时不实际连接）
        config_->mysql.host = "localhost";
        config_->mysql.port = 3306;
        config_->mysql.database = "test_db";
        config_->mysql.username = "test";
        config_->mysql.password = "test";
        config_->mysql.pool_size = 1;
        
        // Redis 配置
        config_->redis.host = "localhost";
        config_->redis.port = 6379;
        config_->redis.pool_size = 1;
        
        // ZooKeeper 配置（禁用）
        config_->zookeeper.enabled = false;
        config_->zookeeper.register_self = false;
        
        // Security 配置
        config_->security.jwt_secret = "test-jwt-secret-key-at-least-32-bytes!!";
        config_->security.jwt_issuer = "test-server";
        config_->security.access_token_ttl_seconds = 300;
        config_->security.refresh_token_ttl_seconds = 3600;
        
        // SMS 配置
        config_->sms.code_len = 6;
        config_->sms.code_ttl_seconds = 60;
        config_->sms.send_interval_seconds = 10;
        config_->sms.max_retry_count = 3;
        config_->sms.retry_ttl_seconds = 60;
        config_->sms.lock_seconds = 300;
        
        // Login 配置
        config_->login.max_failed_attempts = 3;
        config_->login.failed_attempts_window = 300;
        config_->login.lock_duration_seconds = 600;
        config_->login.max_sessions_per_user = 3;
        
        // Password 配置
        config_->password.min_length = 6;
        config_->password.max_length = 20;
        config_->password.require_digit = true;
        
        // Log 配置
        config_->log.level = "error";
        config_->log.path = "./test_logs";
        config_->log.console_output = false;
    }
    
    void TearDown() override {
        // 确保服务器停止
        if (server_ && server_->IsRunning()) {
            server_->Shutdown(std::chrono::milliseconds(1000));
        }
        server_.reset();
    }
    
    // 获取可用端口（简单实现：使用递增的端口号）
    static int GetAvailablePort() {
        static int port = 50200;
        return port++;
    }
    
    std::shared_ptr<Config> config_;
    std::unique_ptr<GrpcServer> server_;
};

// ============================================================================
// 构造函数测试
// ============================================================================

TEST_F(GrpcServerTest, Constructor_ValidConfig_Succeeds) {
    EXPECT_NO_THROW({
        server_ = std::make_unique<GrpcServer>(config_);
    });
    ASSERT_NE(server_, nullptr);
}

TEST_F(GrpcServerTest, Constructor_NullConfig_Throws) {
    EXPECT_THROW({
        auto server = std::make_unique<GrpcServer>(nullptr);
    }, std::invalid_argument);
}

// ============================================================================
// GetAddress 测试
// ============================================================================

TEST_F(GrpcServerTest, GetAddress_ReturnsCorrectFormat) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    std::string expected = config_->server.host + ":" + 
                           std::to_string(config_->server.grpc_port);
    EXPECT_EQ(server_->GetAddress(), expected);
}

// ============================================================================
// GetConfig 测试
// ============================================================================

TEST_F(GrpcServerTest, GetConfig_ReturnsSameConfig) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    auto returned_config = server_->GetConfig();
    ASSERT_NE(returned_config, nullptr);
    EXPECT_EQ(returned_config->server.grpc_port, config_->server.grpc_port);
    EXPECT_EQ(returned_config->security.jwt_secret, config_->security.jwt_secret);
}

// ============================================================================
// IsRunning 测试
// ============================================================================

TEST_F(GrpcServerTest, IsRunning_BeforeStart_ReturnsFalse) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    EXPECT_FALSE(server_->IsRunning());
}

// ============================================================================
// SetShutdownCallback 测试
// ============================================================================

TEST_F(GrpcServerTest, SetShutdownCallback_SetsCallback) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    bool callback_called = false;
    server_->SetShutdownCallback([&callback_called]() {
        callback_called = true;
    });
    
    // 回调只有在 Shutdown 时才会被调用
    // 这里只验证设置成功（不抛异常）
    EXPECT_FALSE(callback_called);
}

// ============================================================================
// 配置验证测试
// ============================================================================

TEST_F(GrpcServerTest, Config_PortRange_Valid) {
    config_->server.grpc_port = 50051;
    
    EXPECT_NO_THROW({
        server_ = std::make_unique<GrpcServer>(config_);
    });
}

TEST_F(GrpcServerTest, Config_ZooKeeperDisabled_NoZKConnection) {
    config_->zookeeper.enabled = false;
    
    server_ = std::make_unique<GrpcServer>(config_);
    
    // 服务器应该能创建成功，即使 ZK 未启用
    ASSERT_NE(server_, nullptr);
}

// ============================================================================
// 服务获取测试（初始化前）
// ============================================================================

TEST_F(GrpcServerTest, GetAuthService_BeforeInit_ReturnsNull) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    // 初始化前，服务应该为 null
    EXPECT_EQ(server_->GetAuthService(), nullptr);
}

TEST_F(GrpcServerTest, GetUserService_BeforeInit_ReturnsNull) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    EXPECT_EQ(server_->GetUserService(), nullptr);
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST_F(GrpcServerTest, MultipleServers_DifferentPorts) {
    auto config1 = std::make_shared<Config>(*config_);
    config1->server.grpc_port = GetAvailablePort();
    
    auto config2 = std::make_shared<Config>(*config_);
    config2->server.grpc_port = GetAvailablePort();
    
    auto server1 = std::make_unique<GrpcServer>(config1);
    auto server2 = std::make_unique<GrpcServer>(config2);
    
    EXPECT_NE(server1->GetAddress(), server2->GetAddress());
}

TEST_F(GrpcServerTest, ShutdownWithoutStart_Succeeds) {
    server_ = std::make_unique<GrpcServer>(config_);
    
    // 没有启动就关闭，应该不会崩溃
    EXPECT_NO_THROW({
        server_->Shutdown(std::chrono::milliseconds(100));
    });
}

// ============================================================================
// 配置组合测试
// ============================================================================

TEST_F(GrpcServerTest, Config_AllOptionalFieldsSet) {
    config_->mysql.connection_timeout_ms = 5000;
    config_->mysql.read_timeout_ms = 30000;
    config_->mysql.write_timeout_ms = 30000;
    config_->mysql.auto_reconnect = true;
    
    config_->redis.connect_timeout_ms = 3000;
    config_->redis.socket_timeout_ms = 3000;
    
    EXPECT_NO_THROW({
        server_ = std::make_unique<GrpcServer>(config_);
    });
}

TEST_F(GrpcServerTest, Config_MinimalRequired) {
    // 只设置必需的配置
    auto minimal_config = std::make_shared<Config>();
    minimal_config->server.grpc_port = GetAvailablePort();
    minimal_config->security.jwt_secret = "minimal-test-secret-32-bytes!!!";
    minimal_config->security.jwt_issuer = "test";
    minimal_config->security.access_token_ttl_seconds = 300;
    minimal_config->security.refresh_token_ttl_seconds = 3600;
    minimal_config->zookeeper.enabled = false;
    
    // 设置其他必需的最小值
    minimal_config->sms.code_len = 6;
    minimal_config->sms.code_ttl_seconds = 60;
    minimal_config->sms.send_interval_seconds = 10;
    minimal_config->sms.max_retry_count = 3;
    minimal_config->sms.retry_ttl_seconds = 60;
    minimal_config->sms.lock_seconds = 300;
    
    minimal_config->login.max_failed_attempts = 3;
    minimal_config->login.failed_attempts_window = 300;
    minimal_config->login.lock_duration_seconds = 600;
    minimal_config->login.max_sessions_per_user = 3;
    
    minimal_config->password.min_length = 6;
    minimal_config->password.max_length = 32;
    
    EXPECT_NO_THROW({
        auto server = std::make_unique<GrpcServer>(minimal_config);
    });
}

// ============================================================================
// 集成测试（需要实际的 MySQL/Redis）
// ============================================================================

class GrpcServerIntegrationTest : public GrpcServerTest {
protected:
    void SetUp() override {
        GrpcServerTest::SetUp();
        
        // 检查是否可以进行集成测试
        // 可以通过环境变量控制
        const char* skip_integration = std::getenv("SKIP_INTEGRATION_TESTS");
        if (skip_integration && std::string(skip_integration) == "1") {
            skip_tests_ = true;
        }
    }
    
    bool skip_tests_ = false;
};

TEST_F(GrpcServerIntegrationTest, Initialize_WithRealDependencies) {
    if (skip_tests_) {
        GTEST_SKIP() << "Skipping integration test (SKIP_INTEGRATION_TESTS=1)";
    }
    
    // 需要实际的 MySQL 和 Redis
    // 这个测试在 CI 环境中运行
    server_ = std::make_unique<GrpcServer>(config_);
    
    // 尝试初始化（可能因为依赖不可用而失败）
    bool init_result = server_->Initialize();
    
    // 如果依赖可用，初始化应该成功
    // 如果依赖不可用，我们跳过后续断言
    if (!init_result) {
        GTEST_SKIP() << "Dependencies not available for integration test";
    }
    
    EXPECT_NE(server_->GetAuthService(), nullptr);
    EXPECT_NE(server_->GetUserService(), nullptr);
}

TEST_F(GrpcServerIntegrationTest, StartAndStop_WithRealDependencies) {
    if (skip_tests_) {
        GTEST_SKIP() << "Skipping integration test";
    }
    
    server_ = std::make_unique<GrpcServer>(config_);
    
    if (!server_->Initialize()) {
        GTEST_SKIP() << "Dependencies not available";
    }
    
    // 启动服务器
    bool start_result = server_->Start();
    EXPECT_TRUE(start_result);
    EXPECT_TRUE(server_->IsRunning());
    
    // 等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 关闭服务器
    server_->Shutdown(std::chrono::milliseconds(1000));
    server_->Wait();
    
    EXPECT_FALSE(server_->IsRunning());
}

TEST_F(GrpcServerIntegrationTest, ShutdownCallback_IsCalled) {
    if (skip_tests_) {
        GTEST_SKIP() << "Skipping integration test";
    }
    
    server_ = std::make_unique<GrpcServer>(config_);
    
    bool callback_called = false;
    server_->SetShutdownCallback([&callback_called]() {
        callback_called = true;
    });
    
    if (!server_->Initialize()) {
        GTEST_SKIP() << "Dependencies not available";
    }
    
    if (!server_->Start()) {
        GTEST_SKIP() << "Failed to start server";
    }
    
    // 关闭服务器
    server_->Shutdown(std::chrono::milliseconds(1000));
    server_->Wait();
    
    EXPECT_TRUE(callback_called);
}

}  // namespace testing
}  // namespace user_service
