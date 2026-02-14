// src/discovery/zk_client.cpp
#include "zk_client.h"
#include "common/logger.h"
#include <sstream>
#include <cstring>
#include <zookeeper/zookeeper.h>

namespace user_service{

// ============================================================================
// 构造与析构
// ============================================================================

ZooKeeperClient::ZooKeeperClient(const std::string& hosts, int session_timeout_ms)
    : hosts_(hosts)
    , session_timeout_ms_(session_timeout_ms)
    , closing_(false) {
    
    // 设置 ZK 日志级别（可选）
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
}

ZooKeeperClient::~ZooKeeperClient() {
    Close();
}

// ============================================================================
// 通用接口（gRPC 服务端 + gRPC 客户端 都使用）
// ============================================================================

bool ZooKeeperClient::Connect(int timeout_ms){
    std::unique_lock<std::mutex> lock(conn_mutex_);

    // 已连接
    if(zh_ && connected_.load()){
        return true;
    }

    // 关闭旧连接
    if(zh_){
        zookeeper_close(zh_);
        zh_=nullptr;
    }

    connected_=false;
    closing_ = false;

    // 初始化 ZK 句柄
    zh_=zookeeper_init(hosts_.c_str(),GlobalWatcher,session_timeout_ms_,nullptr,this,0);

    if (!zh_) {
        LOG_ERROR("zookeeper_init failed, hosts={}", hosts_);
        return false;
    }

    // 等待连接建立（使用条件变量）——> 连接建立成功时，会在”回调函数“中将connected_设置为true
    bool success = conn_cv_.wait_for(lock,
                std::chrono::milliseconds(timeout_ms),
                [this]{return connected_.load();});

    if(!success){
        LOG_ERROR("ZooKeeper connection timeout, hosts={}", hosts_);
        zookeeper_close(zh_);
        zh_ = nullptr;
        return false; 
    }

    LOG_INFO("ZooKeeper client connected, hosts={}", hosts_);
    return true;
}

void ZooKeeperClient::Close() {
    // 1. 标记正在关闭
    closing_.store(true);
    
    // 2. 先清空回调映射，阻止新回调执行
    {
        std::lock_guard<std::mutex> lock(watch_mutex_);
        watches_.clear();
    }
    
    // 3. 等待一小段时间，让正在执行的回调完成
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 4. 关闭 ZK 连接
    if (zh_) {
        zookeeper_close(zh_);
        zh_ = nullptr;
        connected_ = false;
        LOG_INFO("ZooKeeper connection closed");
    }
}

bool ZooKeeperClient::IsConnected() const {
    return zh_ && connected_.load() && !closing_.load() &&
           zoo_state(zh_) == ZOO_CONNECTED_STATE;
}

// ============================================================================
// gRPC 服务端使用的接口（服务注册）
// ============================================================================

// 创建节点
bool ZooKeeperClient::CreateNode(const std::string& path, 
                                const std::string& data,
                                bool ephemeral){
    if(!IsConnected()){
        LOG_ERROR("ZK not connected, cannot create node: {}", path);
        return false;
    }

    // 节点类型（临时节点/顺序节点/临时顺序节点）
    int flags=ephemeral ? ZOO_EPHEMERAL:0;

    // 创建节点
    int rc=zoo_create(zh_,path.c_str(),data.c_str(),data.size(),
                        &ZOO_OPEN_ACL_UNSAFE,flags,nullptr,0);
    
    if(rc==ZOK){    // 节点创建成功
        LOG_DEBUG("ZK node created: {} (ephemeral={})", path, ephemeral);
        return true;
    }else if(rc==ZNODEEXISTS){
        // 节点已存在，对于持久节点视为成功
        // 对于临时节点，可能是上次会话残留，需要特殊处理
        if(ephemeral){
            LOG_WARN("ZK ephemeral node already exists: {}, attempting recovery...", path);
            
            // 尝试删除旧节点后重新创建
            if(DeleteNode(path)){
                rc=zoo_create(zh_,path.c_str(),data.c_str(),data.size(),
                              &ZOO_OPEN_ACL_UNSAFE,flags,nullptr,0);
                if(rc==ZOK){
                    LOG_INFO("ZK ephemeral node recreated: {}", path);
                    return true;
                }
            }
            
            // 如果删除重建失败，尝试直接更新数据
            if(SetData(path,data)){
                LOG_INFO("ZK ephemeral node data updated: {}", path);
                return true;
            }
            
            LOG_ERROR("Failed to recover ephemeral node: {}", path);
            return false;
        }
        return true;
    }else{
        LOG_ERROR("ZK create failed: path={}, error={}", path, zerror(rc));
        return false;
    }
}

bool ZooKeeperClient::CreateServicePath(const std::string& path){
    if(!IsConnected()){
        return false;
    }

    // 使用stringstream 分割路径
    std::string current;
    std::istringstream ss(path);
    std::string token;

    /*
    输入：/services/user
    循环过程大概是：
        - token=services → current=/services → 不存在就创建
        - token=user → current=/services/user → 不存在就创建
    最终路径保证存在。
    */
    while(std::getline(ss,token,'/')){  
        if(token.empty()) continue;

        current += "/" + token;

        if(!Exists(current)){   // 节点不存在
            // 创建持久节点(节点值为空)
            if(!CreateNode(current,"",false)){
                return false;
            }
        }
    }

    return true;
}

// 删除节点
bool ZooKeeperClient::DeleteNode(const std::string& path){
    if(!IsConnected()){
        return false;
    }

    int rc = zoo_delete(zh_,path.c_str(),-1);    // -1 表示忽略版本

    if (rc == ZOK) {
        LOG_DEBUG("ZK node deleted: {}", path);
        return true;
    } else if (rc == ZNONODE) {
        // 节点不存在，视为删除成功
        return true;
    } else {
        LOG_ERROR("ZK delete failed: path={}, error={}", path, zerror(rc));
        return false;
    }
}

// 判断节点是否存在
bool ZooKeeperClient::Exists(const std::string& path) {
    if(!IsConnected()){
        return false;
    }

    struct Stat stat;
    int rc=zoo_exists(zh_,path.c_str(),0,&stat);
    return rc == ZOK;
}

// 设置节点数据
bool ZooKeeperClient::SetData(const std::string& path, const std::string& data) {
    if(!IsConnected()){
        return false;
    }

    int rc = zoo_set(zh_, path.c_str(), data.c_str(), data.size(), -1);

    if (rc == ZOK) {
        LOG_DEBUG("ZK set data: path={}, size={}", path, data.size());
        return true;
    } else {
        LOG_ERROR("ZK set data failed: path={}, error={}", path, zerror(rc));
        return false;
    }
}

// ============================================================================
// gRPC 客户端使用的接口（服务发现）
// ============================================================================

// 获取节点数据
std::string ZooKeeperClient::GetData(const std::string& path){
    if(!IsConnected()){
        return "";
    }

    // 使用 vector 在堆上分配内存，避免栈溢出风险
    constexpr int kMaxDataSize = 65536;  // 64KB
    std::vector<char> buffer(kMaxDataSize);
    int buffer_len = static_cast<int>(buffer.size());
    struct Stat stat;

    // 获取指定实例节点的数据，如"ip:port"
    int rc = zoo_get(zh_, path.c_str(), 0, buffer.data(), &buffer_len, &stat);

    if (rc == ZOK && buffer_len > 0) {
        return std::string(buffer.data(), buffer_len);
    } else if (rc != ZOK) {
        LOG_WARN("ZK get data failed: path={}, error={}", path, zerror(rc));
    }

    return "";
}

// 获取指定目录下所有子节点
std::vector<std::string> ZooKeeperClient::GetChildren(const std::string& path) {
    std::vector<std::string> result;
    
    if (!IsConnected()) {
        return result;
    }
    
    struct String_vector children;
    int rc = zoo_get_children(zh_, path.c_str(), 0, &children);
    
    if (rc == ZOK) {
        result.reserve(children.count);
        for (int i = 0; i < children.count; ++i) {
            result.emplace_back(children.data[i]);
        }
        deallocate_String_vector(&children);
    } else {
        LOG_WARN("ZK get children failed: path={}, error={}", path, zerror(rc));
    }
    
    return result;
}

// 监听子节点
void ZooKeeperClient::WatchChildren(const std::string& path, WatchCallback callback){
    if (!IsConnected() || closing_.load()) {
        LOG_ERROR("ZK not connected or closing, cannot watch: {}", path);
        return;
    }

    // 保存回调
    {
        std::lock_guard<std::mutex> lock(watch_mutex_);
        /* 存储客户端传入的回调函数
           - 每次触发watch后，都需要重新设置，也就需要再次使用“客户端传入的回调函数”
           - 故：需要存储 path ——> callback 的map映射，方便重复使用 
        */
        watches_[path] = std::move(callback);
    }

    // 设置 watch 并获取当前子节点
    struct String_vector children;
    int rc=zoo_wget_children(zh_, path.c_str(), GlobalWatcher, this, &children);

    if (rc == ZOK) {
        // ★ 释放 String_vector 内存（必须调用！）
        deallocate_String_vector(&children);
        LOG_DEBUG("ZK watch set: {}", path);
    } else {
        LOG_ERROR("ZK watch failed: path={}, error={}", path, zerror(rc));
    }
}

// 取消监听子节点
void ZooKeeperClient::UnwatchChildren(const std::string& path) {
    std::lock_guard<std::mutex> lock(watch_mutex_);
    /* watch是一次性的，触发后需要重新注册，即从watches_中通过path取出callback进行注册
        - 只要将对应的callback从watches_中移除，之后path对应的节点也就无法再注册watch
          也就达到了“取消监听子节点”的目的*/
    watches_.erase(path);
    LOG_DEBUG("ZK watch removed: {}", path);
}

// ============================================================================
// 内部实现（静态 Watcher 回调 + 事件处理）
// ============================================================================

// 全局 Watcher 回调（静态，供 C API 调用）
void ZooKeeperClient::GlobalWatcher(zhandle_t* zh, int type, int state,
                    const char* path, void* context){
    
    ZooKeeperClient* client = static_cast<ZooKeeperClient*>(context);

    // 安全检查
    if (!client) {
        return;
    }
    
    // 检查是否正在关闭
    if (client->closing_.load()) {
        return;
    }
    if(type==ZOO_SESSION_EVENT){
        // 会话事件：连接、断开、过期等
        client->HandleSessionEvent(state);
    }else if(type==ZOO_CHILD_EVENT && path){
        // 子节点变化事件
        client->HandleChildEvent(path);
    }
}

// 处理会话状态变化
void ZooKeeperClient::HandleSessionEvent(int state) {
    if (closing_.load()) return;

    if (state == ZOO_CONNECTED_STATE) { 
        // 连接成功
        connected_ = true;
        LOG_INFO("ZooKeeper connected");
        conn_cv_.notify_all();  // 通知等待连接的线程
    } else if (state == ZOO_EXPIRED_SESSION_STATE) {
        // 会话过期
        connected_ = false;
        LOG_WARN("ZooKeeper session expired");
    } else if (state == ZOO_CONNECTING_STATE) {
        // 正在连接
        connected_ = false;
        LOG_INFO("ZooKeeper reconnecting...");
    } else if (state == ZOO_ASSOCIATING_STATE) {
        // 关联中
        LOG_DEBUG("ZooKeeper associating...");
    } else {
        LOG_WARN("ZooKeeper unknown state: {}", state);
    }
}

// 处理子节点列表变更
void ZooKeeperClient::HandleChildEvent(const std::string& path) {
    // 检查是否正在关闭
    if (closing_.load()) {
        return;
    }

    WatchCallback callback;
    
    {
        std::lock_guard<std::mutex> lock(watch_mutex_);

        // 双重检查
        if (closing_.load()) {
            return;
        }

        auto it = watches_.find(path);
        if (it == watches_.end()) {
            return;
        }
        callback = it->second;  // 复制回调
    }

    // 再次检查
    if (!callback || closing_.load()) {
        return;
    }

    // 执行回调
    try {
        callback(path);
    } catch (const std::exception& e) {
        LOG_ERROR("Watch callback exception: {}", e.what());
    } catch (...) {
        LOG_ERROR("Watch callback unknown exception");
    }

    // 重新注册前再次检查
    if (!closing_.load()) {
        ResetWatch(path);
    }
}

// 重新注册指定路径的 watch
void ZooKeeperClient::ResetWatch(const std::string& path){
    if (closing_.load()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(watch_mutex_);
        // 检查是否需要再监听
        if(watches_.find(path) == watches_.end()){
            return;
        }
    }

    if (!IsConnected()) {
        return;
    }

    struct String_vector children;

    int rc=zoo_wget_children(zh_, path.c_str(), GlobalWatcher, this, &children);

    if(rc==ZOK){
        deallocate_String_vector(&children);
    }else{
        LOG_WARN("Failed to reset watch for {}: {}", path, zerror(rc));
    }
}

}   // namespace user_service
