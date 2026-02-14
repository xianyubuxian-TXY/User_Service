#include "server/server_builder.h"
#include "common/logger.h"
#include <stdexcept>

namespace user_service {

ServerBuilder& ServerBuilder::WithConfigFile(const std::string& path) {
    try {
        config_ = std::make_shared<Config>(Config::LoadFromFile(path));
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load config from " + path + ": " + e.what());
    }
    return *this;
}

ServerBuilder& ServerBuilder::WithConfig(std::shared_ptr<Config> config) {
    config_ = std::move(config);
    return *this;
}

ServerBuilder& ServerBuilder::WithPort(int port) {
    port_override_ = port;
    return *this;
}

ServerBuilder& ServerBuilder::WithHost(const std::string& host) {
    host_override_ = host;
    return *this;
}

ServerBuilder& ServerBuilder::EnableServiceDiscovery(bool enable) {
    service_discovery_override_ = enable;
    return *this;
}

ServerBuilder& ServerBuilder::WithServiceName(const std::string& name) {
    service_name_override_ = name;
    return *this;
}

ServerBuilder& ServerBuilder::OnShutdown(GrpcServer::ShutdownCallback callback) {
    shutdown_callback_ = std::move(callback);
    return *this;
}

ServerBuilder& ServerBuilder::LoadFromEnvironment() {
    load_env_ = true;
    return *this;
}

std::unique_ptr<GrpcServer> ServerBuilder::Build() {
    if (!config_) {
        throw std::runtime_error("Config is required. Call WithConfigFile() or WithConfig()");
    }

    // 从环境变量加载
    if (load_env_) {
        config_->LoadFromEnv();
    }

    // 应用覆盖值
    if (port_override_.has_value()) {
        config_->server.grpc_port = port_override_.value();
    }

    if (host_override_.has_value()) {
        config_->server.host = host_override_.value();
    }

    if (service_discovery_override_.has_value()) {
        config_->zookeeper.enabled = service_discovery_override_.value();
    }

    if (service_name_override_.has_value()) {
        config_->zookeeper.service_name = service_name_override_.value();
    }

    // 创建服务器
    auto server = std::make_unique<GrpcServer>(config_);

    if (shutdown_callback_) {
        server->SetShutdownCallback(std::move(shutdown_callback_));
    }

    return server;
}

} // namespace user_service
