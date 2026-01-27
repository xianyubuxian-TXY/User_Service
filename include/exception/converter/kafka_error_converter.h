// kafka/kafka_error_converter.h
#pragma once

#include "kafka/kafka_error.h"
#include "common/error_codes.h"
#include "exception/client_exception.h"
#include "common/logger.h"

namespace user_service {

// KafkaError → ErrorCode 转换
inline ErrorCode ToErrorCode(KafkaError err) {
    switch (err) {
        case KafkaError::Ok:
            return ErrorCode::Ok;
            
        case KafkaError::QueueFull:
            return ErrorCode::RateLimited;       // 队列满 → 限流
            
        case KafkaError::Timeout:
            return ErrorCode::Timeout;           // 超时 → 超时
            
        case KafkaError::NotInitialized:
        case KafkaError::ConfigError:
        case KafkaError::AuthFailed:
            return ErrorCode::Internal;          // 配置/认证问题 → 内部错误
            
        case KafkaError::BrokerUnavailable:
            return ErrorCode::ServiceUnavailable; // Broker 挂了 → 服务不可用
            
        case KafkaError::MessageTooLarge:
        case KafkaError::SerializationFailed:
            return ErrorCode::Internal;          // 序列化问题 → 内部错误
            
        default:
            return ErrorCode::Internal;
    }
}

// 便捷函数：KafkaResult 失败时抛出 ClientException
template<typename T>
T UnwrapKafkaResult(KafkaResult<T>&& result, const std::string& operation = "") {
    if (result) {
        return std::move(*result);
    }
    
    KafkaError err = result.error();
    LOG_ERROR("[Kafka] %s failed: %s", 
              operation.c_str(), KafkaErrorToString(err));
    
    throw ClientException(ToErrorCode(err));
}

// void 特化
inline void UnwrapKafkaResult(KafkaResult<void>&& result, const std::string& operation = "") {
    if (result) {
        return;
    }
    
    KafkaError err = result.error();
    LOG_ERROR("[Kafka] %s failed: %s", 
              operation.c_str(), KafkaErrorToString(err));
    
    throw ClientException(ToErrorCode(err));
}

}  // namespace user_service
