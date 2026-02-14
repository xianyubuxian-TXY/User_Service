// src/server/user_main.cpp
#include "server/server_builder.h"
#include "common/logger.h"
#include <csignal>

user_service::GrpcServer* g_server = nullptr;

void SignalHandler(int signal) {
    if (g_server) {
        g_server->Shutdown();
    }
}

int main(int argc, char* argv[]) {
    // 获取配置文件路径
    const char* config_path = std::getenv("CONFIG_PATH");
    if (!config_path) {
        config_path = "../../configs/config.yaml";
    }

    try {

        // 先加载配置
        auto config = user_service::Config::LoadFromFile(config_path);

        // 立即初始化 Logger
        user_service::Logger::Init(
            config.log.path,
            config.log.filename,
            config.log.level,
            config.log.max_size,
            config.log.max_files,
            config.log.console_output
        );

        // 构建服务器
        auto server = user_service::ServerBuilder()
            .WithConfig(std::make_shared<user_service::Config>(config))
            .LoadFromEnvironment()
            .WithServiceName("user-service")
            .WithPort(50051)
            .Build();
        
        g_server = server.get();
        
        // 注册信号处理
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
        
        // 初始化并运行
        if (!server->Initialize()) {
            std::cerr << "Failed to initialize server" << std::endl;
            return 1;
        }
        
        server->Run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
