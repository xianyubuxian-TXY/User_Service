#include "service_registry.h"
#include "common/logger.h"

namespace user_service{


ServiceRegistry::ServiceRegistry(std::shared_ptr<ZooKeeperClient> zk_client,
    const std::string& root_path)
    : zk_client_(std::move(zk_client))
    , root_path_(root_path) {
}


ServiceRegistry::~ServiceRegistry() {
    Unregister();
}

bool ServiceRegistry::Register(const ServiceInstance& instance){
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查zookeeper客户端句柄的有效性
    if(!zk_client_ || !zk_client_->IsConnected()){
        LOG_ERROR("ZK client not connected, cannot register service");
        return false;
    }

    // 检查 服务示例信息 的有效性
    if(!instance.IsValid()){
        LOG_ERROR("Invalid service instance: host={}, port={}", 
            instance.host, instance.port);
            return false;
    }

    // 1.确保服务路径存在（持久节点）
    std::string service_path=BuildServicePath(instance.service_name);
    if(!zk_client_->CreateServicePath(service_path)){
        LOG_ERROR("Failed to create service path: {}", service_path);
        return false;
    }

    // 2.创建实例节点（临时节点）
    std::string instance_path = BuildInstancePath(instance);
    std::string data=instance.ToJson();

    if(!zk_client_->CreateNode(instance_path,data,true)){
        LOG_ERROR("Failed to create instance node: {}", instance_path);
        return false;
    }

    // 3.保存状态（方便之后进行修改、取消注册）
    current_instance_ =instance;
    current_path_=instance_path;
    registered_=true;

    LOG_INFO("Service registered: {} at {}", 
        instance.service_name, instance.GetAddress());

    return true;
}

bool ServiceRegistry::Unregister(){
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否注册
    if(!registered_.load()){
        return true;  // 未注册，视为成功
    }

    // 句柄失效，则服务实例也就失效了（因为是临时节点）
    if(!zk_client_){
        registered_ = false;
        return true;
    }

    // 删除实例节点
    bool success=zk_client_->DeleteNode(current_path_);

    if (success) {
        LOG_INFO("Service unregistered: {} at {}", 
                 current_instance_.service_name, 
                 current_instance_.GetAddress());
    } else {
        LOG_WARN("Failed to unregister service, node may already be deleted");
    }

    // 更新状态
    registered_=false;
    current_path_.clear();

    return true;
}



bool ServiceRegistry::Update(const ServiceInstance& instance){
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查是否注册
    if(!registered_.load()){
        LOG_ERROR("Service not registered, cannot update");
        return false;
    }

    // 检查zookeeper客户端句柄的有效性
    if (!zk_client_ || !zk_client_->IsConnected()) {
        LOG_ERROR("ZK client not connected, cannot update");
        return false;
    }

    // 序列化节点数据
    std::string data = instance.ToJson();

    if (!zk_client_->SetData(current_path_, data)) {
        LOG_ERROR("Failed to update service instance: {}", current_path_);
        return false;
    }

    current_instance_ = instance;
    LOG_DEBUG("Service instance updated: {}", current_path_);
    
    return true;
}

std::string ServiceRegistry::BuildInstancePath(const ServiceInstance& instance) const {
    // 格式：/services/user-service/192.168.1.10:50051
    return root_path_ + "/" + instance.service_name + "/" + instance.GetAddress();
}

std::string ServiceRegistry::BuildServicePath(const std::string& service_name) const {
    // 格式：/services/user-service
    return root_path_ + "/" + service_name;
}

}