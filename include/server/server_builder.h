#pragma once

#include <memory>
#include <string>
#include <functional>
#include <optional>
#include "server/grpc_server.h"
#include "config/config.h"

namespace user_service {

/**
 * @brief 服务器构建器（Builder 模式）
 * 
 * 提供流畅的 API 来配置和创建 gRPC 服务器，支持：
 * - 从配置文件/对象加载配置
 * - 运行时覆盖配置项
 * - ZooKeeper 服务注册
 * - 环境变量覆盖
 * 
 * ============================================================================
 * 架构说明
 * ============================================================================
 * 
 * 在微服务架构中，通常有多个服务：
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                          ZooKeeper                                      │
 *   │                                                                         │
 *   │   /services                                                             │
 *   │       ├── /auth-service                    ← 认证服务注册路径            │
 *   │       │       ├── 192.168.1.10:50051       ← 实例1（临时节点）           │
 *   │       │       └── 192.168.1.11:50051       ← 实例2（临时节点）           │
 *   │       │                                                                 │
 *   │       ├── /user-service                    ← 用户服务注册路径            │
 *   │       │       ├── 192.168.1.10:50052       ← 实例1                      │
 *   │       │       └── 192.168.1.12:50052       ← 实例2                      │
 *   │       │                                                                 │
 *   │       └── /order-service                   ← 其他服务...                 │
 *   │               └── ...                                                   │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * 每个服务可以有多个实例（水平扩展），通过 ServerBuilder 配置：
 * - service_name: 决定注册到哪个路径（如 "auth-service" 或 "user-service"）
 * - host:port: 决定实例节点的名称
 * 
 * ============================================================================
 * 使用示例
 * ============================================================================
 * 
 * @example 示例1：启动认证服务（auth-service）
 * @code
 *   // auth_service_main.cpp
 *   auto server = ServerBuilder()
 *       .WithConfigFile("config.yaml")
 *       .WithPort(50051)                           // gRPC 监听端口
 *       .WithServiceName("auth-service")           // ← 注册为 auth-service
 *       .EnableServiceDiscovery(true)              // 启用 ZooKeeper 注册
 *       .Build();
 *   
 *   // 会在 ZooKeeper 创建：/services/auth-service/192.168.1.10:50051
 *   server->Run();
 * @endcode
 * 
 * @example 示例2：启动用户服务（user-service）
 * @code
 *   // user_service_main.cpp
 *   auto server = ServerBuilder()
 *       .WithConfigFile("config.yaml")
 *       .WithPort(50052)                           // 不同端口
 *       .WithServiceName("user-service")           // ← 注册为 user-service
 *       .EnableServiceDiscovery(true)
 *       .Build();
 *   
 *   // 会在 ZooKeeper 创建：/services/user-service/192.168.1.10:50052
 *   server->Run();
 * @endcode
 * 
 * @example 示例3：同一服务的多实例部署
 * @code
 *   // 实例1（机器A）
 *   auto server1 = ServerBuilder()
 *       .WithConfigFile("config.yaml")
 *       .WithHost("192.168.1.10")
 *       .WithPort(50051)
 *       .WithServiceName("user-service")
 *       .Build();
 *   
 *   // 实例2（机器B）
 *   auto server2 = ServerBuilder()
 *       .WithConfigFile("config.yaml")
 *       .WithHost("192.168.1.11")
 *       .WithPort(50051)
 *       .WithServiceName("user-service")
 *       .Build();
 *   
 *   // ZooKeeper 中会有：
 *   // /services/user-service/192.168.1.10:50051
 *   // /services/user-service/192.168.1.11:50051
 * @endcode
 * 
 * @example 示例4：Docker/K8s 环境（通过环境变量配置）
 * @code
 *   // 环境变量：
 *   //   SERVICE_NAME=user-service
 *   //   GRPC_PORT=50051
 *   //   ZK_HOSTS=zookeeper:2181
 *   
 *   auto server = ServerBuilder()
 *       .WithConfigFile("/app/config.yaml")
 *       .LoadFromEnvironment()                     // ← 环境变量会覆盖配置文件
 *       .Build();
 * @endcode
 * 
 * @example 示例5：开发模式（禁用服务发现）
 * @code
 *   auto server = ServerBuilder()
 *       .WithConfigFile("config.yaml")
 *       .EnableServiceDiscovery(false)             // ← 不连接 ZooKeeper
 *       .Build();
 * @endcode
 * 
 * @example 示例6：设置关闭回调（用于资源清理）
 * @code
 *   auto server = ServerBuilder()
 *       .WithConfigFile("config.yaml")
 *       .OnShutdown([]() {
 *           LOG_INFO("Server is shutting down, cleaning up...");
 *           // 清理资源、发送告警等
 *       })
 *       .Build();
 * @endcode
 * 
 * ============================================================================
 * 配置优先级（从低到高）
 * ============================================================================
 * 
 *   1. 配置文件默认值（config.yaml）
 *   2. 环境变量覆盖（LoadFromEnvironment）
 *   3. Builder 方法覆盖（WithPort、WithServiceName 等）
 * 
 * 即：WithPort(50052) 会覆盖环境变量和配置文件中的端口设置
 */
class ServerBuilder {
public:
    ServerBuilder() = default;

