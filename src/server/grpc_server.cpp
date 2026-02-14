#include "server/grpc_server.h"
#include "common/logger.h"
#include "auth/jwt_authenticator.h"
#include "auth/token_cleanup_task.h"
#include "pool/connection_pool.h"

#include <csignal>
#include <iostream>

namespace user_service {

// ============================================================================
// 构造与析构
// ============================================================================

GrpcServer::GrpcServer(std::shared_ptr<Config> config)
    : config_(std::move(config)) {
    if (!config_) {
        throw std::invalid_argument("Config cannot be null");
    }
}

GrpcServer::~GrpcServer() {
    Shutdown();
}

// ============================================================================
// 初始化
// ============================================================================

bool GrpcServer::Initialize() {
    if (!Logger::IsInitialized()) {
        Logger::Init(
            config_->log.path,
            config_->log.filename,
            config_->log.level,
            config_->log.max_size,
            config_->log.max_files,
            config_->log.console_output
        );
        LOG_INFO("Logger initialized by GrpcServer (fallback)");
    }

    LOG_INFO("Initializing gRPC server...");

    try {
        if (!InitInfrastructure()) {
            LOG_ERROR("Failed to initialize infrastructure");
            return false;
        }

        if (!InitRepositories()) {
            LOG_ERROR("Failed to initialize repositories");
            return false;
        }

        if (!InitServices()) {
            LOG_ERROR("Failed to initialize services");
            return false;
        }

        if (!InitHandlers()) {
            LOG_ERROR("Failed to initialize handlers");
            return false;
        }

        if (config_->zookeeper.enabled) {
            if (!InitServiceDiscovery()) {
                LOG_WARN("Failed to initialize service discovery, continuing without it");
            }
        }

        LOG_INFO("gRPC server initialized successfully");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Initialization failed: {}", e.what());
        return false;
    }
}

bool GrpcServer::InitInfrastructure() {
    LOG_INFO("Initializing infrastructure...");

    // MySQL 连接池
    LOG_INFO("Creating MySQL connection pool: {}:{}", 
             config_->mysql.host, config_->mysql.port);
    
    mysql_pool_ = std::make_shared<MySQLPool>(
        config_,
        [](const MySQLConfig& cfg) {
            return std::make_unique<MySQLConnection>(cfg);
        }
    );

    // Redis 客户端
    LOG_INFO("Connecting to Redis: {}:{}", 
             config_->redis.host, config_->redis.port);
    
    redis_client_ = std::make_shared<RedisClient>(config_->redis);

    // 验证连接
    auto ping_result = redis_client_->Ping();
    if (!ping_result.IsOk()) {
        LOG_ERROR("Redis ping failed: {}", ping_result.message);
        return false;
    }

    LOG_INFO("Infrastructure initialized");
    return true;
}

bool GrpcServer::InitRepositories() {
    LOG_INFO("Initializing repositories...");

    user_db_ = std::make_shared<UserDB>(mysql_pool_);
    token_repo_ = std::make_shared<TokenRepository>(mysql_pool_);

    LOG_INFO("Repositories initialized");
    return true;
}

bool GrpcServer::InitServices() {
    LOG_INFO("Initializing services...");

    // 基础服务
    jwt_service_ = std::make_shared<JwtService>(config_->security);
    sms_service_ = std::make_shared<SmsService>(redis_client_, config_->sms);

    // 业务服务
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

    // Token 清理任务
    token_cleanup_task_ = std::make_shared<TokenCleanupTask>(token_repo_, 60);
    token_cleanup_task_->Start();

    LOG_INFO("Services initialized");
    return true;
}

bool GrpcServer::InitHandlers() {
    LOG_INFO("Initializing handlers...");

    authenticator_ = std::make_shared<JwtAuthenticator>(jwt_service_);
    auth_handler_ = std::make_unique<AuthHandler>(auth_service_);
    user_handler_ = std::make_unique<UserHandler>(user_service_, authenticator_);

    LOG_INFO("Handlers initialized");
    return true;
}

bool GrpcServer::InitServiceDiscovery() {
    LOG_INFO("Initializing service discovery...");

    try {
        zk_client_ = std::make_shared<ZooKeeperClient>(
            config_->zookeeper.hosts,
            config_->zookeeper.session_timeout_ms
        );

        if (!zk_client_->Connect(config_->zookeeper.connect_timeout_ms)) {
            LOG_ERROR("Failed to connect to ZooKeeper");
            return false;
        }

        service_registry_ = std::make_shared<ServiceRegistry>(
            zk_client_,
            config_->zookeeper.root_path
        );

        LOG_INFO("Service discovery initialized");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Service discovery initialization failed: {}", e.what());
        return false;
    }
}

// ============================================================================
// 运行
// ============================================================================

void GrpcServer::Run() {
    if (!Start()) {
        LOG_ERROR("Failed to start server");
        return;
    }
    Wait();
}

bool GrpcServer::Start() {
    if (running_.load()) {
        LOG_WARN("Server is already running");
        return true;
    }

    std::string address = config_->server.host + ":" + 
                          std::to_string(config_->server.grpc_port);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(auth_handler_.get());
    builder.RegisterService(user_handler_.get());

    // 启用健康检查
    grpc::EnableDefaultHealthCheckService(true);

    // 构建并启动服务器
    server_ = builder.BuildAndStart();

    if (!server_) {
        LOG_ERROR("Failed to start gRPC server on {}", address);
        return false;
    }

    running_.store(true);
    shutdown_requested_.store(false);

    // 注册到 ZooKeeper
    if (config_->zookeeper.enabled && config_->zookeeper.register_self) {
        RegisterToZooKeeper();
    }

    // 启动关闭监控线程
    shutdown_monitor_thread_ = std::thread([this]() {
        ShutdownMonitor();
    });

    LOG_INFO("========================================");
    LOG_INFO("gRPC Server started on {}", address);
    LOG_INFO("========================================");

    return true;
}

void GrpcServer::Wait() {
    if (server_) {
        server_->Wait();
    }

    if (shutdown_monitor_thread_.joinable()) {
        shutdown_monitor_thread_.join();
    }

    running_.store(false);
    LOG_INFO("Server stopped");
}

void GrpcServer::Shutdown(std::chrono::milliseconds deadline) {
    if (!running_.load()) {
        return;
    }

    LOG_INFO("Shutting down server...");
    shutdown_requested_.store(true);

    // 从 ZooKeeper 注销
    UnregisterFromZooKeeper();

    // 停止 Token 清理任务
    if (token_cleanup_task_) {
        token_cleanup_task_->Stop();
    }

    // 关闭 gRPC 服务器
    if (server_) {
        auto deadline_time = std::chrono::system_clock::now() + deadline;
        server_->Shutdown(deadline_time);
    }

    // 触发回调
    if (shutdown_callback_) {
        shutdown_callback_();
    }
}

std::string GrpcServer::GetAddress() const {
    return config_->server.host + ":" + std::to_string(config_->server.grpc_port);
}

// ============================================================================
// ZooKeeper
// ============================================================================

bool GrpcServer::RegisterToZooKeeper() {
    if (!service_registry_) {
        return false;
    }

    ServiceInstance instance;
    instance.service_name = config_->zookeeper.service_name;
    instance.host = config_->server.host;
    instance.port = config_->server.grpc_port;
    instance.weight = config_->zookeeper.weight;
    instance.metadata["version"] = config_->zookeeper.version;
    instance.metadata["region"] = config_->zookeeper.region;
    instance.metadata["zone"] = config_->zookeeper.zone;

    if (service_registry_->Register(instance)) {
        LOG_INFO("Registered to ZooKeeper: {}", instance.GetAddress());
        return true;
    }

    LOG_ERROR("Failed to register to ZooKeeper");
    return false;
}

void GrpcServer::UnregisterFromZooKeeper() {
    if (service_registry_ && service_registry_->IsRegistered()) {
        service_registry_->Unregister();
        LOG_INFO("Unregistered from ZooKeeper");
    }
}

void GrpcServer::ShutdownMonitor() {
    while (!shutdown_requested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (server_ && running_.load()) {
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
        server_->Shutdown(deadline);
    }
}

} // namespace user_service
