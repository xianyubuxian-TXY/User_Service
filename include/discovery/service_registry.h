#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include "zk_client.h"
#include "service_instance.h"

namespace user_service {

/**
 * @brief 服务注册器（gRPC 服务端使用）——> 一个进程 = 一个服务 = 一个服务注册器
 * 
 * 功能：
 * - 将服务实例注册到 ZooKeeper
 * - 自动心跳保活（通过临时节点实现）
 * - 优雅下线
 * 
 * 使用示例：
 * @code
 * auto registry = std::make_shared<ServiceRegistry>(zk_client);
 * ServiceInstance instance;
 * instance.service_name = "user-service";
 * instance.host = "192.168.1.10";
 * instance.port = 50051;
 * registry->Register(instance);
 * // ... 服务运行 ...
 * registry->Unregister();
 * @endcode
 */
class ServiceRegistry {
public:
    /**
     * @brief 构造函数
     * @param zk_client ZooKeeper 客户端（需要已连接）
     * @param root_path 服务根路径，默认 "/services"
     */
    explicit ServiceRegistry(std::shared_ptr<ZooKeeperClient> zk_client,
                            const std::string& root_path = "/services");
    
    ~ServiceRegistry();
    
    // 禁止拷贝
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;
    
    /**
     * @brief 注册服务实例
     * @param instance 服务实例信息
     * @return 注册成功返回 true
     * 
     * @note 会自动创建父路径
     * @note 创建临时节点，服务下线时自动删除
     */
    bool Register(const ServiceInstance& instance);
    
    /**
     * @brief 注销服务实例
     * @return 注销成功返回 true
     */
    bool Unregister();
    
    /**
     * @brief 更新服务实例信息
     * @param instance 新的实例信息
     * @return 更新成功返回 true
     */
    bool Update(const ServiceInstance& instance);
    
    /**
     * @brief 是否已注册
     */
    bool IsRegistered() const { return registered_.load(); }
    
private:
    /**
     * @brief 构建实例节点路径
     * 格式：/services/{service_name}/{instance_id}
     */
    std::string BuildInstancePath(const ServiceInstance& instance) const;
    
    /**
     * @brief 构建服务路径
     * 格式：/services/{service_name}
     */
    std::string BuildServicePath(const std::string& service_name) const;
    
private:
    

    std::shared_ptr<ZooKeeperClient> zk_client_;    // ZooKeeper 客户端（需要已连接）
    std::string root_path_;                         // 服务根路径，默认 "/services"
    
    // 当前注册的实例信息（微服务架构中：一个进程 = 一个服务实例）
    ServiceInstance current_instance_;
    std::string current_path_;
    std::atomic<bool> registered_{false};
    
    mutable std::mutex mutex_;
};

} // namespace user_service