    // ========================================================================
    // 配置加载方法
    // ========================================================================

    /**
     * @brief 从 YAML 配置文件加载配置
     * 
     * @param path 配置文件路径（支持相对路径和绝对路径）
     * @return ServerBuilder& 返回自身，支持链式调用
     * 
     * @throws std::runtime_error 配置文件不存在或解析失败
     * 
     * @note 配置文件格式示例（config.yaml）：
     * @code{.yaml}
     *   server:
     *     host: "0.0.0.0"
     *     grpc_port: 50051
     *   
     *   zookeeper:
     *     enabled: true
     *     hosts: "127.0.0.1:2181"
     *     root_path: "/services"
     *     service_name: "user-service"    # ← 服务名称
     *     register_self: true             # ← 是否注册到 ZK
     *     weight: 100                     # ← 负载均衡权重
     * @endcode
     * 
     * @example
     * @code
     *   // 相对路径
     *   builder.WithConfigFile("configs/config.yaml");
     *   
     *   // 绝对路径
     *   builder.WithConfigFile("/etc/user-service/config.yaml");
     *   
     *   // Docker 环境
     *   builder.WithConfigFile("/app/configs/config.docker.yaml");
     * @endcode
     */
    ServerBuilder& WithConfigFile(const std::string& path);

    /**
     * @brief 使用已加载的配置对象
     * 
     * @param config 配置对象智能指针
     * @return ServerBuilder& 返回自身
     * 
     * @note 适用于：
     *   - 需要在代码中动态修改配置
     *   - 多个服务共享配置对象
     *   - 单元测试中注入 mock 配置
     * 
     * @example
     * @code
     *   // 动态创建配置
     *   auto config = std::make_shared<Config>();
     *   config->server.grpc_port = 50051;
     *   config->zookeeper.service_name = "auth-service";
     *   
     *   auto server = ServerBuilder()
     *       .WithConfig(config)
     *       .Build();
     * @endcode
     */
    ServerBuilder& WithConfig(std::shared_ptr<Config> config);

    /**
     * @brief 从环境变量加载配置（覆盖配置文件）
     * 
     * @return ServerBuilder& 返回自身
     * 
     * @note 支持的环境变量：
     * 
     *   | 环境变量              | 作用                  | 示例                    |
     *   |-----------------------|-----------------------|-------------------------|
     *   | MYSQL_HOST            | MySQL 主机            | mysql                   |
     *   | MYSQL_PASSWORD        | MySQL 密码            | root123                 |
     *   | REDIS_HOST            | Redis 主机            | redis                   |
     *   | ZK_HOSTS              | ZooKeeper 地址        | zk1:2181,zk2:2181       |
     *   | ZK_ROOT_PATH          | 服务根路径            | /services               |
     *   | ZK_SERVICE_NAME       | 服务名称 ★            | auth-service            |
     *   | ZK_ENABLED            | 启用服务发现          | true                    |
     *   | ZK_REGISTER_SELF      | 注册自身              | true                    |
     *   | ZK_WEIGHT             | 负载均衡权重          | 100                     |
     *   | JWT_SECRET            | JWT 密钥              | your-secret-key         |
     * 
     * @example Docker Compose 配置
     * @code{.yaml}
     *   # docker-compose.yml
     *   services:
     *     auth-service:
     *       environment:
     *         - ZK_SERVICE_NAME=auth-service    # ← 设置服务名
     *         - ZK_HOSTS=zookeeper:2181
     *         - ZK_ENABLED=true
     *     
     *     user-service:
     *       environment:
     *         - ZK_SERVICE_NAME=user-service    # ← 不同服务名
     *         - ZK_HOSTS=zookeeper:2181
     *         - ZK_ENABLED=true
     * @endcode
     * 
     * @example Kubernetes Deployment
     * @code{.yaml}
     *   # k8s-deployment.yaml
     *   spec:
     *     containers:
     *     - name: user-service
     *       env:
     *       - name: ZK_SERVICE_NAME
     *         value: "user-service"
     *       - name: ZK_HOSTS
     *         value: "zk-headless.default.svc.cluster.local:2181"
     *       - name: POD_IP
     *         valueFrom:
     *           fieldRef:
     *             fieldPath: status.podIP
     * @endcode
     */
    ServerBuilder& LoadFromEnvironment();

