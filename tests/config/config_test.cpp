// tests/config/config_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <cstdlib>
#include <filesystem>

#include "config/config.h"

namespace user_service {
namespace testing {

// ============================================================================
// 测试夹具
// ============================================================================

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 清除可能影响测试的环境变量
        ClearEnvVars();
    }

    void TearDown() override {
        ClearEnvVars();
        // 清理临时文件
        if (std::filesystem::exists(temp_config_path_)) {
            std::filesystem::remove(temp_config_path_);
        }
    }

    // 清除环境变量
    void ClearEnvVars() {
        unsetenv("MYSQL_HOST");
        unsetenv("MYSQL_PASSWORD");
        unsetenv("REDIS_HOST");
        unsetenv("ZK_HOSTS");
        unsetenv("ZK_SERVICE_NAME");
        unsetenv("ZK_ENABLED");
        unsetenv("ZK_REGISTER_SELF");
        unsetenv("ZK_ROOT_PATH");
        unsetenv("ZK_WEIGHT");
        unsetenv("JWT_SECRET");
        unsetenv("JWT_ISSUER");
        unsetenv("ACCESS_TOKEN_TTL");
        unsetenv("REFRESH_TOKEN_TTL");
        unsetenv("SMS_CODE_LEN");
        unsetenv("LOGIN_MAX_FAILED_ATTEMPTS");
        unsetenv("PASSWORD_MIN_LENGTH");
    }

    // 创建临时配置文件
    void CreateTempConfig(const std::string& content) {
        std::ofstream ofs(temp_config_path_);
        ofs << content;
        ofs.close();
    }

    std::string temp_config_path_ = "temp_test_config.yaml";
};

// ============================================================================
// 默认值测试
// ============================================================================

class ConfigDefaultsTest : public ConfigTest {};

TEST_F(ConfigDefaultsTest, ServerConfigDefaults) {
    ServerConfig config;
    
    EXPECT_EQ(config.host, "0.0.0.0");
    EXPECT_EQ(config.grpc_port, 50051);
    EXPECT_EQ(config.metrics_port, 9090);
}

TEST_F(ConfigDefaultsTest, MySQLConfigDefaults) {
    MySQLConfig config;
    
    EXPECT_EQ(config.host, "localhost");
    EXPECT_EQ(config.port, 3306);
    EXPECT_EQ(config.database, "user_service");
    EXPECT_EQ(config.username, "root");
    EXPECT_EQ(config.password, "");
    EXPECT_EQ(config.pool_size, 10);
    EXPECT_EQ(config.max_retries, 3);
    EXPECT_EQ(config.retry_interval_ms, 1000);
    EXPECT_EQ(config.charset, "utf8mb4");
    EXPECT_FALSE(config.connection_timeout_ms.has_value());
    EXPECT_FALSE(config.auto_reconnect.has_value());
}

TEST_F(ConfigDefaultsTest, RedisConfigDefaults) {
    RedisConfig config;
    
    EXPECT_EQ(config.host, "localhost");
    EXPECT_EQ(config.port, 6379);
    EXPECT_EQ(config.password, "");
    EXPECT_EQ(config.db, 0);
    EXPECT_EQ(config.pool_size, 5);
    EXPECT_EQ(config.wait_timeout_ms, 100);
}

TEST_F(ConfigDefaultsTest, ZooKeeperConfigDefaults) {
    ZooKeeperConfig config;
    
    EXPECT_EQ(config.hosts, "127.0.0.1:2181");
    EXPECT_EQ(config.session_timeout_ms, 15000);
    EXPECT_EQ(config.connect_timeout_ms, 10000);
    EXPECT_EQ(config.root_path, "/services");
    EXPECT_EQ(config.service_name, "user-service");
    EXPECT_TRUE(config.enabled);
    EXPECT_TRUE(config.register_self);
    EXPECT_EQ(config.weight, 100);
    EXPECT_EQ(config.version, "1.0.0");
}

TEST_F(ConfigDefaultsTest, SecurityConfigDefaults) {
    SecurityConfig config;
    
    EXPECT_EQ(config.jwt_secret, "your-secret-key");
    EXPECT_EQ(config.jwt_issuer, "user-service");
    EXPECT_EQ(config.access_token_ttl_seconds, 900);
    EXPECT_EQ(config.refresh_token_ttl_seconds, 604800);
}

