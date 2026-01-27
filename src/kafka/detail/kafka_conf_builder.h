#pragma once
#include <librdkafka/rdkafkacpp.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "config/config.h"

namespace user_service{

//RdKafka::Conf构造器
class KafkaConfBuilder{
public:
    KafkaConfBuilder() {
        conf_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    }

    // 基础设置方法
    KafkaConfBuilder& Set(const std::string& key, const std::string& value) {
        std::string errstr;
        if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
            errors_.push_back("Failed to set " + key + ": " + errstr);
        }
        return *this;
    }

    // 设置回调
    template<typename T>
    KafkaConfBuilder& SetCallBack(const std::string& key, T* cb) {
        std::string errstr;
        if (conf_->set(key, cb, errstr) != RdKafka::Conf::CONF_OK) {
            errors_.push_back("Failed to set callback " + key + ": " + errstr);
        }
        return *this;
    }

    // 应用 Producer 配置
    KafkaConfBuilder& ApplyProducerConfig(const KafkaProducerConfig& config) {
        if (config.acks) Set("acks", *config.acks);
        if (config.enable_idempotence) Set("enable.idempotence", *config.enable_idempotence ? "true" : "false");
        if (config.retries) Set("retries", std::to_string(*config.retries));
        if (config.retry_backoff_ms) Set("retry.backoff.ms", std::to_string(*config.retry_backoff_ms));
        if (config.delivery_timeout_ms) Set("delivery.timeout.ms", std::to_string(*config.delivery_timeout_ms));
        if (config.batch_size) Set("batch.size", std::to_string(*config.batch_size));
        if (config.linger_ms) Set("queue.buffering.max.ms", std::to_string(*config.linger_ms));
        if (config.compression_codec) Set("compression.codec", *config.compression_codec);
        if (config.queue_buffering_max_messages) Set("queue.buffering.max.messages", std::to_string(*config.queue_buffering_max_messages));
        if (config.queue_buffering_max_kbytes) Set("queue.buffering.max.kbytes", std::to_string(*config.queue_buffering_max_kbytes));
        return *this;
    }

    // 应用 Consumer 配置
    KafkaConfBuilder& ApplyConsumerConfig(const KafkaConsumerConfig& config) {
        if (config.group_id) Set("group.id", *config.group_id);
        if (config.auto_offset_reset) Set("auto.offset.reset", *config.auto_offset_reset);
        if (config.enable_auto_commit) Set("enable.auto.commit", *config.enable_auto_commit ? "true" : "false");
        if (config.max_poll_records) Set("max.poll.records", std::to_string(*config.max_poll_records));
        if (config.session_timeout_ms) Set("session.timeout.ms", std::to_string(*config.session_timeout_ms));
        if (config.heartbeat_interval_ms) Set("heartbeat.interval.ms", std::to_string(*config.heartbeat_interval_ms));
        return *this;
    }

    // 应用 Network 配置
    KafkaConfBuilder& ApplyNetworkConfig(const KafkaNetworkConfig& config) {
        if (config.socket_timeout_ms) Set("socket.timeout.ms", std::to_string(*config.socket_timeout_ms));
        if (config.reconnect_backoff_ms) Set("reconnect.backoff.ms", std::to_string(*config.reconnect_backoff_ms));
        if (config.reconnect_backoff_max_ms) Set("reconnect.backoff.max.ms", std::to_string(*config.reconnect_backoff_max_ms));
        return *this;
    }

    // 应用完整配置（用于 Producer）
    KafkaConfBuilder& ApplyForProducer(const KafkaConfig& config) {
        Set("bootstrap.servers", config.brokers);
        Set("client.id", config.client_id);
        ApplyProducerConfig(config.producer);
        ApplyNetworkConfig(config.network);
        return *this;
    }

    // 应用完整配置（用于 Consumer）
    KafkaConfBuilder& ApplyForConsumer(const KafkaConfig& config) {
        Set("bootstrap.servers", config.brokers);
        Set("client.id", config.client_id);
        ApplyConsumerConfig(config.consumer);
        ApplyNetworkConfig(config.network);
        return *this;
    }

    // 构建
    std::unique_ptr<RdKafka::Conf> Build() {
        if (!errors_.empty()) {
            std::ostringstream oss;
            oss << "KafkaConfBuilder errors:\n";
            for (const auto& err : errors_) {
                oss << "  - " << err << "\n";
            }
            throw std::runtime_error(oss.str());
        }
        return std::move(conf_);
    }

    // 检查是否有错误
    bool HasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& GetErrors() const { return errors_; }

    //explicit：只允许在条件判断中使用（if/while/!/&&/||），禁止意外的隐式转换到其他类型。
    explicit operator bool() const{
        return conf_!=nullptr;
    }

private: 
    std::unique_ptr<RdKafka::Conf> conf_;
    std::vector<std::string> errors_;
};

}