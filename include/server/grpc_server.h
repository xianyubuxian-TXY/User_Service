#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "config/config.h"
#include "handlers/auth_handler.h"
#include "handlers/user_handler.h"
#include "service/auth_service.h"
#include "service/user_service.h"
#include "discovery/service_registry.h"
#include "auth/token_cleanup_task.h"

namespace user_service {

/**
 * @brief gRPC 服务器封装类
 * 
 * 提供统一的服务器生命周期管理：
 * - 初始化所有依赖组件
 * - 启动 gRPC 服务
 * - 注册到服务发现
 * - 优雅关闭
 * 
 * @example
 * @code
 *   auto config = Config::LoadFromFile("config.yaml");
 *   GrpcServer server(std::make_shared<Config>(config));
 *   
 *   if (!server.Initialize()) {
 *       return 1;
 *   }
 *   
 *   server.Run();  // 阻塞直到收到关闭信号
 * @endcode
 */
class GrpcServer {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    // 关闭回调类型
    using ShutdownCallback = std::function<void()>;

    /**
     * @brief 构造函数
     * @param config 配置对象
     */
    explicit GrpcServer(std::shared_ptr<Config> config);
    
    /**
     * @brief 析构函数，确保资源释放
     */
    ~GrpcServer();

    // 禁止拷贝和移动
    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;
    GrpcServer(GrpcServer&&) = delete;
    GrpcServer& operator=(GrpcServer&&) = delete;

    /**
     * @brief 初始化所有组件
     * @return 初始化成功返回 true
     * 
     * 初始化顺序：
     * 1. MySQL 连接池
     * 2. Redis 客户端
     * 3. Repository 层
     * 4. Service 层
     * 5. Handler 层
     * 6. ZooKeeper 客户端（可选）
     */
    bool Initialize();

    /**
     * @brief 启动服务器（阻塞）
     * 
     * 调用后会阻塞直到：
     * - 收到 SIGINT/SIGTERM 信号
     * - 调用 Shutdown() 方法
     */
    void Run();

    /**
     * @brief 异步启动服务器（非阻塞）
     * @return 启动成功返回 true
     */
    bool Start();

    /**
     * @brief 请求关闭服务器
     * @param deadline 关闭超时时间，默认 5 秒
     */
    void Shutdown(std::chrono::milliseconds deadline = std::chrono::milliseconds(5000));

    /**
     * @brief 等待服务器关闭
     */
    void Wait();

    /**
     * @brief 检查服务器是否正在运行
     */
    bool IsRunning() const { return running_.load(); }

    /**
     * @brief 获取监听地址
     */
    std::string GetAddress() const;

    /**
     * @brief 设置关闭回调
     */
    void SetShutdownCallback(ShutdownCallback callback) {
        shutdown_callback_ = std::move(callback);
    }

    // ==================== 组件访问器（用于测试或扩展）====================
    
    std::shared_ptr<AuthService> GetAuthService() const { return auth_service_; }
    std::shared_ptr<UserService> GetUserService() const { return user_service_; }
    std::shared_ptr<Config> GetConfig() const { return config_; }

private:
    /**
     * @brief 初始化基础设施（MySQL、Redis）
     */
    bool InitInfrastructure();

    /**
     * @brief 初始化 Repository 层
     */
    bool InitRepositories();

    /**
     * @brief 初始化 Service 层
     */
    bool InitServices();

    /**
     * @brief 初始化 Handler 层
     */
    bool InitHandlers();

    /**
     * @brief 初始化服务发现
     */
    bool InitServiceDiscovery();

    /**
     * @brief 注册到 ZooKeeper
     */
    bool RegisterToZooKeeper();

    /**
     * @brief 从 ZooKeeper 注销
     */
    void UnregisterFromZooKeeper();

    /**
     * @brief 关闭监控线程
     */
    void ShutdownMonitor();

private:
    // 配置
    std::shared_ptr<Config> config_;

    // gRPC 服务器
    std::unique_ptr<grpc::Server> server_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};

    // 基础设施
    std::shared_ptr<MySQLPool> mysql_pool_;
    std::shared_ptr<RedisClient> redis_client_;

    // Repository 层
    std::shared_ptr<UserDB> user_db_;
    std::shared_ptr<TokenRepository> token_repo_;

    // Service 层
    std::shared_ptr<JwtService> jwt_service_;
    std::shared_ptr<SmsService> sms_service_;
    std::shared_ptr<AuthService> auth_service_;
    std::shared_ptr<UserService> user_service_;

    // Handler 层
    std::shared_ptr<Authenticator> authenticator_;
    std::unique_ptr<AuthHandler> auth_handler_;
    std::unique_ptr<UserHandler> user_handler_;

    // 服务发现
    std::shared_ptr<ZooKeeperClient> zk_client_;
    std::shared_ptr<ServiceRegistry> service_registry_;

    // 后台任务
    std::shared_ptr<TokenCleanupTask> token_cleanup_task_;

    // 回调
    ShutdownCallback shutdown_callback_;

    // 监控线程
    std::thread shutdown_monitor_thread_;
};

} // namespace user_service
