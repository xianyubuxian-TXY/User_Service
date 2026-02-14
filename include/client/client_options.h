#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <grpcpp/grpcpp.h>

namespace user_service {

/**
 * @brief 客户端配置选项
 */
struct ClientOptions {
    // 连接目标
    std::string target;                             // 服务地址，如 "localhost:50051"
    
    // 超时设置
    std::chrono::milliseconds timeout{5000};        // 默认超时 5 秒
    std::chrono::milliseconds connect_timeout{3000}; // 连接超时 3 秒
    
    // 重试策略
    int max_retries = 3;                            // 最大重试次数
    std::chrono::milliseconds retry_interval{100};  // 重试间隔
    
    // 认证
    std::string access_token;                       // Bearer Token（可选）
    
    // TLS 配置
    bool use_tls = false;
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
    
    // 连接池
    int max_connections = 10;                       // 最大连接数
    
    /**
     * @brief 创建 gRPC Channel Credentials
     */
    std::shared_ptr<grpc::ChannelCredentials> CreateCredentials() const {
        if (use_tls && !ca_cert_path.empty()) {
            grpc::SslCredentialsOptions ssl_opts;
            // 读取证书文件... (略)
            return grpc::SslCredentials(ssl_opts);
        }
        return grpc::InsecureChannelCredentials();
    }
    
    /**
     * @brief 创建 gRPC Channel Arguments
     */
    grpc::ChannelArguments CreateChannelArgs() const {
        grpc::ChannelArguments args;
        args.SetMaxReceiveMessageSize(4 * 1024 * 1024);  // 4MB
        args.SetMaxSendMessageSize(4 * 1024 * 1024);
        return args;
    }
};

} // namespace user_service
