# Discovery 模块 - 服务注册与发现

基于 ZooKeeper 实现的服务注册与发现模块，为微服务架构提供核心基础设施支持。

---

## 📁 目录结构

```
include/discovery/
├── zk_client.h           # ZooKeeper 客户端封装
├── service_registry.h    # 服务注册器（gRPC 服务端使用）
├── service_discovery.h   # 服务发现器（gRPC 客户端使用）
└── service_instance.h    # 服务实例数据结构

src/discovery/
├── zk_client.cpp
├── service_registry.cpp
├── service_discovery.cpp
└── CMakeLists.txt
```

---

## 🏗️ 架构设计

### 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ZooKeeper Cluster                              │
│                                                                             │
│    /services                        ← 根路径（持久节点）                     │
│    ├── /user-service                ← 服务路径（持久节点）                   │
│    │   ├── /192.168.1.10:50051      ← 实例节点（临时节点）                   │
│    │   ├── /192.168.1.11:50051      ← 实例节点（临时节点）                   │
│    │   └── /192.168.1.12:50051      ← 实例节点（临时节点）                   │
│    └── /auth-service                                                        │
│        └── /192.168.1.20:50052                                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
           ▲                                           ▲
           │ Register/Unregister                       │ Watch/GetChildren
           │                                           │
┌──────────┴──────────┐                     ┌──────────┴──────────┐
│   ServiceRegistry   │                     │  ServiceDiscovery   │
│   (gRPC 服务端)      │                     │   (gRPC 客户端)      │
├─────────────────────┤                     ├─────────────────────┤
│ • 注册服务实例       │                     │ • 订阅服务变化       │
│ • 临时节点（心跳）   │                     │ • 本地缓存实例列表   │
│ • 优雅下线          │                     │ • 负载均衡选择       │
└─────────────────────┘                     └─────────────────────┘
           │                                           │
           │                                           │
    ┌──────┴──────┐                             ┌──────┴──────┐
    │ User Service │                             │ API Gateway │
    │   实例 1     │                             │  / Client   │
    └─────────────┘                             └─────────────┘
```

### 数据流

```
服务端启动流程：
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ 读取配置    │ ──► │ 连接 ZK     │ ──► │ 注册实例    │
│ (host:port) │     │ (Connect)   │     │ (Register)  │
└─────────────┘     └─────────────┘     └─────────────┘

客户端请求流程：
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ 订阅服务    │ ──► │ 获取实例    │ ──► │ 负载均衡    │ ──► │ 建立连接    │
│ (Subscribe) │     │ (Refresh)   │     │ (Select)    │     │ (gRPC)      │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
        │                   ▲
        │                   │
        └───────────────────┘
        Watch 触发自动刷新
```

---

## 🔧 核心组件

### 1. ZooKeeperClient

ZooKeeper C API 的 C++ 封装，提供线程安全的节点操作。

#### 特性
- ✅ 同步连接（带超时）
- ✅ 自动重连处理
- ✅ Watch 机制封装（一次性 → 自动重注册）
- ✅ 线程安全

#### 接口设计

```cpp
class ZooKeeperClient {
public:
    // ==================== 连接管理 ====================
    bool Connect(int timeout_ms = 10000);   // 连接（阻塞直到成功或超时）
    void Close();                            // 关闭连接
    bool IsConnected() const;                // 检查连接状态
    
    // ==================== 服务端接口（服务注册） ====================
    bool CreateNode(const std::string& path, const std::string& data, bool ephemeral);
    bool CreateServicePath(const std::string& path);  // 递归创建路径
    bool DeleteNode(const std::string& path);
    bool SetData(const std::string& path, const std::string& data);
    bool Exists(const std::string& path);
    
    // ==================== 客户端接口（服务发现） ====================
    std::string GetData(const std::string& path);
    std::vector<std::string> GetChildren(const std::string& path);
    void WatchChildren(const std::string& path, WatchCallback callback);
    void UnwatchChildren(const std::string& path);
};
```

#### 使用示例

```cpp
#include "discovery/zk_client.h"

// 创建客户端
auto zk = std::make_shared<ZooKeeperClient>("127.0.0.1:2181", 15000);

// 连接（阻塞等待）
if (!zk->Connect(10000)) {
    LOG_ERROR("ZooKeeper 连接失败");
    return;
}

// 创建临时节点
zk->CreateNode("/services/user-service/192.168.1.10:50051", 
               R"({"host":"192.168.1.10","port":50051})", 
               true);  // ephemeral = true

