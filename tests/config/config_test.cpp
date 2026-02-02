// tests/config_test.cpp
#include <gtest/gtest.h>
#include "config/config.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时测试目录
        test_dir = fs::temp_directory_path() / "config_test";
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        fs::create_directory(test_dir);
        
        // 保存原始环境变量
        SaveEnvironmentVariables();
    }

    void TearDown() override {
        // 清理测试目录
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        
        // 恢复环境变量
        RestoreEnvironmentVariables();
    }

    // 创建测试用的 YAML 配置文件
    void CreateTestConfigFile(const std::string& filename, const std::string& content) {
        std::ofstream file(test_dir / filename);
        file << content;
        file.close();
    }

    // 创建完整的有效配置文件
    std::string GetValidConfigYaml() {
        return R"(
server:
  host: "127.0.0.1"
  grpc_port: 50051
  metrics_port: 9090

mysql:
  host: "localhost"
  port: 3306
  database: "test_db"
  username: "test_user"
  password: "test_password"
  pool_size: 10
  connection_timeout_ms: 5000
  read_timeout_ms: 3000
  write_timeout_ms: 3000
  max_retries: 3
  retry_interval_ms: 1000
  auto_reconnect: true
  charset: "utf8mb4"

redis:
  host: "localhost"
  port: 6379
  password: "redis_password"
  db: 0
  pool_size: 5
  connect_timeout_ms: 2000
  socket_timeout_ms: 2000
  wait_timeout_ms: 100

kafka:
  brokers: "localhost:9092"
  user_events_topic: "user-events"
  client_id: "user-service-test"

zookeeper:
  hosts: "localhost:2181"
  session_timeout_ms: 30000
  service_path: "/services/user-service"

security:
  jwt_secret: "test-secret-key-12345"
  token_ttl: 900
  refresh_token_ttl: 604800

log:
  level: "debug"
  path: "./test_logs"
  filename: "test.log"
  max_size: 10485760
  max_files: 5
  console_output: true
)";
    }

    // 保存环境变量
    void SaveEnvironmentVariables() {
        const char* vars[] = {
            "MYSQL_HOST", "MYSQL_PASSWORD", "REDIS_HOST", 
            "KAFKA_BROKERS", "ZK_HOSTS", "JWT_SECRET"
        };
        
        for (const char* var : vars) {
            if (const char* val = std::getenv(var)) {
                saved_env_vars[var] = val;
            }
        }
    }

    // 恢复环境变量
    void RestoreEnvironmentVariables() {
        const char* vars[] = {
            "MYSQL_HOST", "MYSQL_PASSWORD", "REDIS_HOST", 
            "KAFKA_BROKERS", "ZK_HOSTS", "JWT_SECRET"
        };
        
        for (const char* var : vars) {
            if (saved_env_vars.count(var)) {
                setenv(var, saved_env_vars[var].c_str(), 1);
            } else {
                unsetenv(var);
            }
        }
    }

    fs::path test_dir;
    std::map<std::string, std::string> saved_env_vars;
};

// ==================== 基础功能测试 ====================