TEST_F(ConfigDefaultsTest, SmsConfigDefaults) {
    SmsConfig config;
    
    EXPECT_EQ(config.code_len, 6);
    EXPECT_EQ(config.code_ttl_seconds, 300);
    EXPECT_EQ(config.send_interval_seconds, 60);
    EXPECT_EQ(config.max_retry_count, 5);
    EXPECT_EQ(config.retry_ttl_seconds, 300);
    EXPECT_EQ(config.lock_seconds, 1800);
}

TEST_F(ConfigDefaultsTest, LoginConfigDefaults) {
    LoginConfig config;
    
    EXPECT_EQ(config.max_failed_attempts, 5);
    EXPECT_EQ(config.failed_attempts_window, 900);
    EXPECT_EQ(config.lock_duration_seconds, 1800);
    EXPECT_EQ(config.max_sessions_per_user, 5);
    EXPECT_TRUE(config.kick_oldest_session);
    EXPECT_TRUE(config.enable_password_login);
    EXPECT_TRUE(config.enable_sms_login);
    EXPECT_FALSE(config.require_captcha);
    EXPECT_EQ(config.captcha_after_failed_attempts, 3);
}

TEST_F(ConfigDefaultsTest, PasswordPolicyConfigDefaults) {
    PasswordPolicyConfig config;
    
    EXPECT_EQ(config.min_length, 8);
    EXPECT_EQ(config.max_length, 32);
    EXPECT_FALSE(config.require_uppercase);
    EXPECT_FALSE(config.require_lowercase);
    EXPECT_TRUE(config.require_digit);
    EXPECT_FALSE(config.require_special_char);
    EXPECT_EQ(config.expire_days, 0);
    EXPECT_EQ(config.history_count, 0);
}

TEST_F(ConfigDefaultsTest, LogConfigDefaults) {
    LogConfig config;
    
    EXPECT_EQ(config.level, "info");
    EXPECT_EQ(config.path, "./logs");
    EXPECT_EQ(config.filename, "user-service.log");
    EXPECT_EQ(config.max_size, 10 * 1024 * 1024);
    EXPECT_EQ(config.max_files, 5);
    EXPECT_TRUE(config.console_output);
}

// ============================================================================
// 从文件加载配置测试
// ============================================================================

class ConfigLoadFromFileTest : public ConfigTest {};

