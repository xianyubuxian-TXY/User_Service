// kafka/kafka_producer.cpp
#include "kafka/kafka_producer.h"

#include "detail/detail.h"
#include "common/logger.h"
#include "json/json.hpp"

namespace user_service {

namespace {
    DeliveryReportCb dr_cb;
}

KafkaResult<> KafkaProducer::init(const KafkaConfig& config){
    kafkaConf_ = std::make_unique<KafkaConfig>(config);
    
    try {
        conf_ = KafkaConfBuilder()
            .ApplyForProducer(config)
            .SetCallBack(config.user_events, &dr_cb)
            .Build();

        std::string errstr;
        producer_.reset(RdKafka::Producer::create(conf_.get(), errstr));
        
        if (!producer_) {
            LOG_ERROR("[Kafka] Create producer failed: %s", errstr.c_str());
            return std::unexpected(KafkaError::BrokerUnavailable);
        }
        
        LOG_INFO("[Kafka] Producer initialized, brokers=%s", config.brokers.c_str());
        return {};

    } catch (const std::exception& e) {
        LOG_ERROR("[Kafka] Init exception: %s", e.what());
        return std::unexpected(KafkaError::ConfigError);
    }
}

KafkaProducer::~KafkaProducer() {
    if (producer_) {
        LOG_INFO("[Kafka] Flushing producer...");
        Flush(5000);
        LOG_INFO("[Kafka] Producer destroyed");
    }
}

KafkaResult<> KafkaProducer::Send(const std::string& topic, 
                                   const std::string& key, 
                                   const std::string& value) {
    if (!producer_) {
        LOG_ERROR("[Kafka] Producer not initialized");
        return std::unexpected(KafkaError::NotInitialized);
    }
    
    RdKafka::ErrorCode err = producer_->produce(
        topic,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        (void*)value.data(), value.size(),
        key.data(), key.size(),
        0, nullptr 
    );

    if (err == RdKafka::ERR__QUEUE_FULL) {
        perfm_.queue_full_count_++;
        LOG_WARN("[Kafka] Queue full, topic=%s, key=%s, queue_len=%zu", 
                 topic.c_str(), key.c_str(), producer_->outq_len());
        return std::unexpected(KafkaError::QueueFull);
    }
    
    if (err == RdKafka::ERR_MSG_SIZE_TOO_LARGE) {
        perfm_.failed_count_++;
        LOG_ERROR("[Kafka] Message too large, topic=%s, size=%zu", 
                  topic.c_str(), value.size());
        return std::unexpected(KafkaError::MessageTooLarge);
    }

    if (err != RdKafka::ERR_NO_ERROR) {
        perfm_.failed_count_++;
        LOG_ERROR("[Kafka] Send failed, topic=%s, err=%s", 
                  topic.c_str(), RdKafka::err2str(err).c_str());
        return std::unexpected(KafkaError::Unknown);
    }

    producer_->poll(0);
    perfm_.total_sent_++;
    
    return {};
}

KafkaResult<> KafkaProducer::SendUserEvent(const std::string& topic, 
                                            const UserEvent& event) {
    // 序列化
    std::string value;
    try {
        value = json(event).dump();
    } catch (const std::exception& e) {
        LOG_ERROR("[Kafka] JSON serialization failed: %s, event_type=%s, user_id=%s",
                  e.what(), UserEventTypeToString(event.type), event.user_id);
        return std::unexpected(KafkaError::SerializationFailed);
    }

    return Send(topic, event.user_id, value);
}

KafkaResult<size_t> KafkaProducer::Flush(int timeout_ms) {
    if (!producer_) {
        LOG_ERROR("[Kafka] Flush: producer not initialized");
        return std::unexpected(KafkaError::NotInitialized);
    }
    
    RdKafka::ErrorCode err = producer_->flush(timeout_ms);
    
    if (err == RdKafka::ERR__TIMED_OUT) {
        size_t remaining = producer_->outq_len();
        LOG_WARN("[Kafka] Flush timeout, remaining=%zu", remaining);
        return std::unexpected(KafkaError::Timeout);
    }
    
    return producer_->outq_len();
}

std::unordered_map<std::string, uint64_t> KafkaProducer::GetMetrics() const {
    return {
        {"kafka_producer_total_sent", perfm_.total_sent_.load()},
        {"kafka_producer_queue_full", perfm_.queue_full_count_.load()},
        {"kafka_producer_retry", perfm_.retry_count_.load()},
        {"kafka_producer_failed", perfm_.failed_count_.load()},
    };
}

}  // namespace user_service
