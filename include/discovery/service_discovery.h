#pragma once

#include "zk_client.h"
#include "service_instance.h"
#include <memory>
#include <vector>
#include <map>
#include <shared_mutex>
#include <functional>
#include <random>

namespace user_service {

/**
 * @brief 服务发现器（gRPC 客户端使用）
 * 
 * 功能：
 * - 从 ZooKeeper 获取服务实例列表
 * - 订阅服务变化（实例上线/下线）
 * - 本地缓存 + 自动更新
 * - 负载均衡选择实例
 * 
 * 使用示例：
 * @code
 * auto discovery = std::make_shared<ServiceDiscovery>(zk_client);
 * 
 * // 订阅服务变化
 * discovery->Subscribe("user-service", [](const std::string& service) {
 *     LOG_INFO("Service {} instances changed", service);
 * });
 * 
 * // 获取一个实例（负载均衡）
 * auto instance = discovery->SelectInstance("user-service");
 * if (instance) {
 *     auto channel = grpc::CreateChannel(instance->GetAddress(), ...);
 * }
 * @endcode
 */
class ServiceDiscovery {
public:
    // 服务变化回调
    using ServiceChangeCallback = std::function<void(const std::string& service_name)>;
    
    /**
     * @brief 构造函数
     * @param zk_client ZooKeeper 客户端（需要已连接）
     * @param root_path 服务根路径，默认 "/services"
     */
    explicit ServiceDiscovery(std::shared_ptr<ZooKeeperClient> zk_client,
                             const std::string& root_path = "/services");
    
    ~ServiceDiscovery();
    
    // 禁止拷贝
    ServiceDiscovery(const ServiceDiscovery&) = delete;
    ServiceDiscovery& operator=(const ServiceDiscovery&) = delete;
    
    /**
     * @brief 订阅服务变化（设置watch）
     * @param service_name 服务名称
     * @param callback 变化回调（可选）
     * 
     * @note 会立即拉取一次实例列表
     * @note 之后实例变化时自动更新本地缓存并触发回调
     */
    void Subscribe(const std::string& service_name, 
                   ServiceChangeCallback callback = nullptr);
    
    /**
     * @brief 取消订阅（取消watch）
     * @param service_name 服务名称
     */
    void Unsubscribe(const std::string& service_name);
    
    /**
     * @brief 获取服务的所有实例（业务一个服务在多个ip，或多个port上运行）
     * @param service_name 服务名称
     * @return 实例列表（可能为空）
     */
    std::vector<ServiceInstance> GetInstances(const std::string& service_name);
    
    /**
     * @brief 选择一个实例（随机负载均衡，自己实现）
     * @param service_name 服务名称
     * @return 实例指针，无可用实例返回 nullptr
     */
    std::shared_ptr<ServiceInstance> SelectInstance(const std::string& service_name);
    
    /**
     * @brief 选择一个实例（加权随机，自己实现）
     * @param service_name 服务名称
     * @return 实例指针，无可用实例返回 nullptr
     */
    std::shared_ptr<ServiceInstance> SelectInstanceWeighted(const std::string& service_name);
    
private:
    /**
     * @brief 刷新指定服务的实例列表
     */
    void RefreshInstances(const std::string& service_name);
    
    /**
     * @brief 处理子节点变化（Watch 回调）
     */
    void OnChildrenChanged(const std::string& path);
    
    /**
     * @brief 从路径提取服务名
     * /services/user-service -> user-service
     */
    std::string ExtractServiceName(const std::string& path) const;
    
    /**
     * @brief 构建服务路径
     */
    std::string BuildServicePath(const std::string& service_name) const;
    

    std::shared_ptr<ZooKeeperClient> zk_client_;    // zookeeper客户端句柄
    std::string root_path_;                         // 服务根路径，默认 "/services"
    
    // 服务实例缓存：service_name -> instances
    mutable std::shared_mutex cache_mutex_;
    std::map<std::string, std::vector<ServiceInstance>> instance_cache_;
    
    // 服务变化回调：service_name -> callback
    mutable std::mutex callback_mutex_;
    std::map<std::string, ServiceChangeCallback> callbacks_;
    
    // 随机数生成器（负载均衡用）
    mutable std::mt19937 rng_{std::random_device{}()};
};

} // namespace user_service