TEST_F(ConfigLoadFromFileTest, LoadValidConfig) {
    // 创建有效的测试配置
    std::string yaml_content = R"(
server:
  host: "192.168.1.100"
  grpc_port: 50052
  metrics_port: 9091

mysql:
  host: "mysql.example.com"
  port: 3307
  database: "my_database"
  username: "my_user"
  password: "my_password"
  pool_size: 20
  connection_timeout_ms: 5000
  auto_reconnect: true

redis:
  host: "redis.example.com"
  port: 6380
  password: "redis_pass"
  db: 1
  pool_size: 10

zookeeper:
  hosts: "zk1:2181,zk2:2181"
  session_timeout_ms: 20000
  connect_timeout_ms: 8000
  root_path: "/myservices"
  service_name: "my-service"
  enabled: true
  register_self: true
  weight: 150
  region: "us-west"
  zone: "zone-a"
  version: "2.0.0"

security:
  jwt_secret: "my-super-secret-key-that-is-long-enough"
  jwt_issuer: "my-issuer"
  access_token_ttl_seconds: 1800
  refresh_token_ttl_seconds: 1209600

sms:
  code_len: 4
  code_ttl_seconds: 600
  send_interval_seconds: 120
  max_retry_count: 3
  retry_ttl_seconds: 600
  lock_seconds: 3600

login:
  max_failed_attempts: 3
  failed_attempts_window: 600
  lock_duration_seconds: 3600
  max_sessions_per_user: 3
  kick_oldest_session: false
  enable_password_login: true
  enable_sms_login: false
  require_captcha: true
  captcha_after_failed_attempts: 2

password:
  min_length: 10
  max_length: 64
  require_uppercase: true
  require_lowercase: true
  require_digit: true
  require_special_char: true
  expire_days: 90
  history_count: 5

log:
  level: "warn"
  path: "/var/log/myapp"
  filename: "app.log"
  max_size: 52428800
  max_files: 10
  console_output: false
)";

    CreateTempConfig(yaml_content);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    
    // 验证 Server 配置
    EXPECT_EQ(config.server.host, "192.168.1.100");
    EXPECT_EQ(config.server.grpc_port, 50052);
    EXPECT_EQ(config.server.metrics_port, 9091);
    
    // 验证 MySQL 配置
    EXPECT_EQ(config.mysql.host, "mysql.example.com");
    EXPECT_EQ(config.mysql.port, 3307);
    EXPECT_EQ(config.mysql.database, "my_database");
    EXPECT_EQ(config.mysql.username, "my_user");
    EXPECT_EQ(config.mysql.password, "my_password");
    EXPECT_EQ(config.mysql.pool_size, 20);
    ASSERT_TRUE(config.mysql.connection_timeout_ms.has_value());
    EXPECT_EQ(config.mysql.connection_timeout_ms.value(), 5000);
    ASSERT_TRUE(config.mysql.auto_reconnect.has_value());
    EXPECT_TRUE(config.mysql.auto_reconnect.value());
    
    // 验证 Redis 配置
    EXPECT_EQ(config.redis.host, "redis.example.com");
    EXPECT_EQ(config.redis.port, 6380);
    EXPECT_EQ(config.redis.password, "redis_pass");
    EXPECT_EQ(config.redis.db, 1);
    EXPECT_EQ(config.redis.pool_size, 10);
    
    // 验证 ZooKeeper 配置
    EXPECT_EQ(config.zookeeper.hosts, "zk1:2181,zk2:2181");
    EXPECT_EQ(config.zookeeper.session_timeout_ms, 20000);
    EXPECT_EQ(config.zookeeper.connect_timeout_ms, 8000);
    EXPECT_EQ(config.zookeeper.root_path, "/myservices");
    EXPECT_EQ(config.zookeeper.service_name, "my-service");
    EXPECT_TRUE(config.zookeeper.enabled);
    EXPECT_TRUE(config.zookeeper.register_self);
    EXPECT_EQ(config.zookeeper.weight, 150);
    EXPECT_EQ(config.zookeeper.region, "us-west");
    EXPECT_EQ(config.zookeeper.zone, "zone-a");
    EXPECT_EQ(config.zookeeper.version, "2.0.0");
    
    // 验证 Security 配置
    EXPECT_EQ(config.security.jwt_secret, "my-super-secret-key-that-is-long-enough");
    EXPECT_EQ(config.security.jwt_issuer, "my-issuer");
    EXPECT_EQ(config.security.access_token_ttl_seconds, 1800);
    EXPECT_EQ(config.security.refresh_token_ttl_seconds, 1209600);
    
    // 验证 SMS 配置
    EXPECT_EQ(config.sms.code_len, 4);
    EXPECT_EQ(config.sms.code_ttl_seconds, 600);
    EXPECT_EQ(config.sms.send_interval_seconds, 120);
    EXPECT_EQ(config.sms.max_retry_count, 3);
    EXPECT_EQ(config.sms.retry_ttl_seconds, 600);
    EXPECT_EQ(config.sms.lock_seconds, 3600);
    
    // 验证 Login 配置
    EXPECT_EQ(config.login.max_failed_attempts, 3);
    EXPECT_EQ(config.login.failed_attempts_window, 600);
    EXPECT_EQ(config.login.lock_duration_seconds, 3600);
    EXPECT_EQ(config.login.max_sessions_per_user, 3);
    EXPECT_FALSE(config.login.kick_oldest_session);
    EXPECT_TRUE(config.login.enable_password_login);
    EXPECT_FALSE(config.login.enable_sms_login);
    EXPECT_TRUE(config.login.require_captcha);
    EXPECT_EQ(config.login.captcha_after_failed_attempts, 2);
    
    // 验证 Password 配置
    EXPECT_EQ(config.password.min_length, 10);
    EXPECT_EQ(config.password.max_length, 64);
    EXPECT_TRUE(config.password.require_uppercase);
    EXPECT_TRUE(config.password.require_lowercase);
    EXPECT_TRUE(config.password.require_digit);
    EXPECT_TRUE(config.password.require_special_char);
    EXPECT_EQ(config.password.expire_days, 90);
    EXPECT_EQ(config.password.history_count, 5);
    
    // 验证 Log 配置
    EXPECT_EQ(config.log.level, "warn");
    EXPECT_EQ(config.log.path, "/var/log/myapp");
    EXPECT_EQ(config.log.filename, "app.log");
    EXPECT_EQ(config.log.max_size, 52428800);
    EXPECT_EQ(config.log.max_files, 10);
    EXPECT_FALSE(config.log.console_output);
}