// 监听子节点变化
zk->WatchChildren("/services/user-service", [](const std::string& path) {
    LOG_INFO("子节点变化: {}", path);
});
```

---

### 2. ServiceRegistry（服务注册器）

gRPC 服务端使用，负责将服务实例注册到 ZooKeeper。

#### 特性
- ✅ 临时节点（Ephemeral），服务下线自动删除
- ✅ 自动创建父路径
- ✅ 支持动态更新实例信息
- ✅ 优雅下线

#### 接口设计

```cpp
class ServiceRegistry {
public:
    explicit ServiceRegistry(std::shared_ptr<ZooKeeperClient> zk_client,
                            const std::string& root_path = "/services");
    
    bool Register(const ServiceInstance& instance);   // 注册
    bool Unregister();                                 // 注销
    bool Update(const ServiceInstance& instance);     // 更新
    bool IsRegistered() const;                         // 是否已注册
};
```

#### 使用示例

```cpp
#include "discovery/service_registry.h"

// 创建注册器
auto registry = std::make_shared<ServiceRegistry>(zk_client, "/services");

// 构建实例信息
ServiceInstance instance;
instance.service_name = "user-service";
instance.host = "192.168.1.10";
instance.port = 50051;
instance.weight = 100;
instance.metadata["version"] = "1.0.0";
instance.metadata["region"] = "cn-east";

// 注册
if (registry->Register(instance)) {
    LOG_INFO("服务注册成功: {}", instance.GetAddress());
}

// 服务运行中...

// 更新权重（如健康检查降级）
instance.weight = 50;
registry->Update(instance);

// 优雅下线
registry->Unregister();
```

#### ZooKeeper 节点结构

```
/services                           ← 根路径（持久节点）
└── /user-service                   ← 服务路径（持久节点）
    └── /192.168.1.10:50051         ← 实例节点（临时节点）
        │
        └── 节点数据 (JSON):
            {
                "service_name": "user-service",
                "instance_id": "192.168.1.10:50051",
                "host": "192.168.1.10",
                "port": 50051,
                "weight": 100,
                "metadata": {
                    "version": "1.0.0",
                    "region": "cn-east"
                }
            }
```

---

### 3. ServiceDiscovery（服务发现器）

gRPC 客户端使用，负责发现可用的服务实例。

#### 特性
- ✅ 本地缓存（避免每次请求都访问 ZK）
- ✅ Watch 自动更新（实例变化时实时同步）
- ✅ 多种负载均衡策略
- ✅ 线程安全

#### 接口设计

```cpp
class ServiceDiscovery {
public:
    using ServiceChangeCallback = std::function<void(const std::string& service_name)>;
    
    explicit ServiceDiscovery(std::shared_ptr<ZooKeeperClient> zk_client,
                             const std::string& root_path = "/services");
    
    // 订阅/取消订阅
    void Subscribe(const std::string& service_name, ServiceChangeCallback callback = nullptr);
    void Unsubscribe(const std::string& service_name);
    
    // 获取实例
    std::vector<ServiceInstance> GetInstances(const std::string& service_name);
    
    // 负载均衡
    std::shared_ptr<ServiceInstance> SelectInstance(const std::string& service_name);         // 随机
    std::shared_ptr<ServiceInstance> SelectInstanceWeighted(const std::string& service_name); // 加权随机
};
```

#### 使用示例

```cpp
#include "discovery/service_discovery.h"

// 创建发现器
auto discovery = std::make_shared<ServiceDiscovery>(zk_client, "/services");

// 订阅服务（立即拉取 + 自动监听变化）
discovery->Subscribe("user-service", [](const std::string& service) {
    LOG_INFO("服务 {} 实例列表已更新", service);
});

// 获取所有实例
auto instances = discovery->GetInstances("user-service");
for (const auto& inst : instances) {
    LOG_INFO("实例: {}:{} (权重: {})", inst.host, inst.port, inst.weight);
}

// 负载均衡选择一个实例
auto instance = discovery->SelectInstance("user-service");
if (instance) {
    // 创建 gRPC Channel
    auto channel = grpc::CreateChannel(
        instance->GetAddress(),
        grpc::InsecureChannelCredentials()
    );
    auto stub = pb_user::UserService::NewStub(channel);
}

// 取消订阅
discovery->Unsubscribe("user-service");
```

#### 负载均衡策略

| 方法 | 策略 | 适用场景 |
|------|------|----------|
| `SelectInstance` | 随机 | 实例配置相同 |
| `SelectInstanceWeighted` | 加权随机 | 实例配置不同（如不同机型） |

```cpp
// 加权随机示例
// 实例 A: weight=100, 实例 B: weight=50
// A 被选中概率: 100/(100+50) = 66.7%
// B 被选中概率: 50/(100+50) = 33.3%
auto instance = discovery->SelectInstanceWeighted("user-service");
```

---

### 4. ServiceInstance（服务实例）

服务实例的数据结构，支持 JSON 序列化。

```cpp
struct ServiceInstance {
    std::string service_name;   // 服务名
    std::string instance_id;    // 实例ID
    std::string host;           // 主机地址
    int port;                   // 端口号
    int weight = 100;           // 权重
    std::map<std::string, std::string> metadata;  // 元数据
    
