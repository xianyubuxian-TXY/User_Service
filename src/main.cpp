#include <iostream>
#include <memory>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

// Handler 层
#include "handlers/auth_handler.h"
#include "handlers/user_handler.h"

// Service 层
#include "service/auth_service.h"
#include "service/user_service.h"

// Repository 层
#include "db/user_db.h"
#include "auth/token_repository.h"

// 基础设施
#include "pool/connection_pool.h"
#include "db/mysql_connection.h"
#include "cache/redis_client.h"
#include "auth/jwt_service.h"
#include "auth/sms_service.h"
#include "auth/authenticator.h"
#include "config/config.h"
#include "common/logger.h"

namespace user_service {

// ============================================================================
// 全局变量（用于信号处理）
// ============================================================================

static std::atomic<bool> g_shutdown_requested{false};
static grpc::Server* g_server_ptr = nullptr;

// ============================================================================
// 信号处理 - 只设置标志，不直接调用 Shutdown()
// ============================================================================

void SignalHandler(int signal) {
    // 注意：信号处理函数中只能做最简单的操作
    // 不能调用 Shutdown()，否则会死锁！
    g_shutdown_requested.store(true, std::memory_order_release);
    
    // 使用 write() 而不是 cout/LOG，因为它是信号安全的
    const char* msg = "\n>>> 收到关闭信号，正在优雅关闭...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

// ============================================================================
// UserServiceServer 类
// ============================================================================

class UserServiceServer {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;

    explicit UserServiceServer(std::shared_ptr<Config> config)
        : config_(std::move(config)) {}

    ~UserServiceServer() {
        Shutdown();
    }

    bool Initialize() {
        try {
            std::cout << ">>> Init: 开始初始化服务组件..." << std::endl;
            LOG_INFO("正在初始化服务组件...");

            // 1. 创建 MySQL 连接池
            std::cout << ">>> Init [1/7]: 创建 MySQL 连接池..." << std::endl;
            LOG_INFO("创建 MySQL 连接池...");
            mysql_pool_ = std::make_shared<MySQLPool>(
                config_,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
            std::cout << ">>> Init [1/7]: MySQL 连接池创建完成" << std::endl;

            // 2. 创建 Redis 客户端
            std::cout << ">>> Init [2/7]: 连接 Redis..." << std::endl;
            LOG_INFO("连接 Redis...");
            redis_client_ = std::make_shared<RedisClient>(config_->redis);
            std::cout << ">>> Init [2/7]: Redis 连接完成" << std::endl;

            // 3. 创建 Repository
            std::cout << ">>> Init [3/7]: 创建数据访问层..." << std::endl;
            LOG_INFO("创建数据访问层...");
            user_db_ = std::make_shared<UserDB>(mysql_pool_);
            token_repo_ = std::make_shared<TokenRepository>(mysql_pool_);
            std::cout << ">>> Init [3/7]: 数据访问层创建完成" << std::endl;

            // 4. 创建基础服务
            std::cout << ">>> Init [4/7]: 创建基础服务..." << std::endl;
            LOG_INFO("创建基础服务...");
            jwt_service_ = std::make_shared<JwtService>(config_->security);
            sms_service_ = std::make_shared<SmsService>(redis_client_, config_->sms);
            std::cout << ">>> Init [4/7]: 基础服务创建完成" << std::endl;

            // 5. 创建业务服务
            std::cout << ">>> Init [5/7]: 创建业务服务..." << std::endl;
            LOG_INFO("创建业务服务...");
            auth_service_ = std::make_shared<AuthService>(
                config_,
                user_db_,
                redis_client_,
                token_repo_,
                jwt_service_,
                sms_service_
            );

            user_service_ = std::make_shared<UserService>(
                config_,
                user_db_,
                token_repo_,
                sms_service_
            );
            std::cout << ">>> Init [5/7]: 业务服务创建完成" << std::endl;

            // 6. 创建 Authenticator
            std::cout << ">>> Init [6/7]: 创建认证器..." << std::endl;
            LOG_INFO("创建认证器...");
            authenticator_ = std::make_shared<JwtAuthenticator>(jwt_service_);
            std::cout << ">>> Init [6/7]: 认证器创建完成" << std::endl;

            // 7. 创建 gRPC Handler
            std::cout << ">>> Init [7/7]: 创建 gRPC Handler..." << std::endl;
            LOG_INFO("创建 gRPC Handler...");
            auth_handler_ = std::make_unique<AuthHandler>(auth_service_);
            user_handler_ = std::make_unique<UserHandler>(user_service_, authenticator_);
            std::cout << ">>> Init [7/7]: gRPC Handler 创建完成" << std::endl;
            
            std::cout << ">>> Init: 所有服务组件初始化完成!" << std::endl;
            LOG_INFO("服务组件初始化完成");
            return true;

        } catch (const std::exception& e) {
            std::cerr << ">>> Init FAILED: " << e.what() << std::endl;
            LOG_ERROR("初始化失败: {}", e.what());
            return false;
        }
    }

    void Run() {
        std::string server_address = 
            config_->server.host + ":" + std::to_string(config_->server.grpc_port);
        
        std::cout << ">>> Run: 准备启动 gRPC 服务器..." << std::endl;
        std::cout << ">>> Run: 监听地址 = " << server_address << std::endl;
        
        grpc::ServerBuilder builder;
        
        // 监听地址
        std::cout << ">>> Run: 添加监听端口..." << std::endl;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        
        // 注册服务
        std::cout << ">>> Run: 注册 AuthHandler..." << std::endl;
        builder.RegisterService(auth_handler_.get());
        std::cout << ">>> Run: 注册 UserHandler..." << std::endl;
        builder.RegisterService(user_handler_.get());
        
        // 启用健康检查
        std::cout << ">>> Run: 启用健康检查服务..." << std::endl;
        grpc::EnableDefaultHealthCheckService(true);
        
        // 构建并启动服务器
        std::cout << ">>> Run: BuildAndStart()..." << std::endl;
        server_ = builder.BuildAndStart();
        
        if (!server_) {
            std::cerr << ">>> Run FAILED: 服务器启动失败！" << std::endl;
            LOG_ERROR("服务器启动失败！");
            return;
        }
        
        // 设置全局指针供信号处理使用
        g_server_ptr = server_.get();
        
        std::cout << "========================================" << std::endl;
        std::cout << "  User Service 启动成功" << std::endl;
        std::cout << "  监听地址: " << server_address << std::endl;
        std::cout << "  gRPC 服务已就绪" << std::endl;
        std::cout << "  按 Ctrl+C 优雅关闭" << std::endl;
        std::cout << "========================================" << std::endl;
        
        LOG_INFO("========================================");
        LOG_INFO("  User Service 启动成功");
        LOG_INFO("  监听地址: {}", server_address);
        LOG_INFO("  gRPC 服务已就绪");
        LOG_INFO("========================================");
        
        // ====== 关键：启动独立的关闭监控线程 ======
        std::thread shutdown_monitor([this]() {
            ShutdownMonitor();
        });
        
        std::cout << ">>> Run: 进入 Wait() 等待请求..." << std::endl;
        
        // 主线程等待服务器关闭
        server_->Wait();
        
        // 等待监控线程结束
        if (shutdown_monitor.joinable()) {
            shutdown_monitor.join();
        }
        
        g_server_ptr = nullptr;
        
        std::cout << ">>> Run: 服务器已关闭" << std::endl;
        LOG_INFO("服务器已关闭");
    }

    void Shutdown() {
        g_shutdown_requested.store(true, std::memory_order_release);
    }

private:
    // 关闭监控线程 - 在独立线程中安全调用 Shutdown()
    void ShutdownMonitor() {
        while (!g_shutdown_requested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO("检测到关闭请求，正在停止服务器...");
        std::cout << ">>> ShutdownMonitor: 正在停止服务器..." << std::endl;
        
        if (server_) {
            // 设置关闭超时时间（5秒）
            auto deadline = std::chrono::system_clock::now() + 
                           std::chrono::seconds(5);
            server_->Shutdown(deadline);
        }
        
        LOG_INFO("服务器关闭请求已发送");
    }

private:
    std::shared_ptr<Config> config_;

    // gRPC 服务器
    std::unique_ptr<grpc::Server> server_;

    // 基础设施
    std::shared_ptr<MySQLPool> mysql_pool_;
    std::shared_ptr<RedisClient> redis_client_;
    
    // Repository
    std::shared_ptr<UserDB> user_db_;
    std::shared_ptr<TokenRepository> token_repo_;
    
    // 基础服务
    std::shared_ptr<JwtService> jwt_service_;
    std::shared_ptr<SmsService> sms_service_;
    
    // 业务服务
    std::shared_ptr<AuthService> auth_service_;
    std::shared_ptr<UserService> user_service_;
    
    // 认证
    std::shared_ptr<Authenticator> authenticator_;
    
    // gRPC Handler
    std::unique_ptr<AuthHandler> auth_handler_;
    std::unique_ptr<UserHandler> user_handler_;
};

} // namespace user_service

// ============================================================================
// main 函数
// ============================================================================

int main(int argc, char* argv[]) {
    using namespace user_service;

    std::cout << "=== User Service Starting ===" << std::endl;
    std::cout << "Working directory: " << std::filesystem::current_path() << std::endl;
    std::cout.flush();
    
    try {
        // 1. 加载配置（必须先加载，因为日志配置在里面）
        std::cout << ">>> [1/5] 加载配置文件..." << std::endl;
        auto config = std::make_shared<Config>(
            Config::LoadFromFile("../../configs/config.yaml")
        );
        
        std::cout << "    Server: " << config->server.host 
                  << ":" << config->server.grpc_port << std::endl;
        std::cout << "    MySQL:  " << config->mysql.host << std::endl;
        std::cout << "    Redis:  " << config->redis.host << std::endl;
        
        // 2. 初始化日志（使用配置中的参数）
        std::cout << ">>> [2/5] 初始化日志系统..." << std::endl;
        Logger::Init(
            config->log.path,           // "./logs"
            config->log.filename,       // "user-service.log"
            config->log.level,          // "debug"
            config->log.max_size,       // 10MB
            config->log.max_files,      // 5
            config->log.console_output  // true
        );
        LOG_INFO("User Service 正在启动...");
        
        // 3. 设置信号处理
        std::cout << ">>> [3/5] 设置信号处理..." << std::endl;
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
        std::signal(SIGPIPE, SIG_IGN);
        
        // 从命令行参数覆盖端口
        if (argc > 1) {
            config->server.grpc_port = std::stoi(argv[1]);
            std::cout << "    端口已覆盖为: " << config->server.grpc_port << std::endl;
            LOG_INFO("端口已覆盖为: {}", config->server.grpc_port);
        }
        
        // 4. 创建并初始化服务器
        std::cout << ">>> [4/5] 初始化服务器..." << std::endl;
        UserServiceServer server(config);
        
        if (!server.Initialize()) {
            std::cerr << ">>> 初始化失败，退出" << std::endl;
            LOG_ERROR("服务初始化失败，退出");
            return 1;
        }
        
        // 5. 运行服务器
        std::cout << ">>> [5/5] 启动服务器..." << std::endl;
        server.Run();
        
        LOG_INFO("User Service 已正常退出");
        std::cout << "=== User Service Stopped ===" << std::endl;
        
        Logger::Shutdown();
    } catch (const std::exception& e) {
        std::cerr << "=== FATAL ERROR: " << e.what() << " ===" << std::endl;
        SPDLOG_CRITICAL("致命错误: {}", e.what());
        Logger::Shutdown();
        return 1;
    }

    return 0;
}