TEST_F(ConfigLoadFromFileTest, LoadNonExistentFile) {
    EXPECT_THROW(
        Config::LoadFromFile("non_existent_file.yaml"),
        std::runtime_error
    );
}

TEST_F(ConfigLoadFromFileTest, LoadMalformedYaml) {
    std::string malformed_yaml = R"(
server:
  host: "localhost"
  grpc_port: [invalid array here
    nested: broken
)";
    
    CreateTempConfig(malformed_yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigLoadFromFileTest, LoadPartialConfig) {
    // 只配置部分字段，其余使用默认值
    std::string partial_yaml = R"(
server:
  grpc_port: 50053

mysql:
  host: "db.example.com"
  database: "partial_db"

security:
  jwt_secret: "partial-secret-key-long-enough-32-bytes"
  jwt_issuer: "partial-issuer"
)";
    
    CreateTempConfig(partial_yaml);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    
    // 验证已配置的值
    EXPECT_EQ(config.server.grpc_port, 50053);
    EXPECT_EQ(config.mysql.host, "db.example.com");
    EXPECT_EQ(config.mysql.database, "partial_db");
    
    // 验证默认值
    EXPECT_EQ(config.server.host, "0.0.0.0");  // 默认值
    EXPECT_EQ(config.mysql.port, 3306);        // 默认值
    EXPECT_EQ(config.redis.host, "localhost"); // 默认值
}

// ============================================================================
// 配置验证测试
// ============================================================================

class ConfigValidationTest : public ConfigTest {};

TEST_F(ConfigValidationTest, InvalidGrpcPort) {
    std::string yaml = R"(
server:
  grpc_port: 0

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, InvalidMySQLPoolSize) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"
  pool_size: 0

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, EmptyMySQLHost) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: ""
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, EmptyJwtSecret) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: ""
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, RefreshTokenTTLSmallerThanAccessToken) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
  access_token_ttl_seconds: 3600
  refresh_token_ttl_seconds: 1800
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, InvalidSmsCodeLength) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

sms:
  code_len: 0
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, InvalidPasswordLengthRange) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

password:
  min_length: 20
  max_length: 10
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, ZooKeeperEnabledButEmptyHosts) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

zookeeper:
  enabled: true
  hosts: ""
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, ZooKeeperRootPathNotStartWithSlash) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

zookeeper:
  enabled: true
  hosts: "localhost:2181"
  root_path: "services"
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, ZooKeeperRegisterSelfWithEmptyServiceName) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

zookeeper:
  enabled: true
  hosts: "localhost:2181"
  root_path: "/services"
  register_self: true
  service_name: ""
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

TEST_F(ConfigValidationTest, CaptchaThresholdExceedsMaxFailedAttempts) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

login:
  max_failed_attempts: 3
  captcha_after_failed_attempts: 5
)";
    
    CreateTempConfig(yaml);
    
    EXPECT_THROW(
        Config::LoadFromFile(temp_config_path_),
        std::runtime_error
    );
}

// ============================================================================
// 环境变量覆盖测试
// ============================================================================

class ConfigEnvOverrideTest : public ConfigTest {};

TEST_F(ConfigEnvOverrideTest, OverrideMySQLHost) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    setenv("MYSQL_HOST", "env-mysql-host", 1);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    config.LoadFromEnv();
    
    EXPECT_EQ(config.mysql.host, "env-mysql-host");
}

