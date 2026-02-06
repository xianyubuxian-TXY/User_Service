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
// 信号处理
// ============================================================================

void SignalHandler(int signal) {
    g_shutdown_requested.store(true, std::memory_order_release);
    const char* msg = "\n>>> 收到关闭信号，正在优雅关闭...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

// ============================================================================
// 开发模式枚举
// ============================================================================

enum class DevMode {
    DOCKER_ASSISTED,    // Docker 辅助开发：代码在宿主机，服务在容器
    FULL_CONTAINER,     // 全容器模式：代码和服务都在容器
    LOCAL_NATIVE,       // 本地原生：所有服务都在宿主机本地安装
    CUSTOM              // 自定义配置
};

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
            std::cout << "    → 连接到 " << config_->mysql.host 
                      << ":" << config_->mysql.port 
                      << " (Docker 容器内的 MySQL)" << std::endl;
            LOG_INFO("MySQL 连接目标: {}:{} (Docker 容器)", config_->mysql.host, config_->mysql.port);
            
            mysql_pool_ = std::make_shared<MySQLPool>(
                config_,
                [](const MySQLConfig& cfg) {
                    return std::make_unique<MySQLConnection>(cfg);
                }
            );
            std::cout << "    ✓ MySQL 连接池创建成功! (连接到 Docker 容器)" << std::endl;
            LOG_INFO("MySQL 连接池创建成功 (Docker 容器)");

            // 2. 创建 Redis 客户端
            std::cout << ">>> Init [2/7]: 连接 Redis..." << std::endl;
            std::cout << "    → 连接到 " << config_->redis.host 
                      << ":" << config_->redis.port 
                      << " (Docker 容器内的 Redis)" << std::endl;
            LOG_INFO("Redis 连接目标: {}:{} (Docker 容器)", config_->redis.host, config_->redis.port);
            
            redis_client_ = std::make_shared<RedisClient>(config_->redis);
            std::cout << "    ✓ Redis 连接成功! (连接到 Docker 容器)" << std::endl;
            LOG_INFO("Redis 连接成功 (Docker 容器)");

            // 3. 创建 Repository
            std::cout << ">>> Init [3/7]: 创建数据访问层..." << std::endl;
            LOG_INFO("创建数据访问层...");
            user_db_ = std::make_shared<UserDB>(mysql_pool_);
            token_repo_ = std::make_shared<TokenRepository>(mysql_pool_);
            std::cout << "    ✓ 数据访问层创建完成" << std::endl;

            // 4. 创建基础服务
            std::cout << ">>> Init [4/7]: 创建基础服务..." << std::endl;
            LOG_INFO("创建基础服务...");
            jwt_service_ = std::make_shared<JwtService>(config_->security);
            sms_service_ = std::make_shared<SmsService>(redis_client_, config_->sms);
            std::cout << "    ✓ 基础服务创建完成" << std::endl;

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
            std::cout << "    ✓ 业务服务创建完成" << std::endl;

            // 6. 创建 Authenticator
            std::cout << ">>> Init [6/7]: 创建认证器..." << std::endl;
            LOG_INFO("创建认证器...");
            authenticator_ = std::make_shared<JwtAuthenticator>(jwt_service_);
            std::cout << "    ✓ 认证器创建完成" << std::endl;

            // 7. 创建 gRPC Handler
            std::cout << ">>> Init [7/7]: 创建 gRPC Handler..." << std::endl;
            LOG_INFO("创建 gRPC Handler...");
            auth_handler_ = std::make_unique<AuthHandler>(auth_service_);
            user_handler_ = std::make_unique<UserHandler>(user_service_, authenticator_);
            std::cout << "    ✓ gRPC Handler 创建完成" << std::endl;
            
            std::cout << "\n>>> Init: 所有服务组件初始化完成!\n" << std::endl;
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
        
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(auth_handler_.get());
        builder.RegisterService(user_handler_.get());
        grpc::EnableDefaultHealthCheckService(true);
        
        server_ = builder.BuildAndStart();
        
        if (!server_) {
            std::cerr << ">>> Run FAILED: 服务器启动失败！" << std::endl;
            LOG_ERROR("服务器启动失败！");
            return;
        }
        
        g_server_ptr = server_.get();
        
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║               User Service 启动成功                          ║" << std::endl;
        std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
        std::cout << "║  gRPC 服务: " << server_address << " (宿主机)" << std::endl;
        std::cout << "║  MySQL:     localhost:" << config_->mysql.port << " → Docker 容器 :3306" << std::endl;
        std::cout << "║  Redis:     localhost:" << config_->redis.port << " → Docker 容器 :6379" << std::endl;
        std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
        std::cout << "║  按 Ctrl+C 优雅关闭                                          ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
        std::cout << "\n";
        
        LOG_INFO("========== User Service 启动成功 ==========");
        LOG_INFO("gRPC 服务: {} (宿主机运行)", server_address);
        LOG_INFO("MySQL: localhost:{} → Docker 容器 :3306", config_->mysql.port);
        LOG_INFO("Redis: localhost:{} → Docker 容器 :6379", config_->redis.port);
        LOG_INFO("============================================");
        
        std::thread shutdown_monitor([this]() { ShutdownMonitor(); });
        
        server_->Wait();
        
        if (shutdown_monitor.joinable()) {
            shutdown_monitor.join();
        }
        
        g_server_ptr = nullptr;
        LOG_INFO("服务器已关闭");
    }

    void Shutdown() {
        g_shutdown_requested.store(true, std::memory_order_release);
    }

