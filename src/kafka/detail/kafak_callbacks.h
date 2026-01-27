#pragma once
#include <librdkafka/rdkafkacpp.h>
#include "common/logger.h"
namespace user_service{

class DeliveryReportCb: public RdKafka::DeliveryReportCb{
    public:
        void dr_cb(RdKafka::Message &message) override{
            if(message.err()){
                LOG_ERROR("Kafka delivery failed: topic={},error={}",
                            message.topic_name(),message.errstr());
            }else{
                LOG_INFO("Kafka delivery success: topic={},partition={},offset={}",
                            message.topic_name(),message.partition(),message.offset());
            }
        }
    };


}