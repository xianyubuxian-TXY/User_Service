// src/discovery/zk_client.h
#pragma once

#include <zookeeper/zookeeper.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace user_service {

/**
 * @brief ZooKeeper 客户端封装
 * 
 * 提供 ZooKeeper 基本操作的 C++ 封装，包括：
 * - 节点的 CRUD 操作
 * - 子节点监听（Watch）
 * - 自动重连处理
 * 
 * 使用示例：
 * @code
 * ZooKeeperClient zk("127.0.0.1:2181", 15000);
 * if (zk.Connect()) {
 *     zk.CreateNode("/test", "data", false);
 * }
 * @endcode
 */
class ZooKeeperClient {
public:
    // 子节点变化回调类型
    using WatchCallback = std::function<void(const std::string& path)>;
    
    /**
     * @brief 构造函数
     * @param hosts ZK 服务器地址，格式: "ip1:port1,ip2:port2"
     * @param session_timeout_ms 会话超时时间（毫秒），推荐 10000-30000
     */
    ZooKeeperClient(const std::string& hosts, int session_timeout_ms = 30000);
    
    /**
     * @brief 析构函数，自动关闭连接
     */
    ~ZooKeeperClient();
    
    // 禁止拷贝
    ZooKeeperClient(const ZooKeeperClient&) = delete;
    ZooKeeperClient& operator=(const ZooKeeperClient&) = delete;
    
    // ============================================================================
    // 通用接口（gRPC 服务端 + gRPC 客户端 都使用）
    // ============================================================================
    
    /**
     * @brief 连接到 ZooKeeper
     * @param timeout_ms 连接超时时间（毫秒）
     * @return 连接成功返回 true
     */
    virtual bool Connect(int timeout_ms = 10000);
    
    /**
     * @brief 关闭连接
     */
    virtual void Close();
    
    /**
     * @brief 是否已连接
     */
    virtual bool IsConnected() const;
    
    // ============================================================================
    // gRPC 服务端使用的接口（服务注册）
    // ============================================================================
    
    /**
     * @brief 创建单个节点（f服务路径<父节点>必须存在）
     * @param path 节点路径
     * @param data 节点数据
     * @param ephemeral 是否为临时节点（会话断开自动删除）
     * @return 创建成功或节点已存在返回 true
     * 
     * @note 服务端用于：注册服务实例（创建临时节点）
     */
    virtual bool CreateNode(const std::string& path, const std::string& data, 
                    bool ephemeral = false);
    
    /**
     * @brief 递归创建整个路径（自动创建所有父节点）
     * @param path 服务根路径
     * @return 创建成功返回 true
     * 
     * @note 服务端用于：创建服务根路径 /services/user-service ——> 永久性节点
     * @note 服务注册的典型用法：
        void RegisterService(const std::string& service_name, 
                            const std::string& host, int port) {
            
            // 1. 先用 CreatePath 确保服务路径存在（持久节点）
            std::string service_path = "/services/" + service_name;
            zk_client->CreatePath(service_path);  // 递归创建 /services/user-service
            
            // 2. 再用 CreateNode 创建实例节点（临时节点）
            std::string instance_path = service_path + "/" + host + ":" + std::to_string(port);
            std::string data = R"({"host":")" + host + R"(","port":)" + std::to_string(port) + "}";
            zk_client->CreateNode(instance_path, data, true);  // ephemeral=true
        }

        ┌─────────────────────────────────────────────────────────────────────────────┐
        │  /services/user-service/192.168.1.100:50051                                 │
        │  ├────────┬────────────┬─────────────────────┤                              │
        │  │  持久  │    持久    │        临时          │                              │
        │  └────────┴────────────┴─────────────────────┘                              │
        │                                                                              │
        │  • /services          → 持久：服务根目录，永远存在                            │
        │  • /user-service      → 持久：服务名，永远存在                                │
        │  • /192.168.1.100:50051 → 临时：实例节点，服务下线自动删除                     │
        │                                                                              │
        │  好处：服务全部下线后，/services/user-service 还在，                           │
        │       客户端可以继续 Watch，等待新实例上线                                     │
        └─────────────────────────────────────────────────────────────────────────────┘
     */
    virtual bool CreateServicePath(const std::string& path);
    
