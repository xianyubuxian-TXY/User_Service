#pragma once

#include "auth/authenticator.h"
#include "auth/jwt_service.h"
#include <memory>

namespace user_service {

class JwtAuthenticator : public Authenticator {
public:
    explicit JwtAuthenticator(std::shared_ptr<JwtService> jwt_service)
        : jwt_service_(std::move(jwt_service)) {}

    // ============================================================================
    // gRPC 请求认证函数
    // ============================================================================

    /// @brief 从 gRPC 上下文中验证 Token 并提取用户信息
    /// @details
    ///   ┌────────────────────────────────────────────────────────────────┐
    ///   │  认证流程                                                      │
    ///   ├────────────────────────────────────────────────────────────────┤
    ///   │  Client Request                                                │
    ///   │       │                                                        │
    ///   │       ▼                                                        │
    ///   │  ┌─────────────────────────────────────────┐                   │
    ///   │  │ HTTP Header                             │                   │
    ///   │  │ Authorization: Bearer eyJhbGciOi...     │                   │
    ///   │  └─────────────────────────────────────────┘                   │
    ///   │       │                                                        │
    ///   │       ▼                                                        │
    ///   │  1. 提取 metadata 中的 "authorization"                         │
    ///   │       │                                                        │
    ///   │       ▼                                                        │
    ///   │  2. 验证格式："Bearer " + token                                │
    ///   │       │                                                        │
    ///   │       ▼                                                        │
    ///   │  3. 调用 JwtService::VerifyAccessToken 验证签名和有效期        │
    ///   │       │                                                        │
    ///   │       ▼                                                        │
    ///   │  4. 返回 AuthContext（user_id, user_uuid, mobile）            │
    ///   └────────────────────────────────────────────────────────────────┘
    ///
    ///   gRPC Metadata 说明：
    ///   - gRPC 的 metadata 等同于 HTTP/2 的 headers
    ///   - 客户端设置：metadata.insert({"authorization", "Bearer xxx"})
    ///   - 服务端读取：context->client_metadata()
    ///   - key 全部小写（gRPC 规范）
    ///
    Result<AuthContext> Authenticate(::grpc::ServerContext* context) override {
        auto metadata = context->client_metadata();
        auto it = metadata.find("authorization");
        
        if (it == metadata.end()) {
            return Result<AuthContext>::Fail(ErrorCode::Unauthenticated, "缺少认证信息");
        }

        std::string auth_header(it->second.data(), it->second.size());
        
        const std::string prefix = "Bearer ";
        if (auth_header.rfind(prefix, 0) != 0) {
            return Result<AuthContext>::Fail(ErrorCode::Unauthenticated, "认证格式错误");
        }

        std::string token = auth_header.substr(prefix.length());
        if (token.empty()) {
            return Result<AuthContext>::Fail(ErrorCode::Unauthenticated, "Token 不能为空");
        }

        auto verify_result = jwt_service_->VerifyAccessToken(token);
        if (!verify_result.IsOk()) {
            return Result<AuthContext>::Fail(verify_result.code, verify_result.message);
        }

        const auto& payload = verify_result.Value();
        AuthContext auth_ctx;
        auth_ctx.user_id = payload.user_id;
        auth_ctx.user_uuid = payload.user_uuid;
        auth_ctx.mobile = payload.mobile;
        auth_ctx.role = payload.role;
        
        return Result<AuthContext>::Ok(auth_ctx);
    }

private:
    std::shared_ptr<JwtService> jwt_service_;
};

}  // namespace user_service