    // ========================================================================
    // 服务器配置方法
    // ========================================================================

    /**
     * @brief 覆盖 gRPC 监听端口
     * 
     * @param port 端口号（1-65535）
     * @return ServerBuilder& 返回自身
     * 
     * @note 此设置会覆盖配置文件和环境变量中的端口
     * 
     * @example 不同服务使用不同端口
     * @code
     *   // auth-service 使用 50051
     *   auto auth_server = ServerBuilder()
     *       .WithConfigFile("config.yaml")
     *       .WithPort(50051)
     *       .WithServiceName("auth-service")
     *       .Build();
     *   
     *   // user-service 使用 50052
     *   auto user_server = ServerBuilder()
     *       .WithConfigFile("config.yaml")
     *       .WithPort(50052)
     *       .WithServiceName("user-service")
     *       .Build();
     * @endcode
     */
    ServerBuilder& WithPort(int port);

    /**
     * @brief 覆盖监听地址
     * 
     * @param host 主机地址
     * @return ServerBuilder& 返回自身
     * 
     * @note 常用值：
     *   - "0.0.0.0": 监听所有网卡（Docker/K8s 推荐）
     *   - "127.0.0.1": 仅本地访问（开发模式）
     *   - "192.168.1.10": 指定网卡 IP
     * 
     * @warning 在 Docker 中必须使用 "0.0.0.0"，否则无法从容器外访问
     * 
     * @example
     * @code
     *   // Docker 环境
     *   builder.WithHost("0.0.0.0");
     *   
     *   // 本地开发
     *   builder.WithHost("127.0.0.1");
     *   
     *   // 指定 IP（多网卡场景）
     *   builder.WithHost("10.0.0.5");
     * @endcode
     */
    ServerBuilder& WithHost(const std::string& host);

    // ========================================================================
    // 服务发现配置方法（ZooKeeper）
    // ========================================================================

    /**
     * @brief 启用或禁用服务发现（ZooKeeper 注册）
     * 
     * @param enable true=启用，false=禁用
     * @return ServerBuilder& 返回自身
     * 
     * @note 禁用服务发现的场景：
     *   - 本地开发/调试
     *   - 单机部署
     *   - 使用其他服务发现机制（如 K8s Service）
     * 
     * @example
     * @code
     *   // 开发模式：不需要 ZooKeeper
     *   auto dev_server = ServerBuilder()
     *       .WithConfigFile("config.yaml")
     *       .EnableServiceDiscovery(false)
     *       .Build();
     *   
     *   // 生产模式：启用 ZooKeeper
     *   auto prod_server = ServerBuilder()
     *       .WithConfigFile("config.yaml")
     *       .EnableServiceDiscovery(true)
     *       .Build();
     * @endcode
     */
    ServerBuilder& EnableServiceDiscovery(bool enable);

    /**
     * @brief 设置服务名称（ZooKeeper 注册路径）★ 关键配置
     * 
     * @param name 服务名称，如 "auth-service"、"user-service"
     * @return ServerBuilder& 返回自身
     * 
     * @note 服务名称决定了：
     *   1. ZooKeeper 中的注册路径：/services/{service_name}/
     *   2. 客户端通过服务名称发现此服务
     *   3. 监控/日志中的服务标识
     * 
     * @warning 
     *   - 同一类服务的所有实例必须使用相同的服务名称
     *   - 不同类服务必须使用不同的服务名称
     * 
     * @example 完整的多服务部署示例
     * @code
     *   // ==================== auth_service/main.cpp ====================
     *   int main() {
     *       auto server = ServerBuilder()
     *           .WithConfigFile("config.yaml")
     *           .WithPort(50051)
     *           .WithServiceName("auth-service")      // ← 认证服务
     *           .EnableServiceDiscovery(true)
     *           .Build();
     *       
     *       server->Initialize();
     *       server->Run();
     *       // ZooKeeper: /services/auth-service/192.168.1.10:50051
     *   }
     *   
     *   // ==================== user_service/main.cpp ====================
     *   int main() {
     *       auto server = ServerBuilder()
     *           .WithConfigFile("config.yaml")
     *           .WithPort(50052)
     *           .WithServiceName("user-service")      // ← 用户服务
     *           .EnableServiceDiscovery(true)
     *           .Build();
     *       
     *       server->Initialize();
     *       server->Run();
     *       // ZooKeeper: /services/user-service/192.168.1.10:50052
     *   }
     *   
     *   // ==================== order_service/main.cpp ====================
     *   int main() {
     *       auto server = ServerBuilder()
     *           .WithConfigFile("config.yaml")
     *           .WithPort(50053)
     *           .WithServiceName("order-service")     // ← 订单服务
     *           .EnableServiceDiscovery(true)
     *           .Build();
     *       
     *       server->Initialize();
     *       server->Run();
     *       // ZooKeeper: /services/order-service/192.168.1.10:50053
     *   }
     * @endcode
     * 
     * @example 服务名称命名规范
     * @code
     *   // ✅ 推荐：小写字母 + 连字符
     *   .WithServiceName("auth-service")
     *   .WithServiceName("user-service")
     *   .WithServiceName("order-service")
     *   .WithServiceName("payment-gateway")
     *   
     *   // ❌ 不推荐
     *   .WithServiceName("AuthService")     // 大小写混用
     *   .WithServiceName("user_service")    // 下划线
     *   .WithServiceName("user.service")    // 点号（ZK 路径分隔符）
     * @endcode
     */
    ServerBuilder& WithServiceName(const std::string& name);

