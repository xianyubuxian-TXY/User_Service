#pragma once

#include <memory>
#include "pb_auth/auth.grpc.pb.h"
#include "service/auth_service.h"

namespace user_service {

// 前向声明
class AuthService;

class AuthHandler final : public ::pb_auth::AuthService::Service {
public:
    explicit AuthHandler(std::shared_ptr<AuthService> auth_service);

    // 验证码
    ::grpc::Status SendVerifyCode(::grpc::ServerContext* context, 
                                   const ::pb_auth::SendVerifyCodeRequest* request, 
                                   ::pb_auth::SendVerifyCodeResponse* response) override;
    
    // 注册
    ::grpc::Status Register(::grpc::ServerContext* context, 
                            const ::pb_auth::RegisterRequest* request, 
                            ::pb_auth::RegisterResponse* response) override;
    
    // 登录
    ::grpc::Status LoginByPassword(::grpc::ServerContext* context, 
                                    const ::pb_auth::LoginByPasswordRequest* request, 
                                    ::pb_auth::LoginByPasswordResponse* response) override;
    
    ::grpc::Status LoginByCode(::grpc::ServerContext* context, 
                                const ::pb_auth::LoginByCodeRequest* request, 
                                ::pb_auth::LoginByCodeResponse* response) override;
    
    // Token 管理
    ::grpc::Status RefreshToken(::grpc::ServerContext* context, 
                                 const ::pb_auth::RefreshTokenRequest* request, 
                                 ::pb_auth::RefreshTokenResponse* response) override;
    
    ::grpc::Status Logout(::grpc::ServerContext* context, 
                          const ::pb_auth::LogoutRequest* request, 
                          ::pb_auth::LogoutResponse* response) override;
    
    // 密码
    ::grpc::Status ResetPassword(::grpc::ServerContext* context, 
                                  const ::pb_auth::ResetPasswordRequest* request, 
                                  ::pb_auth::ResetPasswordResponse* response) override;
    
    // 内部验证
    ::grpc::Status ValidateToken(::grpc::ServerContext* context, 
                                  const ::pb_auth::ValidateTokenRequest* request, 
                                  ::pb_auth::ValidateTokenResponse* response) override;

private:
    std::shared_ptr<AuthService> auth_service_;
};

}  // namespace user_service
