#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "user_service.pb.h"
#include "user_service.grpc.pb.h"

class UserDB;

namespace user_service{
class UserServiceImpl final: public user_service::UserService::Service{
public:


    // 用户 CRUD 接口
   ::grpc::Status CreateUser(::grpc::ServerContext* context, const ::user_service::CreateUserRequest* request, ::user_service::CreateUserResponse* response) override;
   ::grpc::Status GetUser(::grpc::ServerContext* context, const ::user_service::GetUserRequest* request, ::user_service::GetUserResponse* response) override;
   ::grpc::Status UpdateUser(::grpc::ServerContext* context, const ::user_service::UpdateUserRequest* request, ::user_service::UpdateUserResponse* response) override;
   ::grpc::Status DeleteUser(::grpc::ServerContext* context, const ::user_service::DeleteUserRequest* request, ::user_service::DeleteUserResponse* response) override;
   ::grpc::Status ListUsers(::grpc::ServerContext* context, const ::user_service::ListUsersRequest* request, ::user_service::ListUsersResponse* response) override;
   // 认证/授权接口
   ::grpc::Status Authenticate(::grpc::ServerContext* context, const ::user_service::AuthenticateRequest* request, ::user_service::AuthenticateResponse* response) override;
   ::grpc::Status ValidateToken(::grpc::ServerContext* context, const ::user_service::ValidateTokenRequest* request, ::user_service::ValidateTokenResponse* response) override;

private:
    UserDB* user_db_;
};


}