// 测试加载有效的配置文件
TEST_F(ConfigTest, LoadValidConfigFile) {
    std::string config_file = "valid_config.yaml";
    CreateTestConfigFile(config_file, GetValidConfigYaml());
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    // 验证 Server 配置
    EXPECT_EQ(config.server.host, "127.0.0.1");
    EXPECT_EQ(config.server.grpc_port, 50051);
    EXPECT_EQ(config.server.metrics_port, 9090);
    
    // 验证 MySQL 配置
    EXPECT_EQ(config.mysql.host, "localhost");
    EXPECT_EQ(config.mysql.port, 3306);
    EXPECT_EQ(config.mysql.database, "test_db");
    EXPECT_EQ(config.mysql.username, "test_user");
    EXPECT_EQ(config.mysql.password, "test_password");
    EXPECT_EQ(config.mysql.pool_size, 10);
    EXPECT_TRUE(config.mysql.connection_timeout_ms.has_value());
    EXPECT_EQ(config.mysql.connection_timeout_ms.value(), 5000);
    EXPECT_EQ(config.mysql.max_retries, 3);
    EXPECT_EQ(config.mysql.charset, "utf8mb4");
    
    // 验证 Redis 配置
    EXPECT_EQ(config.redis.host, "localhost");
    EXPECT_EQ(config.redis.port, 6379);
    EXPECT_EQ(config.redis.password, "redis_password");
    EXPECT_EQ(config.redis.db, 0);
    EXPECT_EQ(config.redis.pool_size, 5);
    
    // 验证 Kafka 配置
    EXPECT_EQ(config.kafka.brokers, "localhost:9092");
    EXPECT_EQ(config.kafka.user_events_topic, "user-events");
    
    // 验证 Security 配置
    EXPECT_EQ(config.security.jwt_secret, "test-secret-key-12345");
    EXPECT_EQ(config.security.token_ttl.count(), 900);
    
    // 验证 Log 配置
    EXPECT_EQ(config.log.level, "debug");
    EXPECT_EQ(config.log.max_files, 5);
    EXPECT_TRUE(config.log.console_output);
}

// 测试加载默认配置（空文件）
TEST_F(ConfigTest, LoadDefaultConfig) {
    std::string config_file = "empty_config.yaml";
    CreateTestConfigFile(config_file, "# Empty config file\n");
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    // 验证默认值
    EXPECT_EQ(config.server.host, "0.0.0.0");
    EXPECT_EQ(config.server.grpc_port, 50051);
    EXPECT_EQ(config.mysql.pool_size, 10);
    EXPECT_EQ(config.redis.pool_size, 5);
}

// 测试加载部分配置
TEST_F(ConfigTest, LoadPartialConfig) {
    std::string partial_yaml = R"(
server:
  grpc_port: 8080

mysql:
  host: "custom-host"
  database: "custom_db"
)";
    
    std::string config_file = "partial_config.yaml";
    CreateTestConfigFile(config_file, partial_yaml);
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    // 验证指定的值
    EXPECT_EQ(config.server.grpc_port, 8080);
    EXPECT_EQ(config.mysql.host, "custom-host");
    EXPECT_EQ(config.mysql.database, "custom_db");
    
    // 验证未指定的值使用默认值
    EXPECT_EQ(config.server.host, "0.0.0.0");
    EXPECT_EQ(config.mysql.port, 3306);
}

// ==================== 配置验证测试 ====================

// 测试无效的端口号
TEST_F(ConfigTest, InvalidPort_TooLarge) {
    std::string invalid_yaml = R"(
server:
  grpc_port: 99999
)";
    
    std::string config_file = "invalid_port.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

TEST_F(ConfigTest, InvalidPort_Negative) {
    std::string invalid_yaml = R"(
mysql:
  port: -1
)";
    
    std::string config_file = "negative_port.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

// 测试无效的连接池大小
TEST_F(ConfigTest, InvalidPoolSize_Zero) {
    std::string invalid_yaml = R"(
mysql:
  pool_size: 0
)";
    
    std::string config_file = "zero_pool.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

TEST_F(ConfigTest, InvalidPoolSize_Negative) {
    std::string invalid_yaml = R"(
redis:
  pool_size: -5
)";
    
    std::string config_file = "negative_pool.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

// 测试空的必需字段
TEST_F(ConfigTest, EmptyMySQLHost) {
    std::string invalid_yaml = R"(
mysql:
  host: ""
  database: "test_db"
)";
    
    std::string config_file = "empty_host.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

TEST_F(ConfigTest, EmptyDatabase) {
    std::string invalid_yaml = R"(
mysql:
  host: "localhost"
  database: ""
)";
    
    std::string config_file = "empty_database.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

TEST_F(ConfigTest, EmptyJWTSecret) {
    std::string invalid_yaml = R"(
security:
  jwt_secret: ""
)";
    
    std::string config_file = "empty_jwt.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}

// ==================== 文件错误测试 ====================

