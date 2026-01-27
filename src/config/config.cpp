// config.cpp
#include "config/config.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <sstream>
#include <cstdlib>

namespace user_service {

// 配置加载完成后，添加合法性校验
void ValidateConfig(const Config& config) {
    // 校验端口合法性（1-65535）
    if (config.server.grpc_port <= 0 || config.server.grpc_port > 65535) {
        throw std::runtime_error("Invalid gRPC port: " + std::to_string(config.server.grpc_port));
    }
    if (config.server.metrics_port <= 0 || config.server.metrics_port > 65535) {
        throw std::runtime_error("Invalid metrics port: " + std::to_string(config.server.metrics_port));
    }
    if (config.mysql.port <= 0 || config.mysql.port > 65535) {
        throw std::runtime_error("Invalid MySQL port: " + std::to_string(config.mysql.port));
    }

    // 校验连接池大小（大于 0）
    if (config.mysql.pool_size <= 0) {
        throw std::runtime_error("Invalid MySQL pool size: " + std::to_string(config.mysql.pool_size));
    }
    if (config.redis.pool_size <= 0) {
        throw std::runtime_error("Invalid Redis pool size: " + std::to_string(config.redis.pool_size));
    }

    // 校验核心配置非空
    if (config.mysql.host.empty()) {
        throw std::runtime_error("MySQL host is empty");
    }
    if (config.mysql.database.empty()) {
        throw std::runtime_error("MySQL database is empty");
    }
    if (config.security.jwt_secret.empty()) {
        throw std::runtime_error("JWT secret is empty");
    }
    if (config.kafka.brokers.empty()) {
        throw std::runtime_error("Kafka brokers is empty");
    }
}

Config Config::LoadFromFile(const std::string& path) {
    Config config;
    try {
        YAML::Node yaml = YAML::LoadFile(path);

        // Server
        if (yaml["server"]) {
            const auto& s = yaml["server"];
            if (s["host"]) config.server.host = s["host"].as<std::string>();
            if (s["grpc_port"]) config.server.grpc_port = s["grpc_port"].as<int>();
            if (s["metrics_port"]) config.server.metrics_port = s["metrics_port"].as<int>();
        }

        // MySQL
        if (yaml["mysql"]) {
            const auto& m = yaml["mysql"];
            if (m["host"]) config.mysql.host = m["host"].as<std::string>();
            if (m["port"]) config.mysql.port = m["port"].as<int>();
            if (m["database"]) config.mysql.database = m["database"].as<std::string>();
            if (m["username"]) config.mysql.username = m["username"].as<std::string>();
            if (m["password"]) config.mysql.password = m["password"].as<std::string>();
            if (m["pool_size"]) config.mysql.pool_size = m["pool_size"].as<int>();
            if (m["connection_timeout_ms"]) config.mysql.connection_timeout_ms = m["connection_timeout_ms"].as<unsigned int>();
            if (m["read_timeout_ms"]) config.mysql.read_timeout_ms = m["read_timeout_ms"].as<unsigned int>();
            if (m["write_timeout_ms"]) config.mysql.write_timeout_ms = m["write_timeout_ms"].as<unsigned int>();
            if (m["max_retries"]) config.mysql.max_retries = m["max_retries"].as<unsigned int>();
            if (m["retry_interval_ms"]) config.mysql.retry_interval_ms = m["retry_interval_ms"].as<unsigned int>();
            if (m["auto_reconnect"]) config.mysql.auto_reconnect = m["auto_reconnect"].as<bool>();
            if (m["charset"]) config.mysql.charset = m["charset"].as<std::string>();
        }

        // Redis
        if (yaml["redis"]) {
            const auto& r = yaml["redis"];
            if (r["host"]) config.redis.host = r["host"].as<std::string>();
            if (r["port"]) config.redis.port = r["port"].as<int>();
            if (r["password"]) config.redis.password = r["password"].as<std::string>();
            if (r["db"]) config.redis.db = r["db"].as<int>();
            if (r["pool_size"]) config.redis.pool_size = r["pool_size"].as<int>();
            if (r["wait_timeout_ms"]) config.redis.wait_timeout_ms = r["wait_timeout_ms"].as<unsigned int>();
            if (r["connect_timeout_ms"]) config.redis.connect_timeout_ms = r["connect_timeout_ms"].as<unsigned int>();
            if (r["socket_timeout_ms"]) config.redis.socket_timeout_ms = r["socket_timeout_ms"].as<unsigned int>();
        }

        // Kafka
        if (yaml["kafka"]) {
            const auto& k = yaml["kafka"];
            
            // 基础配置
            if (k["brokers"]) config.kafka.brokers = k["brokers"].as<std::string>();
            if (k["client_id"]) config.kafka.client_id = k["client_id"].as<std::string>();
            
            // Topics
            if (k["topics"]) {
                const auto& topics = k["topics"];
                if (topics["user_events"]) config.kafka.user_events = topics["user_events"].as<std::string>();
            }
            
            // Producer 配置
            if (k["producer"]) {
                const auto& p = k["producer"];
                if (p["acks"]) config.kafka.producer.acks = p["acks"].as<std::string>();
                if (p["enable_idempotence"]) config.kafka.producer.enable_idempotence = p["enable_idempotence"].as<bool>();
                if (p["retries"]) config.kafka.producer.retries = p["retries"].as<int>();
                if (p["retry_backoff_ms"]) config.kafka.producer.retry_backoff_ms = p["retry_backoff_ms"].as<int>();
                if (p["delivery_timeout_ms"]) config.kafka.producer.delivery_timeout_ms = p["delivery_timeout_ms"].as<int>();
                if (p["batch_size"]) config.kafka.producer.batch_size = p["batch_size"].as<int>();
                if (p["linger_ms"]) config.kafka.producer.linger_ms = p["linger_ms"].as<int>();
                if (p["compression_codec"]) config.kafka.producer.compression_codec = p["compression_codec"].as<std::string>();
                if (p["queue_buffering_max_messages"]) config.kafka.producer.queue_buffering_max_messages = p["queue_buffering_max_messages"].as<int>();
                if (p["queue_buffering_max_kbytes"]) config.kafka.producer.queue_buffering_max_kbytes = p["queue_buffering_max_kbytes"].as<int>();
            }
            
            // Consumer 配置
            if (k["consumer"]) {
                const auto& c = k["consumer"];
                if (c["group_id"]) config.kafka.consumer.group_id = c["group_id"].as<std::string>();
                if (c["auto_offset_reset"]) config.kafka.consumer.auto_offset_reset = c["auto_offset_reset"].as<std::string>();
                if (c["enable_auto_commit"]) config.kafka.consumer.enable_auto_commit = c["enable_auto_commit"].as<bool>();
                if (c["max_poll_records"]) config.kafka.consumer.max_poll_records = c["max_poll_records"].as<int>();
                if (c["session_timeout_ms"]) config.kafka.consumer.session_timeout_ms = c["session_timeout_ms"].as<int>();
                if (c["heartbeat_interval_ms"]) config.kafka.consumer.heartbeat_interval_ms = c["heartbeat_interval_ms"].as<int>();
            }
            
            // Network 配置
            if (k["network"]) {
                const auto& n = k["network"];
                if (n["socket_timeout_ms"]) config.kafka.network.socket_timeout_ms = n["socket_timeout_ms"].as<int>();
                if (n["reconnect_backoff_ms"]) config.kafka.network.reconnect_backoff_ms = n["reconnect_backoff_ms"].as<int>();
                if (n["reconnect_backoff_max_ms"]) config.kafka.network.reconnect_backoff_max_ms = n["reconnect_backoff_max_ms"].as<int>();
            }
        }

        // ZooKeeper
        if (yaml["zookeeper"]) {
            const auto& z = yaml["zookeeper"];
            if (z["hosts"]) config.zookeeper.hosts = z["hosts"].as<std::string>();
            if (z["session_timeout_ms"]) config.zookeeper.session_timeout_ms = z["session_timeout_ms"].as<int>();
            if (z["service_path"]) config.zookeeper.service_path = z["service_path"].as<std::string>();
        }

        // Security
        if (yaml["security"]) {
            const auto& s = yaml["security"];
            if (s["jwt_secret"]) config.security.jwt_secret = s["jwt_secret"].as<std::string>();
            if (s["token_ttl"]) config.security.token_ttl = std::chrono::seconds(s["token_ttl"].as<int>());
            if (s["refresh_token_ttl"]) config.security.refresh_token_ttl = std::chrono::seconds(s["refresh_token_ttl"].as<int>());
        }

        // Log
        if (yaml["log"]) {
            const auto& l = yaml["log"];
            if (l["level"]) config.log.level = l["level"].as<std::string>();
            if (l["path"]) config.log.path = l["path"].as<std::string>();
            if (l["filename"]) config.log.filename = l["filename"].as<std::string>();
            if (l["max_size"]) config.log.max_size = l["max_size"].as<size_t>();
            if (l["max_files"]) config.log.max_files = l["max_files"].as<int>();
            if (l["console_output"]) config.log.console_output = l["console_output"].as<bool>();
        }
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load config: " + std::string(e.what()));
    }

    ValidateConfig(config);
    return config;
}

void Config::LoadFromEnv() {
    if (const char* env = std::getenv("MYSQL_HOST")) mysql.host = env;
    if (const char* env = std::getenv("MYSQL_PASSWORD")) mysql.password = env;
    if (const char* env = std::getenv("REDIS_HOST")) redis.host = env;
    if (const char* env = std::getenv("KAFKA_BROKERS")) kafka.brokers = env;
    if (const char* env = std::getenv("ZK_HOSTS")) zookeeper.hosts = env;
    if (const char* env = std::getenv("JWT_SECRET")) security.jwt_secret = env;
}

// ========================== ToString 实现 ==========================

std::string ServerConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Server Config ===" << std::endl;
    oss << "Host: " << host << std::endl;
    oss << "gRPC Port: " << grpc_port << std::endl;
    oss << "Metrics Port: " << metrics_port << std::endl;
    return oss.str();
}

