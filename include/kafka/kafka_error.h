#pragma once

namespace user_service{

// ============================================================================
// Kafka 层独立错误码（基础设施层，与业务无关）
// ============================================================================
enum class KafkaError {
    Ok = 0,
    
    // 初始化相关
    NotInitialized,         // 未初始化
    ConfigError,            // 配置错误
    
    // 连接相关
    BrokerUnavailable,      // Broker 不可用
    AuthFailed,             // 认证失败
    
    // 发送相关
    QueueFull,              // 本地队列满
    MessageTooLarge,        // 消息过大
    Timeout,                // 超时
    
    // 序列化相关
    SerializationFailed,    // 序列化失败
    
    // 其他
    Unknown,                // 未知错误
};

// 错误码 → 字符串（用于日志）
inline const char* KafkaErrorToString(KafkaError err) {
    switch (err) {
        case KafkaError::Ok:                  return "Ok";
        case KafkaError::NotInitialized:      return "NotInitialized";
        case KafkaError::ConfigError:         return "ConfigError";
        case KafkaError::BrokerUnavailable:   return "BrokerUnavailable";
        case KafkaError::AuthFailed:          return "AuthFailed";
        case KafkaError::QueueFull:           return "QueueFull";
        case KafkaError::MessageTooLarge:     return "MessageTooLarge";
        case KafkaError::Timeout:             return "Timeout";
        case KafkaError::SerializationFailed: return "SerializationFailed";
        case KafkaError::Unknown:             return "Unknown";
        default:                              return "Unknown";
    }
}

// Result 类型
template<typename T = void>
using KafkaResult = std::expected<T, KafkaError>;


}