private:
    void ShutdownMonitor() {
        while (!g_shutdown_requested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO("检测到关闭请求，正在停止服务器...");
        
        if (server_) {
            auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
            server_->Shutdown(deadline);
        }
    }

private:
    std::shared_ptr<Config> config_;
    std::unique_ptr<grpc::Server> server_;
    std::shared_ptr<MySQLPool> mysql_pool_;
    std::shared_ptr<RedisClient> redis_client_;
    std::shared_ptr<UserDB> user_db_;
    std::shared_ptr<TokenRepository> token_repo_;
    std::shared_ptr<JwtService> jwt_service_;
    std::shared_ptr<SmsService> sms_service_;
    std::shared_ptr<AuthService> auth_service_;
    std::shared_ptr<UserService> user_service_;
    std::shared_ptr<Authenticator> authenticator_;
    std::unique_ptr<AuthHandler> auth_handler_;
    std::unique_ptr<UserHandler> user_handler_;
};

// ============================================================================
// 检测开发模式
// ============================================================================

DevMode DetectDevMode(const Config& config) {
    bool mysql_is_localhost = (config.mysql.host == "localhost" || config.mysql.host == "127.0.0.1");
    bool redis_is_localhost = (config.redis.host == "localhost" || config.redis.host == "127.0.0.1");
    bool mysql_is_container_name = (config.mysql.host == "mysql");
    bool redis_is_container_name = (config.redis.host == "redis");
    
    // Docker 辅助开发：localhost + 非标准端口（映射端口）
    if (mysql_is_localhost && redis_is_localhost) {
        if (config.mysql.port != 3306 || config.redis.port != 6379) {
            return DevMode::DOCKER_ASSISTED;
        }
        return DevMode::LOCAL_NATIVE;
    }
    
    // 全容器模式：使用容器名作为 host
    if (mysql_is_container_name && redis_is_container_name) {
        return DevMode::FULL_CONTAINER;
    }
    
    return DevMode::CUSTOM;
}

void PrintDevModeInfo(DevMode mode, const Config& config) {
    std::cout << "\n";
    
    switch (mode) {
        case DevMode::DOCKER_ASSISTED:
            std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║     🐳 Docker 辅助开发模式 (Docker-Assisted Development)     ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║                                                              ║" << std::endl;
            std::cout << "║  ┌─────────────────┐      ┌─────────────────────────────┐   ║" << std::endl;
            std::cout << "║  │    宿主机        │      │      Docker 容器             │   ║" << std::endl;
            std::cout << "║  │                 │      │                             │   ║" << std::endl;
            std::cout << "║  │  [你的代码]     │ ───► │  [MySQL] :3306              │   ║" << std::endl;
            std::cout << "║  │  user_service   │      │  [Redis] :6379              │   ║" << std::endl;
            std::cout << "║  │                 │      │                             │   ║" << std::endl;
            std::cout << "║  └─────────────────┘      └─────────────────────────────┘   ║" << std::endl;
            std::cout << "║                                                              ║" << std::endl;
            std::cout << "║  优势：                                                      ║" << std::endl;
            std::cout << "║    ✓ 无需在宿主机安装 MySQL、Redis 等第三方服务              ║" << std::endl;
            std::cout << "║    ✓ docker compose up -d 一键启动所有依赖                  ║" << std::endl;
            std::cout << "║    ✓ 代码修改后直接编译运行，无需重建镜像                    ║" << std::endl;
            std::cout << "║    ✓ 环境隔离，不污染宿主机                                  ║" << std::endl;
            std::cout << "║                                                              ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║  端口映射:                                                   ║" << std::endl;
            std::cout << "║    宿主机 localhost:" << config.mysql.port << " ──► Docker MySQL:3306" << std::endl;
            std::cout << "║    宿主机 localhost:" << config.redis.port << " ──► Docker Redis:6379" << std::endl;
            std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
            break;
            
        case DevMode::FULL_CONTAINER:
            std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║        🐳 全容器模式 (Full Container Mode)                   ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║  所有服务都在 Docker 容器内运行                              ║" << std::endl;
            std::cout << "║  使用 Docker Compose 网络，通过容器名互相访问                ║" << std::endl;
            std::cout << "║                                                              ║" << std::endl;
            std::cout << "║  MySQL: mysql:3306                                           ║" << std::endl;
            std::cout << "║  Redis: redis:6379                                           ║" << std::endl;
            std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
            break;
            
        case DevMode::LOCAL_NATIVE:
            std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║        💻 本地原生模式 (Local Native Mode)                   ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║  所有服务都在宿主机本地安装运行                              ║" << std::endl;
            std::cout << "║                                                              ║" << std::endl;
            std::cout << "║  MySQL: localhost:3306 (本地安装)                            ║" << std::endl;
            std::cout << "║  Redis: localhost:6379 (本地安装)                            ║" << std::endl;
            std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
            break;
            
        case DevMode::CUSTOM:
            std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║        🔧 自定义配置模式                                     ║" << std::endl;
            std::cout << "╠══════════════════════════════════════════════════════════════╣" << std::endl;
            std::cout << "║  MySQL: " << config.mysql.host << ":" << config.mysql.port << std::endl;
            std::cout << "║  Redis: " << config.redis.host << ":" << config.redis.port << std::endl;
            std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
            break;
    }
    
    std::cout << "\n";
}

} // namespace user_service