    std::string GetAddress() const;          // 获取 "host:port"
    std::string ToJson() const;              // 序列化
    static ServiceInstance FromJson(const std::string& json);  // 反序列化
    bool IsValid() const;                    // 有效性检查
};
```

---

## ⚙️ 配置说明

### config.yaml

```yaml
zookeeper:
  # ==================== 连接配置 ====================
  hosts: "127.0.0.1:2181"           # ZK 地址，多个用逗号分隔
  session_timeout_ms: 30000          # 会话超时（推荐 10000-30000）
  connect_timeout_ms: 10000          # 连接超时
  
  # ==================== 服务注册配置 ====================
  root_path: "/services"             # 服务根路径
  service_name: "user-service"       # 当前服务名称
  
  # ==================== 开关 ====================
  enabled: true                      # 是否启用服务注册/发现
  register_self: true                # 是否注册自身
                                     # - 服务端设为 true
                                     # - 纯客户端设为 false
  
  # ==================== 元数据（可选） ====================
  weight: 100                        # 服务权重
  region: "cn-east"                  # 区域标识
  zone: "zone-a"                     # 可用区
  version: "1.0.0"                   # 服务版本
```

### 环境变量覆盖

```bash
export ZK_HOSTS="10.0.0.1:2181,10.0.0.2:2181"
export ZK_ROOT_PATH="/services"
export ZK_SERVICE_NAME="user-service"
export ZK_ENABLED="true"
export ZK_REGISTER_SELF="true"
export ZK_WEIGHT="100"
```

---

## 🚀 集成示例

### 服务端集成（main.cpp）

```cpp
#include "discovery/zk_client.h"
#include "discovery/service_registry.h"

int main() {
    // 1. 加载配置
    auto config = Config::LoadFromFile("config.yaml");
    
    // 2. 连接 ZooKeeper
    auto zk_client = std::make_shared<ZooKeeperClient>(
        config.zookeeper.hosts,
        config.zookeeper.session_timeout_ms
    );
    
    if (!zk_client->Connect(config.zookeeper.connect_timeout_ms)) {
        LOG_ERROR("ZooKeeper 连接失败");
        return 1;
    }
    
    // 3. 创建服务注册器
    auto registry = std::make_shared<ServiceRegistry>(
        zk_client, 
        config.zookeeper.root_path
    );
    
    // 4. 注册服务实例
    ServiceInstance instance;
    instance.service_name = config.zookeeper.service_name;
    instance.host = config.server.host;
    instance.port = config.server.grpc_port;
    instance.weight = config.zookeeper.weight;
    instance.metadata["version"] = config.zookeeper.version;
    instance.metadata["region"] = config.zookeeper.region;
    
    if (!registry->Register(instance)) {
        LOG_ERROR("服务注册失败");
        return 1;
    }
    
    LOG_INFO("服务注册成功: {}", instance.GetAddress());
    
    // 5. 启动 gRPC 服务
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    // ... 注册 Service Handler ...
    auto server = builder.BuildAndStart();
    
    // 6. 等待信号
    server->Wait();
    
    // 7. 优雅下线
    registry->Unregister();
    zk_client->Close();
    
    return 0;
}
```

### 客户端集成

```cpp
#include "discovery/zk_client.h"
#include "discovery/service_discovery.h"

class UserServiceClient {
public:
    UserServiceClient(std::shared_ptr<ServiceDiscovery> discovery)
        : discovery_(std::move(discovery)) 
    {
        // 订阅服务变化
        discovery_->Subscribe("user-service", [this](const std::string& service) {
            LOG_INFO("user-service 实例列表已更新");
            // 可选：清理连接缓存
        });
    }
    