// 测试文件不存在
TEST_F(ConfigTest, FileNotExists) {
    EXPECT_THROW({
        user::Config::LoadFromFile((test_dir / "non_existent.yaml").string());
    }, std::runtime_error);
}

// 测试无效的 YAML 格式
TEST_F(ConfigTest, InvalidYamlFormat) {
    // 使用无法解析的 YAML 语法
    std::string invalid_yaml = "{ this is: [definitely, not: valid] yaml";
    
    std::string config_file = "invalid_yaml.yaml";
    CreateTestConfigFile(config_file, invalid_yaml);
    
    EXPECT_THROW({
        user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    }, std::runtime_error);
}


// ==================== 环境变量测试 ====================

// 测试环境变量覆盖
TEST_F(ConfigTest, EnvironmentVariableOverride) {
    std::string config_file = "env_test.yaml";
    CreateTestConfigFile(config_file, GetValidConfigYaml());
    
    // 设置环境变量
    setenv("MYSQL_HOST", "env-mysql-host", 1);
    setenv("MYSQL_PASSWORD", "env-password", 1);
    setenv("REDIS_HOST", "env-redis-host", 1);
    setenv("KAFKA_BROKERS", "env-kafka:9092", 1);
    setenv("ZK_HOSTS", "env-zk:2181", 1);
    setenv("JWT_SECRET", "env-jwt-secret", 1);
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    config.LoadFromEnv();
    
    // 验证环境变量覆盖
    EXPECT_EQ(config.mysql.host, "env-mysql-host");
    EXPECT_EQ(config.mysql.password, "env-password");
    EXPECT_EQ(config.redis.host, "env-redis-host");
    EXPECT_EQ(config.kafka.brokers, "env-kafka:9092");
    EXPECT_EQ(config.zookeeper.hosts, "env-zk:2181");
    EXPECT_EQ(config.security.jwt_secret, "env-jwt-secret");
    
    // 验证未设置环境变量的字段保持原值
    EXPECT_EQ(config.mysql.port, 3306);
}

// 测试环境变量不存在时不覆盖
TEST_F(ConfigTest, EnvironmentVariableNotSet) {
    std::string config_file = "env_not_set.yaml";
    CreateTestConfigFile(config_file, GetValidConfigYaml());
    
    // 确保环境变量未设置
    unsetenv("MYSQL_HOST");
    unsetenv("REDIS_HOST");
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    std::string original_mysql_host = config.mysql.host;
    
    config.LoadFromEnv();
    
    // 验证配置未被改变
    EXPECT_EQ(config.mysql.host, original_mysql_host);
}

// ==================== ToString 测试 ====================

// 测试 ServerConfig ToString
TEST_F(ConfigTest, ServerConfigToString) {
    user::ServerConfig server;
    server.host = "test-host";
    server.grpc_port = 8080;
    server.metrics_port = 9090;
    
    std::string str = server.ToString();
    
    EXPECT_NE(str.find("test-host"), std::string::npos);
    EXPECT_NE(str.find("8080"), std::string::npos);
    EXPECT_NE(str.find("9090"), std::string::npos);
}

// 测试 MySQLConfig ToString
TEST_F(ConfigTest, MySQLConfigToString) {
    user::MySQLConfig mysql;
    mysql.host = "mysql-host";
    mysql.password = "secret123";
    mysql.connection_timeout_ms = 5000;
    
    std::string str = mysql.ToString();
    
    EXPECT_NE(str.find("mysql-host"), std::string::npos);
    EXPECT_EQ(str.find("secret123"), std::string::npos); // 密码应该被隐藏
    EXPECT_NE(str.find("******"), std::string::npos);
    EXPECT_NE(str.find("5000"), std::string::npos);
}

// 测试完整 Config ToString
TEST_F(ConfigTest, ConfigToString) {
    std::string config_file = "to_string_test.yaml";
    CreateTestConfigFile(config_file, GetValidConfigYaml());
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    std::string str = config.ToString();
    
    // 验证包含各个部分
    EXPECT_NE(str.find("Server Config"), std::string::npos);
    EXPECT_NE(str.find("MySQL Config"), std::string::npos);
    EXPECT_NE(str.find("Redis Config"), std::string::npos);
    EXPECT_NE(str.find("Kafka Config"), std::string::npos);
    EXPECT_NE(str.find("Security Config"), std::string::npos);
    EXPECT_NE(str.find("Log Config"), std::string::npos);
}

