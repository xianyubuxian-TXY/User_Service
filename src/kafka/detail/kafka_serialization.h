#pragma once
#include "kafka/kafka_types.h"

#include "json/json.hpp"
namespace user_service{

    using json=nlohmann::json;

    NLOHMANN_JSON_SERIALIZE_ENUM(UserEventType,{
        {UserEventType::CREATED,"CREATE"},
        {UserEventType::UPDATED,"UPDATED"},
        {UserEventType::DELETED,"DELETED"},
        {UserEventType::LOGIN,"LOGIN"},
        {UserEventType::LOGOUT,"LOGOUT"},
        {UserEventType::UNKNOW,"UNKNOW"}
    })
    
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserEvent,type,user_id,
                                        username,timestamp,extra_data)

}