    /**
     * @brief 删除节点
     * @param path 节点路径
     * @return 删除成功或节点不存在返回 true
     * 
     * @note 服务端用于：主动注销服务实例
     */
    virtual bool DeleteNode(const std::string& path);
    
    /**
     * @brief 检查节点是否存在
     * 
     * @note 服务端用于：检查路径是否已创建
     */
    virtual bool Exists(const std::string& path);
    
    /**
     * @brief 设置节点数据，用于更新已有节点数据
     * @param path 节点路径
     * @param data 新数据
     * @return 设置成功返回 true
     * 
     * @note 服务端用于：更新服务实例的元信息（如权重、状态）
     */
    virtual bool SetData(const std::string& path, const std::string& data);
    
    // ============================================================================
    // gRPC 客户端使用的接口（服务发现）
    // ============================================================================
    
    /**
     * @brief 获取节点数据
     * @param path 节点路径
     * @return 节点数据，失败返回空字符串
     * 
     * @note 客户端用于：读取服务实例信息（host、port、metadata）
     */
    virtual std::string GetData(const std::string& path);
    
    /**
     * @brief 获取子节点列表
     * @param path 父节点路径
     * @return 子节点名称列表（不含路径前缀）
     * 
     * @note 客户端用于：获取所有服务实例节点名
     */
    virtual std::vector<std::string> GetChildren(const std::string& path);
    
    /**
     * @brief 监听子节点变化
     * @param path 监听路径
     * @param callback 变化回调
     * 
     * @note ZK 的 watch 是一次性的，本方法会自动重新注册
     * @note 回调在 ZK 事件线程中执行，避免长时间阻塞
     * @note 客户端用于：监听服务实例上下线
     */
    virtual void WatchChildren(const std::string& path, WatchCallback callback);
    
    /**
     * @brief 取消监听
     * @param path 监听路径
     * 
     * @note 客户端用于：停止监听某个服务
     */
    virtual void UnwatchChildren(const std::string& path);
    
private:
    /**
     * @brief 全局 Watcher 回调（静态，供 C API 调用）
     * @param type 事件类型：ZOO_SESSION_EVENT（会话状态变更）、ZOO_CHILD_EVENT（子节点列表变更）等
     * @param state 会话状态：连接、断开、过期等（用于：ZOO_SESSION_EVENT）
     * @param path 子节点路径（用于：ZOO_CHILD_EVENT）
     * @param context 用户上下文，传入ZooKeeperClient*，方便在内部调用HandleXXX函数
     * @note 必须是static函数，因为是供 C API 调用，故不可以用普通成员函数
     */
    static void GlobalWatcher(zhandle_t* zh, int type, int state,
                              const char* path, void* context);
    
    /**
     * @brief 处理会话状态变化
     */
    void HandleSessionEvent(int state);
    
    /**
     * @brief 处理子节点变化事件
     */
    void HandleChildEvent(const std::string& path);
    
    /**
     * @brief 重新注册指定路径的 watch
     */
    void ResetWatch(const std::string& path);

    // ZooKeeper 句柄
    zhandle_t* zh_ = nullptr;
    
    // 配置
    std::string hosts_;
    int session_timeout_ms_;
    
    // 连接状态（原子变量，线程安全）
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};  // 标记正在关闭
    
    // 连接等待
    std::mutex conn_mutex_;
    std::condition_variable conn_cv_;
    
    // Watch 回调管理（因为watch只能使用一次，所以会多次注册watch，故需要存储“子节点路径——>回调处理函数”）
    std::mutex watch_mutex_;
    std::map<std::string, WatchCallback> watches_;  // 回调map：子节点路径——>回调处理函数（客户端设置）
};

} // namespace user_service
