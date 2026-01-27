#pragma once
#include <librdkafka/rdkafkacpp.h>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include "expected"

#include "kafka/kafka_types.h"
#include "kafka/kafka_error.h"
namespace user_service{

class KafkaConfig;

// Kafka生产者
class KafkaProducer{
public:
    KafkaProducer()=default;
    ~KafkaProducer();

    /// @brief 初始化生产者
    /// @return 成功返回 void，失败返回错误信息
    KafkaResult<> init(const KafkaConfig& config);
    
    /// @brief 发送通用消息
    /// @return 成功返回 void，失败返回错误信息
    KafkaResult<> Send(const std::string& topic,const std::string& key,const std::string& value);

    /// @brief 发送用户事件
    /// @return 成功返回 void，失败返回错误信息
    KafkaResult<> SendUserEvent(const std::string& topic,const UserEvent& event);

    /// @brief 刷新待发送消息
    /// @return 成功返回实际刷新的消息数，失败返回错误信息
    KafkaResult<size_t> Flush(int timeout_ms=1000);

    // 暴露给 Prometheus/监控系统
    std::unordered_map<std::string, uint64_t> GetMetrics() const;

private:
    /// @brief 性能指标
    struct Metrics{
        std::atomic<uint64_t> total_sent_{0};           // 发送的总次数
        std::atomic<uint64_t> queue_full_count_{0};     // 队列满的次数
        std::atomic<uint64_t> retry_count_{0};          // 重试次数
        std::atomic<uint64_t> failed_count_{0};         // 失败次数
    };

    std::unique_ptr<KafkaConfig> kafkaConf_=nullptr;    //外部配合

    // Kafka生产者实例（智能指针自动管理生命周期，避免内存泄漏）
    std::unique_ptr<RdKafka::Producer> producer_=nullptr;
    // Kafka生产者配置对象（存储brokers、client.id、重试策略等核心配置）
    std::unique_ptr<RdKafka::Conf> conf_=nullptr;

    Metrics perfm_;
};

}