// ==================== Optional 字段测试 ====================

// 测试 optional 字段未设置
TEST_F(ConfigTest, OptionalFieldsNotSet) {
    std::string minimal_yaml = R"(
mysql:
  host: "localhost"
  database: "test_db"
)";
    
    std::string config_file = "optional_not_set.yaml";
    CreateTestConfigFile(config_file, minimal_yaml);
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    // 验证 optional 字段未设置
    EXPECT_FALSE(config.mysql.connection_timeout_ms.has_value());
    EXPECT_FALSE(config.mysql.read_timeout_ms.has_value());
    EXPECT_FALSE(config.mysql.auto_reconnect.has_value());
    EXPECT_FALSE(config.redis.connect_timeout_ms.has_value());
}

// 测试 optional 字段已设置
TEST_F(ConfigTest, OptionalFieldsSet) {
    std::string config_file = "optional_set.yaml";
    CreateTestConfigFile(config_file, GetValidConfigYaml());
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    // 验证 optional 字段已设置
    EXPECT_TRUE(config.mysql.connection_timeout_ms.has_value());
    EXPECT_EQ(config.mysql.connection_timeout_ms.value(), 5000);
    EXPECT_TRUE(config.mysql.auto_reconnect.has_value());
    EXPECT_TRUE(config.mysql.auto_reconnect.value());
}

// ==================== 边界值测试 ====================

// 测试最大有效端口号
TEST_F(ConfigTest, MaxValidPort) {
    std::string yaml = R"(
server:
  grpc_port: 65535
)";
    
    std::string config_file = "max_port.yaml";
    CreateTestConfigFile(config_file, yaml);
    
    EXPECT_NO_THROW({
        user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
        EXPECT_EQ(config.server.grpc_port, 65535);
    });
}

// 测试最小有效端口号
TEST_F(ConfigTest, MinValidPort) {
    std::string yaml = R"(
server:
  grpc_port: 1
)";
    
    std::string config_file = "min_port.yaml";
    CreateTestConfigFile(config_file, yaml);
    
    EXPECT_NO_THROW({
        user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
        EXPECT_EQ(config.server.grpc_port, 1);
    });
}

// 测试极大的日志文件大小
TEST_F(ConfigTest, LargeLogFileSize) {
    std::string yaml = R"(
log:
  max_size: 1073741824
)";
    
    std::string config_file = "large_log.yaml";
    CreateTestConfigFile(config_file, yaml);
    
    EXPECT_NO_THROW({
        user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
        EXPECT_EQ(config.log.max_size, 1073741824); // 1GB
    });
}

// ==================== 特殊字符测试 ====================

// 测试密码包含特殊字符
TEST_F(ConfigTest, PasswordWithSpecialCharacters) {
    std::string yaml = R"delimiter(
mysql:
  password: "p@ssw0rd!#$%^&*()"
redis:
  password: "redis_p@ss!123"
)delimiter";
    
    std::string config_file = "special_chars.yaml";
    CreateTestConfigFile(config_file, yaml);
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    EXPECT_EQ(config.mysql.password, "p@ssw0rd!#$%^&*()");
    EXPECT_EQ(config.redis.password, "redis_p@ss!123");
}

// ==================== Getter 方法测试 ====================

// 测试 GetPoolSize 方法
TEST_F(ConfigTest, GetPoolSizeMethods) {
    std::string config_file = "pool_size_test.yaml";
    CreateTestConfigFile(config_file, GetValidConfigYaml());
    
    user::Config config = user::Config::LoadFromFile((test_dir / config_file).string());
    
    EXPECT_EQ(config.mysql.GetPoolSize(), 10);
    EXPECT_EQ(config.redis.GetPoolSize(), 5);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
