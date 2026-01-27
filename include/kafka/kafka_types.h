#pragma once
#include <string>
#include <optional>
#include <expected>
namespace user_service{

//用户事件类型
enum class UserEventType {
    CREATED,    // 用户创建事件（注册）
    UPDATED,    // 用户信息更新事件（如修改昵称、手机号）
    DELETED,    // 用户注销/删除事件
    LOGIN,      // 用户登录事件
    LOGOUT,      // 用户登出事件
    UNKNOW
};

//用户事件
struct UserEvent{
    UserEventType type;          // 事件类型（如登录、注册）
    std::string user_id;         // 用户唯一标识（主键），用于分区/检索
    std::string username;        // 用户名（冗余字段，便于消费端快速解析）
    std::string timestamp;       // 事件发生时间戳（格式建议：ISO 8601，如 "2026-01-15T10:00:00+08:00"）
    std::optional<std::string> extra_data;      // 额外数据（JSON格式），存储非核心的动态数据，如：
                                 // - CREATED事件：注册IP、设备信息
                                 // - LOGIN事件：登录IP、登录设备类型 
};

inline const char* UserEventTypeToString(UserEventType type){
    switch(type){
        case UserEventType::CREATED:
            return "CREATED";
        case UserEventType::UPDATED:
            return "UPDATE";
        case UserEventType::DELETED:
            return "DELETED";
        case UserEventType::LOGIN:
            return "LOGIN";
        case UserEventType::LOGOUT:
            return "LOGOUT";
        default:
            return "UNKNOW";
    }
}

}