std::string MySQLConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== MySQL Config ===" << std::endl;
    oss << "Host: " << host << std::endl;
    oss << "Port: " << port << std::endl;
    oss << "Database: " << database << std::endl;
    oss << "Username: " << username << std::endl;
    oss << "Password: " << (password.empty() ? "(empty)" : "******") << std::endl;
    oss << "Pool Size: " << pool_size << std::endl;
    oss << "Connection Timeout(ms): " 
        << (connection_timeout_ms ? std::to_string(*connection_timeout_ms) : "(not set)") << std::endl;
    oss << "Read Timeout(ms): " 
        << (read_timeout_ms ? std::to_string(*read_timeout_ms) : "(not set)") << std::endl;
    oss << "Write Timeout(ms): " 
        << (write_timeout_ms ? std::to_string(*write_timeout_ms) : "(not set)") << std::endl;
    oss << "Max Retries: " << max_retries << std::endl;
    oss << "Retry Interval(ms): " << retry_interval_ms << std::endl;
    oss << "Auto Reconnect: " 
        << (auto_reconnect ? (*auto_reconnect ? "true" : "false") : "(not set)") << std::endl;
    oss << "Charset: " << charset << std::endl;
    return oss.str();
}

std::string RedisConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Redis Config ===" << std::endl;
    oss << "Host: " << host << std::endl;
    oss << "Port: " << port << std::endl;
    oss << "Password: " << (password.empty() ? "(empty)" : "******") << std::endl;
    oss << "DB Index: " << db << std::endl;
    oss << "Pool Size: " << pool_size << std::endl;
    oss << "Wait Timeout(ms): " << wait_timeout_ms << std::endl;
    oss << "Connect Timeout(ms): " 
        << (connect_timeout_ms ? std::to_string(*connect_timeout_ms) : "(not set)") << std::endl;
    oss << "Socket Timeout(ms): " 
        << (socket_timeout_ms ? std::to_string(*socket_timeout_ms) : "(not set)") << std::endl;
    return oss.str();
}

