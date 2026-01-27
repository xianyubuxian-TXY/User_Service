// src/config/config.h
#pragma once

#include <string>
#include <chrono>
#include <optional>
namespace user_service {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int grpc_port = 50051;
    int metrics_port = 9090; //独立端口暴露监控指标

    std::string ToString() const;
};

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

// ============ Kafka Producer 配置 ============
struct KafkaProducerConfig {
    std::optional<std::string> acks;
    std::optional<bool> enable_idempotence;
    std::optional<int> retries;
    std::optional<int> retry_backoff_ms;
    std::optional<int> delivery_timeout_ms;
    std::optional<int> batch_size;
    std::optional<int> linger_ms;
    std::optional<std::string> compression_codec;
    std::optional<int> queue_buffering_max_messages;
    std::optional<int> queue_buffering_max_kbytes;

    std::string ToString() const;
};

// ============ Kafka Consumer 配置 ============
struct KafkaConsumerConfig {
    std::optional<std::string> group_id;
    std::optional<std::string> auto_offset_reset;
    std::optional<bool> enable_auto_commit;
    std::optional<int> max_poll_records;
    std::optional<int> session_timeout_ms;
    std::optional<int> heartbeat_interval_ms;

    std::string ToString() const;
};

// ============ Kafka 网络配置 ============
struct KafkaNetworkConfig {
    std::optional<int> socket_timeout_ms;
    std::optional<int> reconnect_backoff_ms;
    std::optional<int> reconnect_backoff_max_ms;

    std::string ToString() const;
};


// ============ Kafka 主配置 ============
struct KafkaConfig {
    std::string brokers = "localhost:9092";
    std::string user_events="user-events";
    std::string client_id= "user-service";
    
    KafkaProducerConfig producer;
    KafkaConsumerConfig consumer;
    KafkaNetworkConfig network;

    std::string ToString() const;
};



struct ZooKeeperConfig {
    std::string hosts = "localhost:2181";
    int session_timeout_ms = 30000;
    std::string service_path = "/services/user-service";

    std::string ToString() const;
};

struct SecurityConfig {
    std::string jwt_secret = "your-secret-key";
    std::chrono::seconds token_ttl{900};           // 15分钟
    std::chrono::seconds refresh_token_ttl{604800}; // 7天

    std::string ToString() const;
};

struct LogConfig {
    std::string level = "info";
    std::string path = "./logs";
    std::string filename = "user-service.log";
    size_t max_size = 10 * 1024 * 1024;  // 10MB
    int max_files = 5;
    bool console_output = true;

    std::string ToString() const;
};

struct Config {
    ServerConfig server;
    MySQLConfig mysql;
    RedisConfig redis;
    KafkaConfig kafka;
    ZooKeeperConfig zookeeper;
    SecurityConfig security;
    LogConfig log;
    
    // 从 YAML 文件加载配置
    static Config LoadFromFile(const std::string& path);
    
    // 从环境变量覆盖配置
    void LoadFromEnv();

    std::string ToString() const;
};

} // namespace user