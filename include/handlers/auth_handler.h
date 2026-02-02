#pragma once

#include <memory>
#include "pb_auth/auth.grpc.pb.h"

namespace user_service {

// 前向声明
class AuthService;

class AuthHandler final : public ::auth::AuthService::Service {
public:
    explicit AuthHandler(std::shared_ptr<AuthService> auth_service);

    // 验证码
    ::grpc::Status SendVerifyCode(::grpc::ServerContext* context, 
                                   const ::auth::SendVerifyCodeRequest* request, 
                                   ::auth::SendVerifyCodeResponse* response) override;
    
    // 注册
    ::grpc::Status Register(::grpc::ServerContext* context, 
                            const ::auth::RegisterRequest* request, 
                            ::auth::RegisterResponse* response) override;
    
    // 登录
    ::grpc::Status LoginByPassword(::grpc::ServerContext* context, 
                                    const ::auth::LoginByPasswordRequest* request, 
                                    ::auth::LoginByPasswordResponse* response) override;
    
    ::grpc::Status LoginByCode(::grpc::ServerContext* context, 
                                const ::auth::LoginByCodeRequest* request, 
                                ::auth::LoginByCodeResponse* response) override;
    
    // Token 管理
    ::grpc::Status RefreshToken(::grpc::ServerContext* context, 
                                 const ::auth::RefreshTokenRequest* request, 
                                 ::auth::RefreshTokenResponse* response) override;
    
    ::grpc::Status Logout(::grpc::ServerContext* context, 
                          const ::auth::LogoutRequest* request, 
                          ::auth::LogoutResponse* response) override;
    
    // 密码
    ::grpc::Status ResetPassword(::grpc::ServerContext* context, 
                                  const ::auth::ResetPasswordRequest* request, 
                                  ::auth::ResetPasswordResponse* response) override;
    
    // 内部验证
    ::grpc::Status ValidateToken(::grpc::ServerContext* context, 
                                  const ::auth::ValidateTokenRequest* request, 
                                  ::auth::ValidateTokenResponse* response) override;

private:
    std::shared_ptr<AuthService> auth_service_;
};

}  // namespace user_service