    // ========================================================================
    // 生命周期回调
    // ========================================================================

    /**
     * @brief 设置服务器关闭回调
     * 
     * @param callback 关闭时执行的回调函数
     * @return ServerBuilder& 返回自身
     * 
     * @note 回调触发时机：
     *   1. 收到 SIGINT/SIGTERM 信号
     *   2. 调用 server->Shutdown()
     *   3. ZooKeeper 注销完成后
     *   4. gRPC 服务器停止前
     * 
     * @note 回调中适合做的事情：
     *   - 记录日志
     *   - 发送告警通知
     *   - 清理临时资源
     *   - 完成进行中的请求（graceful shutdown）
     * 
     * @warning 回调中不要做耗时操作，避免阻塞关闭流程
     * 
     * @example
     * @code
     *   auto server = ServerBuilder()
     *       .WithConfigFile("config.yaml")
     *       .OnShutdown([]() {
     *           LOG_INFO("=== Server Shutdown ===");
     *           
     *           // 发送钉钉/飞书告警
     *           NotifyOps("user-service is shutting down");
     *           
     *           // 等待进行中的请求完成（可选）
     *           std::this_thread::sleep_for(std::chrono::seconds(2));
     *       })
     *       .Build();
     * @endcode
     */
    ServerBuilder& OnShutdown(GrpcServer::ShutdownCallback callback);

    // ========================================================================
    // 构建方法
    // ========================================================================

    /**
     * @brief 构建 GrpcServer 实例
     * 
     * @return std::unique_ptr<GrpcServer> 服务器实例
     * 
     * @throws std::runtime_error 配置未设置或配置无效
     * 
     * @note 构建过程：
     *   1. 检查配置是否已加载
     *   2. 应用环境变量覆盖（如果 LoadFromEnvironment 被调用）
     *   3. 应用 Builder 方法设置的覆盖值
     *   4. 创建 GrpcServer 实例
     *   5. 设置关闭回调
     * 
     * @note Build() 后需要手动调用：
     *   - server->Initialize(): 初始化所有组件
     *   - server->Run(): 启动服务器（阻塞）
     * 
     * @example 完整启动流程
     * @code
     *   // 1. 构建服务器
     *   auto server = ServerBuilder()
     *       .WithConfigFile("config.yaml")
     *       .WithServiceName("user-service")
     *       .Build();
     *   
     *   if (!server) {
     *       std::cerr << "Failed to build server" << std::endl;
     *       return 1;
     *   }
     *   
     *   // 2. 初始化组件（MySQL、Redis、ZK 等）
     *   if (!server->Initialize()) {
     *       std::cerr << "Failed to initialize server" << std::endl;
     *       return 1;
     *   }
     *   
     *   // 3. 启动服务器（阻塞，直到收到关闭信号）
     *   server->Run();
     *   
     *   return 0;
     * @endcode
     */
    std::unique_ptr<GrpcServer> Build();

private:
    /// 配置对象（从文件加载或外部传入）
    std::shared_ptr<Config> config_;
    
    /// 端口覆盖值（优先级最高）
    std::optional<int> port_override_;
    
    /// 主机地址覆盖值
    std::optional<std::string> host_override_;
    
    /// 服务发现开关覆盖值
    std::optional<bool> service_discovery_override_;
    
    /// 服务名称覆盖值（决定 ZK 注册路径）
    std::optional<std::string> service_name_override_;
    
    /// 关闭回调函数
    GrpcServer::ShutdownCallback shutdown_callback_;
    
    /// 是否从环境变量加载配置
    bool load_env_ = false;
};

} // namespace user_service
