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
    if (config.security.jwt_issuer.empty()) {
        throw std::runtime_error("JWT issuer is empty");
    }

    // 校验 Token TTL 合法性
    if (config.security.access_token_ttl_seconds <= 0) {
        throw std::runtime_error("Invalid access token TTL: " + 
            std::to_string(config.security.access_token_ttl_seconds));
    }
    if (config.security.refresh_token_ttl_seconds <= 0) {
        throw std::runtime_error("Invalid refresh token TTL: " + 
            std::to_string(config.security.refresh_token_ttl_seconds));
    }
    // Refresh Token TTL 应该大于 Access Token TTL
    if (config.security.refresh_token_ttl_seconds <= config.security.access_token_ttl_seconds) {
        throw std::runtime_error("Refresh token TTL should be greater than access token TTL");
    }

    // 校验验证码配置合法性
    if (config.sms.code_len <= 0 || config.sms.code_len > 10) {
        throw std::runtime_error("Invalid SMS code length: " + std::to_string(config.sms.code_len));
    }
    if (config.sms.code_ttl_seconds <= 0) {
        throw std::runtime_error("Invalid SMS code TTL: " + std::to_string(config.sms.code_ttl_seconds));
    }
    if (config.sms.send_interval_seconds <= 0) {
        throw std::runtime_error("Invalid SMS send interval: " + std::to_string(config.sms.send_interval_seconds));
    }
    if (config.sms.max_retry_count <= 0) {
        throw std::runtime_error("Invalid SMS max retry count: " + std::to_string(config.sms.max_retry_count));
    }
    if (config.sms.retry_ttl_seconds <= 0) {
        throw std::runtime_error("Invalid SMS retry TTL: " + std::to_string(config.sms.retry_ttl_seconds));
    }
    if (config.sms.lock_seconds <= 0) {
        throw std::runtime_error("Invalid SMS lock seconds: " + std::to_string(config.sms.lock_seconds));
    }
    // 锁定时间应大于验证码有效期
    if (config.sms.lock_seconds < config.sms.code_ttl_seconds) {
        throw std::runtime_error("SMS lock seconds should be greater than or equal to code TTL");
    }

    // ============ 校验登录配置 ============
    if (config.login.max_failed_attempts <= 0) {
        throw std::runtime_error("Invalid login max failed attempts: " + 
            std::to_string(config.login.max_failed_attempts));
    }
    if (config.login.failed_attempts_window <= 0) {
        throw std::runtime_error("Invalid login failed attempts window: " + 
            std::to_string(config.login.failed_attempts_window));
    }
    if (config.login.lock_duration_seconds <= 0) {
        throw std::runtime_error("Invalid login lock duration: " + 
            std::to_string(config.login.lock_duration_seconds));
    }
    if (config.login.max_sessions_per_user <= 0) {
        throw std::runtime_error("Invalid max sessions per user: " + 
            std::to_string(config.login.max_sessions_per_user));
    }
    // 图形验证码触发阈值应小于等于最大失败次数
    if (config.login.captcha_after_failed_attempts > config.login.max_failed_attempts) {
        throw std::runtime_error("Captcha trigger threshold should be <= max failed attempts");
    }

    // ============ 校验密码策略配置 ============
    if (config.password.min_length <= 0) {
        throw std::runtime_error("Invalid password min length: " + 
            std::to_string(config.password.min_length));
    }
    if (config.password.max_length <= 0) {
        throw std::runtime_error("Invalid password max length: " + 
            std::to_string(config.password.max_length));
    }
    if (config.password.min_length > config.password.max_length) {
        throw std::runtime_error("Password min length should be <= max length");
    }
    if (config.password.expire_days < 0) {
        throw std::runtime_error("Invalid password expire days: " + 
            std::to_string(config.password.expire_days));
    }
    if (config.password.history_count < 0) {
        throw std::runtime_error("Invalid password history count: " + 
            std::to_string(config.password.history_count));
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

        // ============ Security（已更新） ============
        if (yaml["security"]) {
            const auto& s = yaml["security"];
            if (s["jwt_secret"]) 
                config.security.jwt_secret = s["jwt_secret"].as<std::string>();
            if (s["jwt_issuer"]) 
                config.security.jwt_issuer = s["jwt_issuer"].as<std::string>();
            if (s["access_token_ttl_seconds"]) 
                config.security.access_token_ttl_seconds = s["access_token_ttl_seconds"].as<int>();
            if (s["refresh_token_ttl_seconds"]) 
                config.security.refresh_token_ttl_seconds = s["refresh_token_ttl_seconds"].as<int>();
        }

        // ============ SMS 验证码配置 ============
        if (yaml["sms"]) {
            const auto& s = yaml["sms"];
            if (s["code_len"]) 
                config.sms.code_len = s["code_len"].as<int>();
            if (s["code_ttl_seconds"]) 
                config.sms.code_ttl_seconds = s["code_ttl_seconds"].as<int>();
            if (s["send_interval_seconds"]) 
                config.sms.send_interval_seconds = s["send_interval_seconds"].as<int>();
            if (s["max_retry_count"]) 
                config.sms.max_retry_count = s["max_retry_count"].as<int>();
            if (s["retry_ttl_seconds"]) 
                config.sms.retry_ttl_seconds = s["retry_ttl_seconds"].as<int>();
            if (s["lock_seconds"]) 
                config.sms.lock_seconds = s["lock_seconds"].as<int>();
        }

        // ============ Login 登录安全配置 ============
        if (yaml["login"]) {
            const auto& l = yaml["login"];
            if (l["max_failed_attempts"]) 
                config.login.max_failed_attempts = l["max_failed_attempts"].as<int>();
            if (l["failed_attempts_window"]) 
                config.login.failed_attempts_window = l["failed_attempts_window"].as<int>();
            if (l["lock_duration_seconds"]) 
                config.login.lock_duration_seconds = l["lock_duration_seconds"].as<int>();
            if (l["max_sessions_per_user"]) 
                config.login.max_sessions_per_user = l["max_sessions_per_user"].as<int>();
            if (l["kick_oldest_session"]) 
                config.login.kick_oldest_session = l["kick_oldest_session"].as<bool>();
            if (l["enable_password_login"]) 
                config.login.enable_password_login = l["enable_password_login"].as<bool>();
            if (l["enable_sms_login"]) 
                config.login.enable_sms_login = l["enable_sms_login"].as<bool>();
            if (l["require_captcha"]) 
                config.login.require_captcha = l["require_captcha"].as<bool>();
            if (l["captcha_after_failed_attempts"]) 
                config.login.captcha_after_failed_attempts = l["captcha_after_failed_attempts"].as<int>();
        }

        // ============ Password 密码策略配置 ============
        if (yaml["password"]) {
            const auto& p = yaml["password"];
            if (p["min_length"]) 
                config.password.min_length = p["min_length"].as<int>();
            if (p["max_length"]) 
                config.password.max_length = p["max_length"].as<int>();
            if (p["require_uppercase"]) 
                config.password.require_uppercase = p["require_uppercase"].as<bool>();
            if (p["require_lowercase"]) 
                config.password.require_lowercase = p["require_lowercase"].as<bool>();
            if (p["require_digit"]) 
                config.password.require_digit = p["require_digit"].as<bool>();
            if (p["require_special_char"]) 
                config.password.require_special_char = p["require_special_char"].as<bool>();
            if (p["expire_days"]) 
                config.password.expire_days = p["expire_days"].as<int>();
            if (p["history_count"]) 
                config.password.history_count = p["history_count"].as<int>();
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
    // MySQL
    if (const char* env = std::getenv("MYSQL_HOST")) mysql.host = env;
    if (const char* env = std::getenv("MYSQL_PASSWORD")) mysql.password = env;
    
    // Redis
    if (const char* env = std::getenv("REDIS_HOST")) redis.host = env;
    
    
    // Security
    if (const char* env = std::getenv("JWT_SECRET")) security.jwt_secret = env;
    if (const char* env = std::getenv("JWT_ISSUER")) security.jwt_issuer = env;
    if (const char* env = std::getenv("ACCESS_TOKEN_TTL")) {
        security.access_token_ttl_seconds = std::atoi(env);
    }
    if (const char* env = std::getenv("REFRESH_TOKEN_TTL")) {
        security.refresh_token_ttl_seconds = std::atoi(env);
    }

    // SMS 验证码配置
    if (const char* env = std::getenv("SMS_CODE_LEN")) {
        sms.code_len = std::atoi(env);
    }
    if (const char* env = std::getenv("SMS_CODE_TTL")) {
        sms.code_ttl_seconds = std::atoi(env);
    }
    if (const char* env = std::getenv("SMS_SEND_INTERVAL")) {
        sms.send_interval_seconds = std::atoi(env);
    }
    if (const char* env = std::getenv("SMS_MAX_RETRY")) {
        sms.max_retry_count = std::atoi(env);
    }
    if (const char* env = std::getenv("SMS_RETRY_TTL")) {
        sms.retry_ttl_seconds = std::atoi(env);
    }
    if (const char* env = std::getenv("SMS_LOCK_SECONDS")) {
        sms.lock_seconds = std::atoi(env);
    }

    // Login 登录安全配置（核心参数支持环境变量覆盖）
    if (const char* env = std::getenv("LOGIN_MAX_FAILED_ATTEMPTS")) {
        login.max_failed_attempts = std::atoi(env);
    }
    if (const char* env = std::getenv("LOGIN_LOCK_DURATION")) {
        login.lock_duration_seconds = std::atoi(env);
    }
    if (const char* env = std::getenv("LOGIN_MAX_SESSIONS")) {
        login.max_sessions_per_user = std::atoi(env);
    }

    // Password 密码策略配置（核心参数支持环境变量覆盖）
    if (const char* env = std::getenv("PASSWORD_MIN_LENGTH")) {
        password.min_length = std::atoi(env);
    }
    if (const char* env = std::getenv("PASSWORD_MAX_LENGTH")) {
        password.max_length = std::atoi(env);
    }
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

// ============ SecurityConfig::ToString ============
std::string SecurityConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Security Config ===" << std::endl;
    oss << "JWT Secret: " << (jwt_secret.empty() ? "(empty)" : "******") << std::endl;
    oss << "JWT Issuer: " << jwt_issuer << std::endl;
    oss << "Access Token TTL: " << access_token_ttl_seconds << " seconds ("
        << access_token_ttl_seconds / 60 << " minutes)" << std::endl;
    oss << "Refresh Token TTL: " << refresh_token_ttl_seconds << " seconds ("
        << refresh_token_ttl_seconds / 86400 << " days)" << std::endl;
    return oss.str();
}

// ============ SmsConfig::ToString ============
std::string SmsConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== SMS Config ===" << std::endl;
    oss << "Code Length: " << code_len << std::endl;
    oss << "Code TTL: " << code_ttl_seconds << " seconds ("
        << code_ttl_seconds / 60 << " minutes)" << std::endl;
    oss << "Send Interval: " << send_interval_seconds << " seconds" << std::endl;
    oss << "Max Retry Count: " << max_retry_count << std::endl;
    oss << "Retry TTL: " << retry_ttl_seconds << " seconds ("
        << retry_ttl_seconds / 60 << " minutes)" << std::endl;
    oss << "Lock Duration: " << lock_seconds << " seconds ("
        << lock_seconds / 60 << " minutes)" << std::endl;
    return oss.str();
}