// ========================== Kafka 子配置 ToString ==========================

std::string KafkaProducerConfig::ToString() const {
    std::ostringstream oss;
    oss << "  --- Producer ---" << std::endl;
    oss << "  Acks: " << (acks ? *acks : "(not set)") << std::endl;
    oss << "  Enable Idempotence: " 
        << (enable_idempotence ? (*enable_idempotence ? "true" : "false") : "(not set)") << std::endl;
    oss << "  Retries: " << (retries ? std::to_string(*retries) : "(not set)") << std::endl;
    oss << "  Retry Backoff(ms): " 
        << (retry_backoff_ms ? std::to_string(*retry_backoff_ms) : "(not set)") << std::endl;
    oss << "  Delivery Timeout(ms): " 
        << (delivery_timeout_ms ? std::to_string(*delivery_timeout_ms) : "(not set)") << std::endl;
    oss << "  Batch Size: " << (batch_size ? std::to_string(*batch_size) : "(not set)") << std::endl;
    oss << "  Linger(ms): " << (linger_ms ? std::to_string(*linger_ms) : "(not set)") << std::endl;
    oss << "  Compression Codec: " << (compression_codec ? *compression_codec : "(not set)") << std::endl;
    oss << "  Queue Max Messages: " 
        << (queue_buffering_max_messages ? std::to_string(*queue_buffering_max_messages) : "(not set)") << std::endl;
    oss << "  Queue Max KBytes: " 
        << (queue_buffering_max_kbytes ? std::to_string(*queue_buffering_max_kbytes) : "(not set)") << std::endl;
    return oss.str();
}

