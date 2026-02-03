#pragma once

#include <grpcpp/grpcpp.h>
#include "common/result.h"
#include "common/auth_type.h"

namespace user_service {

// 认证上下文
// 认证上下文（从 Token 解析出的用户信息）
struct AuthContext {
    int64_t user_id;        // 数据库 ID
    std::string user_uuid;  // UUID
    std::string mobile;     // 手机号（可选）
    UserRole role;          // 用户角色
};

// 认证器接口（可 mock）
class Authenticator {
public:
    virtual ~Authenticator() = default;

    // ============================================================================
    // gRPC 请求认证函数
    // ============================================================================

    /// @brief 从 gRPC 上下文中验证 Token 并提取用户信息
    /// @details
    ///   gRPC Metadata 说明：
    ///   - gRPC 的 metadata 等同于 HTTP/2 的 headers
    ///   - 客户端设置：metadata.insert({"authorization", "Bearer xxx"})
    ///   - 服务端读取：context->client_metadata()
    ///   - key 全部小写（gRPC 规范）
    ///
    virtual Result<AuthContext> Authenticate(::grpc::ServerContext* context) = 0;
};

}  // namespace user_service