// ============ LoginConfig::ToString ============
std::string LoginConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Login Config ===" << std::endl;
    oss << "Max Failed Attempts: " << max_failed_attempts << std::endl;
    oss << "Failed Attempts Window: " << failed_attempts_window << " seconds ("
        << failed_attempts_window / 60 << " minutes)" << std::endl;
    oss << "Lock Duration: " << lock_duration_seconds << " seconds ("
        << lock_duration_seconds / 60 << " minutes)" << std::endl;
    oss << "Max Sessions Per User: " << max_sessions_per_user << std::endl;
    oss << "Kick Oldest Session: " << (kick_oldest_session ? "Yes" : "No") << std::endl;
    oss << "Enable Password Login: " << (enable_password_login ? "Yes" : "No") << std::endl;
    oss << "Enable SMS Login: " << (enable_sms_login ? "Yes" : "No") << std::endl;
    oss << "Require Captcha: " << (require_captcha ? "Yes" : "No") << std::endl;
    oss << "Captcha After Failed: " << captcha_after_failed_attempts << " attempts" << std::endl;
    return oss.str();
}

// ============ PasswordPolicyConfig::ToString ============
std::string PasswordPolicyConfig::ToString() const {
    std::ostringstream oss;
    oss << "=== Password Policy ===" << std::endl;
    oss << "Length: " << min_length << " - " << max_length << " chars" << std::endl;
    oss << "Require Uppercase: " << (require_uppercase ? "Yes" : "No") << std::endl;
    oss << "Require Lowercase: " << (require_lowercase ? "Yes" : "No") << std::endl;
    oss << "Require Digit: " << (require_digit ? "Yes" : "No") << std::endl;
    oss << "Require Special Char: " << (require_special_char ? "Yes" : "No") << std::endl;
    oss << "Expire Days: " << (expire_days > 0 ? std::to_string(expire_days) + " days" : "Never") << std::endl;
    oss << "History Count: " << (history_count > 0 ? std::to_string(history_count) : "Not checked") << std::endl;
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
    oss << security.ToString() << std::endl;
    oss << sms.ToString() << std::endl;
    oss << login.ToString() << std::endl;
    oss << password.ToString() << std::endl;
    oss << log.ToString();
    oss << "==============================================================" << std::endl;
    return oss.str();
}

} // namespace user_service