// ============================================================================
// main 函数
// ============================================================================

namespace {
    constexpr const char* DEFAULT_CONFIG_PATH = "../../configs/config.yaml";
}

int main(int argc, char* argv[]) {
    using namespace user_service;

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║                 User Service Starting...                     ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n";
    std::cout << "工作目录: " << std::filesystem::current_path() << std::endl;
    std::cout.flush();
    
    try {
        // 1. 确定配置文件路径
        std::string config_path = DEFAULT_CONFIG_PATH;
        
        if (const char* env_path = std::getenv("CONFIG_PATH")) {
            config_path = env_path;
            std::cout << ">>> 配置来源: 环境变量 CONFIG_PATH" << std::endl;
        } else if (argc > 2 && std::string(argv[1]) == "--config") {
            config_path = argv[2];
            std::cout << ">>> 配置来源: 命令行参数" << std::endl;
        } else {
            std::cout << ">>> 配置来源: 默认路径" << std::endl;
        }
        
        // 2. 加载配置文件
        std::cout << "\n>>> [1/5] 加载配置文件: " << config_path << std::endl;
        
        if (!std::filesystem::exists(config_path)) {
            std::cerr << ">>> ERROR: 配置文件不存在: " << config_path << std::endl;
            return 1;
        }
        
        auto config = std::make_shared<Config>(Config::LoadFromFile(config_path));
        
        // 3. 检测并显示开发模式
        DevMode dev_mode = DetectDevMode(*config);
        PrintDevModeInfo(dev_mode, *config);
        
        // 4. 初始化日志
        std::cout << ">>> [2/5] 初始化日志系统..." << std::endl;
        Logger::Init(
            config->log.path,
            config->log.filename,
            config->log.level,
            config->log.max_size,
            config->log.max_files,
            config->log.console_output
        );
        
        // 记录开发模式到日志
        switch (dev_mode) {
            case DevMode::DOCKER_ASSISTED:
                LOG_INFO("========== Docker 辅助开发模式 ==========");
                LOG_INFO("代码运行在: 宿主机");
                LOG_INFO("第三方服务: Docker 容器 (无需本地安装)");
                LOG_INFO("MySQL: localhost:{} → Docker 容器 :3306", config->mysql.port);
                LOG_INFO("Redis: localhost:{} → Docker 容器 :6379", config->redis.port);
                LOG_INFO("==========================================");
                break;
            case DevMode::FULL_CONTAINER:
                LOG_INFO("========== 全容器模式 ==========");
                LOG_INFO("所有服务都在 Docker 容器内运行");
                break;
            case DevMode::LOCAL_NATIVE:
                LOG_INFO("========== 本地原生模式 ==========");
                LOG_INFO("所有服务都在宿主机本地运行");
                break;
            case DevMode::CUSTOM:
                LOG_INFO("========== 自定义配置模式 ==========");
                break;
        }
        
        // 5. 设置信号处理
        std::cout << ">>> [3/5] 设置信号处理..." << std::endl;
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
        std::signal(SIGPIPE, SIG_IGN);
        
        // 命令行覆盖端口
        if (argc == 2) {
            try {
                int port = std::stoi(argv[1]);
                if (port > 0 && port < 65536) {
                    config->server.grpc_port = port;
                    LOG_INFO("gRPC 端口已覆盖为: {}", port);
                }
            } catch (...) {}
        }
        
        // 6. 创建并初始化服务器
        std::cout << ">>> [4/5] 初始化服务器..." << std::endl;
        UserServiceServer server(config);
        
        if (!server.Initialize()) {
            LOG_ERROR("服务初始化失败");
            Logger::Shutdown();
            return 1;
        }
        
        // 7. 运行服务器
        std::cout << ">>> [5/5] 启动服务器..." << std::endl;
        server.Run();
        
        LOG_INFO("User Service 已正常退出");
        Logger::Shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "\n=== FATAL ERROR: " << e.what() << " ===" << std::endl;
        SPDLOG_CRITICAL("致命错误: {}", e.what());
        Logger::Shutdown();
        return 1;
    }

    return 0;
}
