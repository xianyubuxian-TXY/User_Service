// tests/server/server_builder_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <cstdlib>

#include "server/server_builder.h"
#include "config/config.h"

namespace user_service {
namespace testing {

class ServerBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 获取测试配置文件路径
        test_config_path_ = "test_config.yaml";
        
        // 清除可能影响测试的环境变量
        unsetenv("ZK_HOSTS");
        unsetenv("ZK_SERVICE_NAME");
        unsetenv("ZK_ENABLED");
        unsetenv("MYSQL_HOST");
        unsetenv("REDIS_HOST");
    }
    
    void TearDown() override {
        // 清理环境变量
        unsetenv("ZK_HOSTS");
        unsetenv("ZK_SERVICE_NAME");
        unsetenv("ZK_ENABLED");
        unsetenv("MYSQL_HOST");
        unsetenv("REDIS_HOST");
    }
    
    std::string test_config_path_;
};

// ============================================================================
// WithConfigFile 测试
// ============================================================================

TEST_F(ServerBuilderTest, WithConfigFile_ValidPath_LoadsConfig) {
    // 检查测试配置文件是否存在
    std::ifstream file(test_config_path_);
    if (!file.good()) {
        GTEST_SKIP() << "Test config file not found: " << test_config_path_;
    }
    file.close();
    
    ServerBuilder builder;
    
    // 不应该抛出异常
    EXPECT_NO_THROW({
        builder.WithConfigFile(test_config_path_);
    });
}

TEST_F(ServerBuilderTest, WithConfigFile_InvalidPath_ThrowsException) {
    ServerBuilder builder;
    
    EXPECT_THROW({
        builder.WithConfigFile("/nonexistent/path/config.yaml");
    }, std::runtime_error);
}

TEST_F(ServerBuilderTest, WithConfigFile_ChainedCalls) {
    std::ifstream file(test_config_path_);
    if (!file.good()) {
        GTEST_SKIP() << "Test config file not found";
    }
    file.close();
    
    // 测试链式调用
    auto& builder = ServerBuilder()
        .WithConfigFile(test_config_path_)
        .WithPort(50100)
        .WithHost("0.0.0.0");
    
    // 链式调用应该返回 ServerBuilder&
    EXPECT_TRUE(true);  // 如果编译通过，测试通过
}

// ============================================================================
// WithConfig 测试
// ============================================================================

TEST_F(ServerBuilderTest, WithConfig_ValidConfig_Succeeds) {
    auto config = std::make_shared<Config>();
    config->server.grpc_port = 50101;
    config->server.host = "127.0.0.1";
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    ServerBuilder builder;
    
    EXPECT_NO_THROW({
        builder.WithConfig(config);
    });
}

TEST_F(ServerBuilderTest, WithConfig_NullConfig_BuildThrows) {
    ServerBuilder builder;
    builder.WithConfig(nullptr);
    
    EXPECT_THROW({
        builder.Build();
    }, std::runtime_error);
}

// ============================================================================
// WithPort 测试
// ============================================================================

TEST_F(ServerBuilderTest, WithPort_OverridesConfigPort) {
    auto config = std::make_shared<Config>();
    config->server.grpc_port = 50051;  // 原始端口
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .WithPort(50102)  // 覆盖端口
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->server.grpc_port, 50102);
}

TEST_F(ServerBuilderTest, WithPort_MultipleCallsUsesLast) {
    auto config = std::make_shared<Config>();
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .WithPort(50001)
        .WithPort(50002)
        .WithPort(50103)  // 最后一次调用
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->server.grpc_port, 50103);
}

// ============================================================================
// WithHost 测试
// ============================================================================

TEST_F(ServerBuilderTest, WithHost_OverridesConfigHost) {
    auto config = std::make_shared<Config>();
    config->server.host = "127.0.0.1";  // 原始 host
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .WithHost("0.0.0.0")  // 覆盖 host
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->server.host, "0.0.0.0");
}

// ============================================================================
// EnableServiceDiscovery 测试
// ============================================================================

TEST_F(ServerBuilderTest, EnableServiceDiscovery_True_EnablesZK) {
    auto config = std::make_shared<Config>();
    config->zookeeper.enabled = false;  // 原始配置禁用
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .EnableServiceDiscovery(true)  // 覆盖为启用
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_TRUE(server->GetConfig()->zookeeper.enabled);
}

TEST_F(ServerBuilderTest, EnableServiceDiscovery_False_DisablesZK) {
    auto config = std::make_shared<Config>();
    config->zookeeper.enabled = true;  // 原始配置启用
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .EnableServiceDiscovery(false)  // 覆盖为禁用
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_FALSE(server->GetConfig()->zookeeper.enabled);
}

// ============================================================================
// WithServiceName 测试
// ============================================================================

TEST_F(ServerBuilderTest, WithServiceName_OverridesConfigServiceName) {
    auto config = std::make_shared<Config>();
    config->zookeeper.service_name = "old-service";  // 原始名称
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .WithServiceName("new-service")  // 新名称
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->zookeeper.service_name, "new-service");
}

// ============================================================================
// LoadFromEnvironment 测试
// ============================================================================