std::string KafkaConsumerConfig::ToString() const {
    std::ostringstream oss;
    oss << "  --- Consumer ---" << std::endl;
    oss << "  Group ID: " << (group_id ? *group_id : "(not set)") << std::endl;
    oss << "  Auto Offset Reset: " << (auto_offset_reset ? *auto_offset_reset : "(not set)") << std::endl;
    oss << "  Enable Auto Commit: " 
        << (enable_auto_commit ? (*enable_auto_commit ? "true" : "false") : "(not set)") << std::endl;
    oss << "  Max Poll Records: " 
        << (max_poll_records ? std::to_string(*max_poll_records) : "(not set)") << std::endl;
    oss << "  Session Timeout(ms): " 
        << (session_timeout_ms ? std::to_string(*session_timeout_ms) : "(not set)") << std::endl;
    oss << "  Heartbeat Interval(ms): " 
        << (heartbeat_interval_ms ? std::to_string(*heartbeat_interval_ms) : "(not set)") << std::endl;
    return oss.str();
}

std::string KafkaNetworkConfig::ToString() const {
    std::ostringstream oss;
    oss << "  --- Network ---" << std::endl;
    oss << "  Socket Timeout(ms): " 
        << (socket_timeout_ms ? std::to_string(*socket_timeout_ms) : "(not set)") << std::endl;
    oss << "  Reconnect Backoff(ms): " 
        << (reconnect_backoff_ms ? std::to_string(*reconnect_backoff_ms) : "(not set)") << std::endl;
    oss << "  Reconnect Backoff Max(ms): " 
        << (reconnect_backoff_max_ms ? std::to_string(*reconnect_backoff_max_ms) : "(not set)") << std::endl;
    return oss.str();
}

std::string KafkaConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Kafka Config ===" << std::endl;
    oss << "Brokers: " << brokers << std::endl;
    oss << "Client ID: " << client_id << std::endl;
    oss << "User Events Topic: " << user_events << std::endl;
    oss << producer.ToString();
    oss << consumer.ToString();
    oss << network.ToString();
    return oss.str();
}

// ========================== 其他配置 ToString ==========================

std::string ZooKeeperConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== ZooKeeper Config ===" << std::endl;
    oss << "Hosts: " << hosts << std::endl;
    oss << "Session Timeout(ms): " << session_timeout_ms << std::endl;
    oss << "Service Path: " << service_path << std::endl;
    return oss.str();
}

std::string SecurityConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Security Config ===" << std::endl;
    oss << "JWT Secret: " << (jwt_secret.empty() ? "(empty)" : "******") << std::endl;
    oss << "Token TTL(seconds): " << token_ttl.count() << std::endl;
    oss << "Refresh Token TTL(seconds): " << refresh_token_ttl.count() << std::endl;
    return oss.str();
}

std::string LogConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Log Config ===" << std::endl;
    oss << "Level: " << level << std::endl;
    oss << "Path: " << path << std::endl;
    oss << "Filename: " << filename << std::endl;
    oss << "Max Size: " << max_size / 1024 / 1024 << " MB (" << max_size << " bytes)" << std::endl;
    oss << "Max Files: " << max_files << std::endl;
    oss << "Console Output: " << (console_output ? "Enabled" : "Disabled") << std::endl;
    return oss.str();
}

std::string Config::ToString() const {
    std::ostringstream oss;
    oss << "==================== User Service Config ====================" << std::endl;
    oss << server.ToString() << std::endl;
    oss << mysql.ToString() << std::endl;
    oss << redis.ToString() << std::endl;
    oss << kafka.ToString() << std::endl;
    oss << zookeeper.ToString() << std::endl;
    oss << security.ToString() << std::endl;
    oss << log.ToString();
    oss << "==============================================================" << std::endl;
    return oss.str();
}

} // namespace user