    Result<UserInfo> GetUser(const std::string& user_id) {
        // 负载均衡选择实例
        auto instance = discovery_->SelectInstance("user-service");
        if (!instance) {
            return Result<UserInfo>::Fail(ErrorCode::ServiceUnavailable, "无可用实例");
        }
        
        // 创建 gRPC Channel（实际项目中应缓存 Channel）
        auto channel = grpc::CreateChannel(
            instance->GetAddress(),
            grpc::InsecureChannelCredentials()
        );
        auto stub = pb_user::UserService::NewStub(channel);
        
        // 发起请求
        grpc::ClientContext context;
        pb_user::GetUserRequest request;
        pb_user::GetUserResponse response;
        request.set_id(user_id);
        
        auto status = stub->GetUser(&context, request, &response);
        if (!status.ok()) {
            return Result<UserInfo>::Fail(ErrorCode::Internal, status.error_message());
        }
        
        return Result<UserInfo>::Ok(/* ... */);
    }
    
private:
    std::shared_ptr<ServiceDiscovery> discovery_;
};
```

---

## 📊 临时节点 vs 持久节点

| 特性 | 临时节点 (Ephemeral) | 持久节点 (Persistent) |
|------|---------------------|----------------------|
| 生命周期 | 与会话绑定，断开自动删除 | 永久存在，需手动删除 |
| 用途 | 服务实例节点 | 服务路径 |
| 示例路径 | `/services/user-service/192.168.1.10:50051` | `/services/user-service` |
| 优势 | 自动下线检测 | 服务全下线后路径仍存在，客户端可继续 Watch |

**设计原因：**
```
/services/user-service/192.168.1.100:50051
├────────┬────────────┬─────────────────────┤
│  持久  │    持久    │        临时          │
└────────┴────────────┴─────────────────────┘

• /services          → 持久：服务根目录，永远存在
• /user-service      → 持久：服务名，永远存在
• /192.168.1.100:50051 → 临时：实例节点，服务下线自动删除

好处：服务全部下线后，/services/user-service 还在，
     客户端可以继续 Watch，等待新实例上线
```

---

## 🔄 Watch 机制

### ZooKeeper Watch 特性
- **一次性**：触发后自动失效，需重新注册
- **异步通知**：变化时在 ZK 事件线程中回调

### 本模块处理
```cpp
// ZooKeeperClient 内部自动重新注册 Watch
void ZooKeeperClient::HandleChildEvent(const std::string& path) {
    // 1. 触发用户回调
    if (callback) callback(path);
    
    // 2. 重新注册 Watch（关键！）
    ResetWatch(path);
}
```

### Watch 数据流
```
┌──────────────┐     子节点变化      ┌──────────────┐
│  ZooKeeper   │ ──────────────────► │ GlobalWatcher│
│   Server     │                     │  (静态回调)   │
└──────────────┘                     └──────┬───────┘
                                            │
                                            ▼
                                    ┌──────────────┐
                                    │ HandleChild  │
                                    │   Event      │
                                    └──────┬───────┘
                                            │
                        ┌───────────────────┼───────────────────┐
                        │                   │                   │
                        ▼                   ▼                   ▼
               ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
               │ 用户回调     │    │ 重新注册     │    │ 刷新缓存     │
               │ (optional)   │    │ Watch        │    │ (Discovery)  │
               └──────────────┘    └──────────────┘    └──────────────┘
```

---

## 🧪 测试

### 单元测试

```bash
cd build
ctest -R discovery --output-on-failure
```

### 集成测试

```bash
# 1. 启动 ZooKeeper
docker run -d --name zk -p 2181:2181 zookeeper:3.8

# 2. 运行测试
./bin/discovery_integration_test
```

### 手动验证

```bash
# 使用 zkCli 查看节点
docker exec -it zk zkCli.sh

# 查看服务列表
ls /services

# 查看服务实例
ls /services/user-service

# 查看实例数据
get /services/user-service/192.168.1.10:50051
```

---

## ⚠️ 注意事项

### 1. 会话超时设置
```yaml
# 推荐值：10000-30000 ms
# 太小：网络抖动导致频繁断连
# 太大：服务下线检测延迟
session_timeout_ms: 15000
```

### 2. 线程安全
- `ZooKeeperClient`：所有公共方法线程安全
- `ServiceRegistry`：所有公共方法线程安全
- `ServiceDiscovery`：所有公共方法线程安全
- **Watch 回调**：在 ZK 事件线程中执行，避免长时间阻塞

### 3. 错误处理
```cpp
// 连接失败处理
if (!zk_client->Connect(10000)) {
    // 方案1：启动失败
    return 1;
    
    // 方案2：降级运行（不使用服务发现）
    // discovery_enabled = false;
}

// 注册失败处理
if (!registry->Register(instance)) {
    // 重试或告警
}
```

### 4. 生产环境建议
- 使用 ZooKeeper 集群（至少 3 节点）
- 配置合理的超时时间
- 监控 ZK 连接状态
- 实现健康检查更新权重

---

## 📚 参考资料

- [Apache ZooKeeper 官方文档](https://zookeeper.apache.org/doc/current/)
- [ZooKeeper C API](https://zookeeper.apache.org/doc/current/zookeeperProgrammers.html#ch_bindings)
- [服务发现模式](https://microservices.io/patterns/service-discovery.html)