TEST_F(ConfigEnvOverrideTest, OverrideRedisHost) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    setenv("REDIS_HOST", "env-redis-host", 1);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    config.LoadFromEnv();
    
    EXPECT_EQ(config.redis.host, "env-redis-host");
}

TEST_F(ConfigEnvOverrideTest, OverrideZooKeeperConfig) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

zookeeper:
  enabled: false
)";
    
    CreateTempConfig(yaml);
    
    setenv("ZK_HOSTS", "zk1:2181,zk2:2181", 1);
    setenv("ZK_SERVICE_NAME", "env-service", 1);
    setenv("ZK_ENABLED", "true", 1);
    setenv("ZK_REGISTER_SELF", "1", 1);
    setenv("ZK_ROOT_PATH", "/env-services", 1);
    setenv("ZK_WEIGHT", "200", 1);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    config.LoadFromEnv();
    
    EXPECT_EQ(config.zookeeper.hosts, "zk1:2181,zk2:2181");
    EXPECT_EQ(config.zookeeper.service_name, "env-service");
    EXPECT_TRUE(config.zookeeper.enabled);
    EXPECT_TRUE(config.zookeeper.register_self);
    EXPECT_EQ(config.zookeeper.root_path, "/env-services");
    EXPECT_EQ(config.zookeeper.weight, 200);
}

TEST_F(ConfigEnvOverrideTest, OverrideJwtConfig) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "original-secret"
  jwt_issuer: "original-issuer"
)";
    
    CreateTempConfig(yaml);
    
    setenv("JWT_SECRET", "env-secret-key-at-least-32-bytes!", 1);
    setenv("JWT_ISSUER", "env-issuer", 1);
    setenv("ACCESS_TOKEN_TTL", "7200", 1);
    setenv("REFRESH_TOKEN_TTL", "2592000", 1);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    config.LoadFromEnv();
    
    EXPECT_EQ(config.security.jwt_secret, "env-secret-key-at-least-32-bytes!");
    EXPECT_EQ(config.security.jwt_issuer, "env-issuer");
    EXPECT_EQ(config.security.access_token_ttl_seconds, 7200);
    EXPECT_EQ(config.security.refresh_token_ttl_seconds, 2592000);
}

TEST_F(ConfigEnvOverrideTest, OverrideSmsConfig) {
    std::string yaml = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    
    setenv("SMS_CODE_LEN", "4", 1);
    setenv("SMS_CODE_TTL", "600", 1);
    setenv("SMS_SEND_INTERVAL", "120", 1);
    
    Config config = Config::LoadFromFile(temp_config_path_);
    config.LoadFromEnv();
    
    EXPECT_EQ(config.sms.code_len, 4);
    EXPECT_EQ(config.sms.code_ttl_seconds, 600);
    EXPECT_EQ(config.sms.send_interval_seconds, 120);
}

// ============================================================================
// ToString 方法测试
// ============================================================================

class ConfigToStringTest : public ConfigTest {};

