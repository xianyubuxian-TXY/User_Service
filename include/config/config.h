// src/config/config.h
#pragma once

#include <string>
#include <chrono>
#include <optional>
namespace user_service {

// ==================================== gRPC服务 配置 ===============================

struct ServerConfig {
    std::string host = "0.0.0.0";
    int grpc_port = 50051;
    int metrics_port = 9090; //独立端口暴露监控指标

    std::string ToString() const;
};

// ==================================== Mysql 配置 ===============================

struct MySQLConfig {
    //基本连接信息
    std::string host = "localhost";
    int port = 3306;
    std::string database = "user_service";
    std::string username = "root";
    std::string password = "";
    int pool_size = 10;

    //超时配置(ms)
    std::optional<unsigned int> connection_timeout_ms;
    std::optional<unsigned int> read_timeout_ms;
    std::optional<unsigned int> write_timeout_ms;
    
    //重试配置
    unsigned int max_retries=3; 
    unsigned int retry_interval_ms=1000; //重试间隔（ms）

    //其它配置
    std::optional<bool> auto_reconnect;
    std::string charset= "utf8mb4";

    int GetPoolSize() const{ return pool_size;}

    std::string ToString() const;
};

// ==================================== Redis 配置 ===============================

struct RedisConfig {
    std::string host = "localhost";
    int port = 6379;
    std::string password = "";
    int db = 0;
    int pool_size = 5;

    std::optional<unsigned int> connect_timeout_ms; //连接超时
    std::optional<unsigned int> socket_timeout_ms;  //读写超时
    
    unsigned int wait_timeout_ms=100;
    int GetPoolSize()const {return pool_size;}

    std::string ToString() const;
};

// ==================================== Logger 配置 ===============================

struct LogConfig {
    std::string level = "info";
    std::string path = "./logs";
    std::string filename = "user-service.log";
    size_t max_size = 10 * 1024 * 1024;  // 10MB
    int max_files = 5;
    bool console_output = true;

    std::string ToString() const;
};


// ==================================== ZooKeeper 配置 ===============================

struct ZooKeeperConfig {
    // ==================== 连接配置 ====================
    std::string hosts = "127.0.0.1:2181";       // ZK 服务器地址，多个用逗号分隔
                                                 // 例如: "192.168.1.10:2181,192.168.1.11:2181"
    int session_timeout_ms = 15000;              // 会话超时（毫秒），推荐 10000-30000
    int connect_timeout_ms = 10000;              // 连接超时（毫秒）
    
    // ==================== 服务注册配置（服务端使用） ====================
    std::string root_path = "/services";         // 服务根路径
    std::string service_name = "user-service";   // 当前服务名称
    
    // ==================== 开关 ====================
    bool enabled = true;                         // 是否启用服务注册/发现
    bool register_self = true;                   // 是否注册自身（服务端设为true，纯客户端设为false）
    
    // ==================== 元数据 ====================
    int weight = 100;                            // 服务权重（负载均衡用）
    std::string region = "";                     // 区域标识，如 "cn-east"
    std::string zone = "";                       // 可用区，如 "zone-a"
    std::string version = "1.0.0";               // 服务版本
    
    /**
     * @brief 获取完整的服务路径
     * @return 例如 "/services/user-service"
     */
    std::string GetServicePath() const {
        return root_path + "/" + service_name;
    }
    
    std::string ToString() const;
};



// ==================================== 业务相关 ===============================

// token配置
struct SecurityConfig {
    std::string jwt_secret = "your-secret-key";
    std::string jwt_issuer = "user-service";        
    int access_token_ttl_seconds{900};              // access_token有效期：15分钟
    int refresh_token_ttl_seconds{604800};          // refresh_token有效期：7天

    std::string ToString() const;
};

// 验证码配置
struct SmsConfig{
    // 验证码长度
    int code_len=6;

    // 验证码有效期（秒）
    int code_ttl_seconds=300;       // 5分钟有效期

    // 发送间隔（秒）- 防止频繁发送（可以覆盖之前的验证码，方便用户没有收到验证码时快速重新获取）
    int send_interval_seconds=60;    // 60秒才能重发
    
    // 最大验证错误次数
    int max_retry_count=5;          

    // 错误次数记录有效期（秒）
    int retry_ttl_seconds=300;      // 3分钟 

    // 失败次数超过max_retry_count，进行锁定（避免被暴力破解）
    int lock_seconds = 1800;            // 锁定30分钟

    std::string ToString() const;
};

// ============ 登录安全配置 ============
struct LoginConfig {
    // ==================== 登录失败锁定策略 ====================
    int max_failed_attempts = 5;        // 最大登录失败次数
    int failed_attempts_window = 900;   // 失败计数窗口期（秒），15分钟
    int lock_duration_seconds = 1800;   // 锁定时长（秒），30分钟
    
    // ==================== 会话管理 ====================
    int max_sessions_per_user = 5;      // 单用户最大同时登录设备数
    bool kick_oldest_session = true;    // 超出时是否踢掉最旧的会话（false则拒绝新登录）
    
    // ==================== 登录方式开关 ====================
    bool enable_password_login = true;  // 允许密码登录
    bool enable_sms_login = true;       // 允许验证码登录
    
    // ==================== 图形验证码 ====================
    bool require_captcha = false;                   // 是否强制图形验证码
    int captcha_after_failed_attempts = 3;          // 失败N次后需要图形验证码

    std::string ToString() const;
};

// ============ 密码策略配置 ============
struct PasswordPolicyConfig {
    int min_length = 8;                 // 最小长度
    int max_length = 32;                // 最大长度
    bool require_uppercase = false;     // 需要大写字母
    bool require_lowercase = false;     // 需要小写字母
    bool require_digit = true;          // 需要数字
    bool require_special_char = false;  // 需要特殊字符
    
    // 密码过期（0 = 不过期）
    int expire_days = 0;                
    
    // 历史密码检查（0 = 不检查）
    int history_count = 0;              // 不能与最近N个密码相同

    std::string ToString() const;
};

struct Config {
    ServerConfig server;
    MySQLConfig mysql;
    RedisConfig redis;
    LogConfig log;
    ZooKeeperConfig zookeeper;
    
    // 业务配置
    SecurityConfig security;
    SmsConfig sms;
    LoginConfig login;                  // 登录安全策略
    PasswordPolicyConfig password;      // 密码强度策略
    
    // 从 YAML 文件加载配置
    static Config LoadFromFile(const std::string& path);
    
    // 从环境变量覆盖配置
    void LoadFromEnv();

    std::string ToString() const;
};

} // namespace user_service