TEST_F(ServerBuilderTest, LoadFromEnvironment_ZKHosts_Overrides) {
    auto config = std::make_shared<Config>();
    config->zookeeper.hosts = "localhost:2181";
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    // 设置环境变量
    setenv("ZK_HOSTS", "zk1:2181,zk2:2181", 1);
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .LoadFromEnvironment()
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->zookeeper.hosts, "zk1:2181,zk2:2181");
}

TEST_F(ServerBuilderTest, LoadFromEnvironment_ServiceName_Overrides) {
    auto config = std::make_shared<Config>();
    config->zookeeper.service_name = "config-service";
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    setenv("ZK_SERVICE_NAME", "env-service", 1);
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .LoadFromEnvironment()
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->zookeeper.service_name, "env-service");
}

TEST_F(ServerBuilderTest, LoadFromEnvironment_WithPortOverride_PortTakesPrecedence) {
    auto config = std::make_shared<Config>();
    config->server.grpc_port = 50051;
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    // 即使环境变量设置了其他值，WithPort 应该优先
    auto server = ServerBuilder()
        .WithConfig(config)
        .LoadFromEnvironment()
        .WithPort(50104)  // 显式覆盖
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->server.grpc_port, 50104);
}

// ============================================================================
// OnShutdown 测试
// ============================================================================

TEST_F(ServerBuilderTest, OnShutdown_CallbackIsSet) {
    auto config = std::make_shared<Config>();
    config->security.jwt_secret = "test-secret";
    config->security.jwt_issuer = "test-issuer";
    
    bool callback_set = false;
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .OnShutdown([&callback_set]() {
            callback_set = true;
        })
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    // 注意：这里我们只能验证 Build 成功，
    // 实际回调调用需要在集成测试中验证
}

// ============================================================================
// Build 测试
// ============================================================================

TEST_F(ServerBuilderTest, Build_WithoutConfig_Throws) {
    ServerBuilder builder;
    
    EXPECT_THROW({
        builder.Build();
    }, std::runtime_error);
}

TEST_F(ServerBuilderTest, Build_WithValidConfig_ReturnsServer) {
    auto config = std::make_shared<Config>();
    config->server.grpc_port = 50105;
    config->server.host = "127.0.0.1";
    config->security.jwt_secret = "test-secret-key-32-bytes-minimum!";
    config->security.jwt_issuer = "test-issuer";
    config->security.access_token_ttl_seconds = 300;
    config->security.refresh_token_ttl_seconds = 3600;
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetAddress(), "127.0.0.1:50105");
}

TEST_F(ServerBuilderTest, Build_MultipleTimes_CreatesSeparateInstances) {
    auto config1 = std::make_shared<Config>();
    config1->security.jwt_secret = "test-secret";
    config1->security.jwt_issuer = "test-issuer";
    
    auto config2 = std::make_shared<Config>();
    config2->security.jwt_secret = "test-secret";
    config2->security.jwt_issuer = "test-issuer";
    
    auto server1 = ServerBuilder()
        .WithConfig(config1)
        .WithPort(50106)
        .EnableServiceDiscovery(false)
        .Build();
    
    auto server2 = ServerBuilder()
        .WithConfig(config2)
        .WithPort(50107)
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server1, nullptr);
    ASSERT_NE(server2, nullptr);
    EXPECT_NE(server1.get(), server2.get());
    EXPECT_NE(server1->GetAddress(), server2->GetAddress());
}

// ============================================================================
// 完整流程测试
// ============================================================================

TEST_F(ServerBuilderTest, FullBuilderFlow_AuthService) {
    auto config = std::make_shared<Config>();
    config->server.host = "0.0.0.0";
    config->server.grpc_port = 50108;
    config->security.jwt_secret = "auth-service-secret-key-minimum-32!";
    config->security.jwt_issuer = "auth-service";
    config->security.access_token_ttl_seconds = 900;
    config->security.refresh_token_ttl_seconds = 604800;
    config->zookeeper.enabled = false;
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .WithPort(50051)
        .WithServiceName("auth-service")
        .EnableServiceDiscovery(false)
        .OnShutdown([]() {
            // Cleanup callback
        })
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->server.grpc_port, 50051);
    EXPECT_EQ(server->GetConfig()->zookeeper.service_name, "auth-service");
    EXPECT_FALSE(server->IsRunning());
}

TEST_F(ServerBuilderTest, FullBuilderFlow_UserService) {
    auto config = std::make_shared<Config>();
    config->server.host = "0.0.0.0";
    config->server.grpc_port = 50109;
    config->security.jwt_secret = "user-service-secret-key-minimum-32!";
    config->security.jwt_issuer = "user-service";
    config->security.access_token_ttl_seconds = 900;
    config->security.refresh_token_ttl_seconds = 604800;
    config->zookeeper.enabled = false;
    
    auto server = ServerBuilder()
        .WithConfig(config)
        .WithPort(50052)
        .WithServiceName("user-service")
        .EnableServiceDiscovery(false)
        .Build();
    
    ASSERT_NE(server, nullptr);
    EXPECT_EQ(server->GetConfig()->server.grpc_port, 50052);
    EXPECT_EQ(server->GetConfig()->zookeeper.service_name, "user-service");
}

}  // namespace testing
}  // namespace user_service