TEST_F(ConfigToStringTest, ServerConfigToString) {
    ServerConfig config;
    config.host = "192.168.1.1";
    config.grpc_port = 50051;
    config.metrics_port = 9090;
    
    std::string str = config.ToString();
    
    EXPECT_THAT(str, ::testing::HasSubstr("Server Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("192.168.1.1"));
    EXPECT_THAT(str, ::testing::HasSubstr("50051"));
    EXPECT_THAT(str, ::testing::HasSubstr("9090"));
}

TEST_F(ConfigToStringTest, MySQLConfigToStringHidesPassword) {
    MySQLConfig config;
    config.host = "db.example.com";
    config.password = "super_secret_password";
    
    std::string str = config.ToString();
    
    EXPECT_THAT(str, ::testing::HasSubstr("MySQL Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("db.example.com"));
    EXPECT_THAT(str, ::testing::HasSubstr("******"));
    EXPECT_THAT(str, ::testing::Not(::testing::HasSubstr("super_secret_password")));
}

TEST_F(ConfigToStringTest, MySQLConfigToStringShowsEmptyPassword) {
    MySQLConfig config;
    config.password = "";
    
    std::string str = config.ToString();
    
    EXPECT_THAT(str, ::testing::HasSubstr("(empty)"));
}

TEST_F(ConfigToStringTest, SecurityConfigToStringHidesSecret) {
    SecurityConfig config;
    config.jwt_secret = "my-super-secret-key";
    config.jwt_issuer = "my-issuer";
    
    std::string str = config.ToString();
    
    EXPECT_THAT(str, ::testing::HasSubstr("Security Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("******"));
    EXPECT_THAT(str, ::testing::HasSubstr("my-issuer"));
    EXPECT_THAT(str, ::testing::Not(::testing::HasSubstr("my-super-secret-key")));
}

TEST_F(ConfigToStringTest, ZooKeeperConfigToString) {
    ZooKeeperConfig config;
    config.hosts = "zk1:2181,zk2:2181";
    config.enabled = true;
    config.service_name = "test-service";
    
    std::string str = config.ToString();
    
    EXPECT_THAT(str, ::testing::HasSubstr("ZooKeeper Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("zk1:2181,zk2:2181"));
    EXPECT_THAT(str, ::testing::HasSubstr("test-service"));
    EXPECT_THAT(str, ::testing::HasSubstr("Yes"));  // enabled
}

TEST_F(ConfigToStringTest, FullConfigToString) {
    Config config;
    std::string str = config.ToString();
    
    // 应该包含所有子配置
    EXPECT_THAT(str, ::testing::HasSubstr("Server Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("MySQL Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("Redis Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("ZooKeeper Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("Security Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("SMS Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("Login Config"));
    EXPECT_THAT(str, ::testing::HasSubstr("Password Policy"));
    EXPECT_THAT(str, ::testing::HasSubstr("Log Config"));
}

// ============================================================================
// 辅助方法测试
// ============================================================================

class ConfigHelperMethodsTest : public ConfigTest {};

TEST_F(ConfigHelperMethodsTest, ZooKeeperGetServicePath) {
    ZooKeeperConfig config;
    config.root_path = "/services";
    config.service_name = "user-service";
    
    EXPECT_EQ(config.GetServicePath(), "/services/user-service");
}

TEST_F(ConfigHelperMethodsTest, MySQLGetPoolSize) {
    MySQLConfig config;
    config.pool_size = 25;
    
    EXPECT_EQ(config.GetPoolSize(), 25);
}

TEST_F(ConfigHelperMethodsTest, RedisGetPoolSize) {
    RedisConfig config;
    config.pool_size = 15;
    
    EXPECT_EQ(config.GetPoolSize(), 15);
}

// ============================================================================
// 边界条件测试
// ============================================================================

class ConfigBoundaryTest : public ConfigTest {};

TEST_F(ConfigBoundaryTest, PortBoundaryValues) {
    // 测试端口边界值
    std::string yaml_min_port = R"(
server:
  grpc_port: 1

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml_min_port);
    EXPECT_NO_THROW(Config::LoadFromFile(temp_config_path_));
    
    std::string yaml_max_port = R"(
server:
  grpc_port: 65535

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml_max_port);
    EXPECT_NO_THROW(Config::LoadFromFile(temp_config_path_));
}

TEST_F(ConfigBoundaryTest, PortOutOfRange) {
    std::string yaml = R"(
server:
  grpc_port: 65536

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"
)";
    
    CreateTempConfig(yaml);
    EXPECT_THROW(Config::LoadFromFile(temp_config_path_), std::runtime_error);
}

TEST_F(ConfigBoundaryTest, SmsCodeLengthBoundary) {
    // 测试验证码长度边界 (1-10)
    std::string yaml_valid = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

sms:
  code_len: 10
)";
    
    CreateTempConfig(yaml_valid);
    EXPECT_NO_THROW(Config::LoadFromFile(temp_config_path_));
    
    std::string yaml_invalid = R"(
server:
  grpc_port: 50051

mysql:
  host: "localhost"
  database: "test"

security:
  jwt_secret: "valid-secret-key-at-least-32-bytes!"
  jwt_issuer: "test"

sms:
  code_len: 11
)";
    
    CreateTempConfig(yaml_invalid);
    EXPECT_THROW(Config::LoadFromFile(temp_config_path_), std::runtime_error);
}

}  // namespace testing
}  // namespace user_service

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
