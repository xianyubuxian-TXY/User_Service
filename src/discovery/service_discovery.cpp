#include "service_discovery.h"
#include "common/logger.h"

namespace user_service{

ServiceDiscovery::ServiceDiscovery(std::shared_ptr<ZooKeeperClient> zk_client,
    const std::string& root_path)
    : zk_client_(std::move(zk_client))
    , root_path_(root_path) {
}

ServiceDiscovery::~ServiceDiscovery() {
    // 取消所有订阅
    std::vector<std::string> services;
    {
        std::shared_lock<std::shared_mutex> lock(cache_mutex_);
        for (const auto& [name, _] : instance_cache_) {
            services.push_back(name);
        }
    }
    
    for (const auto& service : services) {
        Unsubscribe(service);
    }
}

void ServiceDiscovery::Subscribe(const std::string& service_name,
                                 ServiceChangeCallback callback) {
    
    // 检查zookeeper客户端句柄的有效性
    if (!zk_client_ || !zk_client_->IsConnected()) {
        LOG_ERROR("ZK client not connected, cannot subscribe: {}", service_name);
        return;
    }
    
    // 保存回调
    if (callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callbacks_[service_name] = std::move(callback);
    }
    
    // 立即刷新一次实例列表
    RefreshInstances(service_name);
    
    // 设置 Watch
    std::string service_path = BuildServicePath(service_name);
    zk_client_->WatchChildren(service_path, 
        // 子节点改变是的“回调函数”
        [this](const std::string& path) {
            OnChildrenChanged(path);
        });
    
    LOG_INFO("Subscribed to service: {}", service_name);
}

void ServiceDiscovery::Unsubscribe(const std::string& service_name) {
    
    // 取消 Watch
    std::string service_path = BuildServicePath(service_name);
    if (zk_client_) {
        zk_client_->UnwatchChildren(service_path);
    }
    
    // 清理回调
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callbacks_.erase(service_name);
    }
    
    // 清理缓存
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        instance_cache_.erase(service_name);
    }
    
    LOG_INFO("Unsubscribed from service: {}", service_name);
}

// 获取服务的所有实例
std::vector<ServiceInstance> ServiceDiscovery::GetInstances(const std::string& service_name) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    auto it = instance_cache_.find(service_name);
    if (it != instance_cache_.end()) {
        return it->second;
    }
    
    return {};
}

// 选择一个实例（随机负载均衡，自己实现）
std::shared_ptr<ServiceInstance> ServiceDiscovery::SelectInstance(const std::string& service_name) {
    // 获取实例列表
    auto instances = GetInstances(service_name);
    
    if (instances.empty()) {
        LOG_WARN("No available instance for service: {}", service_name);
        return nullptr;
    }
    
    // 随机选择一个实例
    std::uniform_int_distribution<size_t> dist(0, instances.size() - 1);
    size_t index = dist(rng_);
    
    return std::make_shared<ServiceInstance>(instances[index]);
}

// 选择一个实例（加权随机，自己实现）
std::shared_ptr<ServiceInstance> ServiceDiscovery::SelectInstanceWeighted(
    const std::string& service_name) {
    
    // 获取实例列表
    auto instances = GetInstances(service_name);
    
    if (instances.empty()) {
        LOG_WARN("No available instance for service: {}", service_name);
        return nullptr;
    }
    
    // 计算总权重
    int total_weight = 0;
    for (const auto& inst : instances) {
        total_weight += inst.weight;
    }
    
    if (total_weight <= 0) {
        // 权重都为0，退化为随机选择
        return SelectInstance(service_name);
    }
    
    // 加权随机
    std::uniform_int_distribution<int> dist(1, total_weight);
    int random_weight = dist(rng_);
    
    int current_weight = 0;
    for (const auto& inst : instances) {
        current_weight += inst.weight;
        if (random_weight <= current_weight) {
            return std::make_shared<ServiceInstance>(inst);
        }
    }
    
    // 理论上不会到这里
    return std::make_shared<ServiceInstance>(instances.back());
}

// 刷新指定服务的实例列表
void ServiceDiscovery::RefreshInstances(const std::string& service_name) {
    if (!zk_client_ || !zk_client_->IsConnected()) {
        return;
    }
    
    // 构造 服务路径名
    std::string service_path = BuildServicePath(service_name);
    
    // 获取子节点列表（实例ID列表，ip:port）
    auto children = zk_client_->GetChildren(service_path);
    
    std::vector<ServiceInstance> instances;
    instances.reserve(children.size());
    
    // 获取每个实例的详细信息
    for (const auto& child : children) {
        // 通过实例节点路径，获取实例节点数据（json存储）
        std::string instance_path = service_path + "/" + child;
        std::string data = zk_client_->GetData(instance_path);
        
        if (!data.empty()) {
            // 反序列化出“实例节点数据”
            ServiceInstance instance = ServiceInstance::FromJson(data);
            if (instance.IsValid()) {
                instances.push_back(std::move(instance));
            } else {
                LOG_WARN("Invalid instance data at: {}", instance_path);
            }
        }
    }
    
    // 更新缓存
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex_);
        instance_cache_[service_name] = std::move(instances);
    }
    
    LOG_DEBUG("Refreshed service {}: {} instances", 
              service_name, instance_cache_[service_name].size());
}

void ServiceDiscovery::OnChildrenChanged(const std::string& path) {
    
    // 获取服务名
    std::string service_name = ExtractServiceName(path);
    
    if (service_name.empty()) {
        LOG_WARN("Cannot extract service name from path: {}", path);
        return;
    }
    
    LOG_INFO("Service {} instances changed, refreshing...", service_name);
    
    // 刷新实例列表
    RefreshInstances(service_name);
    
    // 触发回调
    ServiceChangeCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        auto it = callbacks_.find(service_name);
        if (it != callbacks_.end()) {
            callback = it->second;
        }
    }
    
    if (callback) {
        callback(service_name);
    }
}

std::string ServiceDiscovery::ExtractServiceName(const std::string& path) const {
    // /services/user-service -> user-service
    if (path.length() <= root_path_.length() + 1) {
        return "";
    }
    
    return path.substr(root_path_.length() + 1);  // +1 跳过 '/'
}

std::string ServiceDiscovery::BuildServicePath(const std::string& service_name) const {
    return root_path_ + "/" + service_name;
}

}