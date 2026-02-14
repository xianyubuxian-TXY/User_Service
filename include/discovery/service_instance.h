// src/discovery/service_instance.h
#pragma once

#include <string>
#include <map>
#include "json/json.hpp"

namespace user_service {

/**
 * @brief 服务实例信息
 * 
 * @note services/user-service/192.168.1.10:50051
 *       - serivces：root_path
 *       - services/user-service：服务路径（永久节点） 
 *           - user-service：服务名
 *       - services/user-service/192.168.1.10:50051：实例节点（临时节点）
 *           - 192.168.1.10:50051：实例节点数据（存储用json格式）
 */
struct ServiceInstance {
    std::string service_name;   // 服务名，如 "user-service"
    std::string instance_id;    // 实例ID，如 "192.168.1.10:50051"
    std::string host;           // 主机地址
    int port;                   // 端口号
    int weight = 100;           // 权重（负载均衡用）
    std::map<std::string, std::string> metadata;  // 元数据
    
    /**
     * @brief 获取完整地址
     */
    std::string GetAddress() const {
        return host + ":" + std::to_string(port);
    }
    
    /**
     * @brief 序列化为 JSON
     */
    std::string ToJson() const {
        nlohmann::json j;
        j["service_name"] = service_name;
        j["instance_id"] = instance_id;
        j["host"] = host;
        j["port"] = port;
        j["weight"] = weight;
        j["metadata"] = metadata;
        return j.dump();
    }
    
    /**
     * @brief 从 JSON 反序列化
     */
    static ServiceInstance FromJson(const std::string& json_str) {
        ServiceInstance instance;
        try {
            auto j = nlohmann::json::parse(json_str);
            instance.service_name = j.value("service_name", "");
            instance.instance_id = j.value("instance_id", "");
            instance.host = j.value("host", "");
            instance.port = j.value("port", 0);
            instance.weight = j.value("weight", 100);
            if (j.contains("metadata")) {
                instance.metadata = j["metadata"].get<std::map<std::string, std::string>>();
            }
        } catch (...) {
            // 解析失败，返回空实例
        }
        return instance;
    }
    
    bool IsValid() const {
        return !host.empty() && port > 0;
    }
};

} // namespace user_service
