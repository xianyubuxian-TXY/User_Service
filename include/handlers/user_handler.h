#pragma once

#include <memory>
#include "pb_user/user.grpc.pb.h"
#include "service/user_service.h"
#include "auth/jwt_service.h"  // 新增
#include "auth/jwt_authenticator.h"

namespace user_service {

class UserHandler final : public ::pb_user::UserService::Service {
public:
    UserHandler(std::shared_ptr<UserService> user_service,
        std::shared_ptr<Authenticator> authenticator);

    ::grpc::Status GetCurrentUser(::grpc::ServerContext* context, 
                                   const ::pb_user::GetCurrentUserRequest* request, 
                                   ::pb_user::GetCurrentUserResponse* response) override;
    ::grpc::Status UpdateUser(::grpc::ServerContext* context, 
                               const ::pb_user::UpdateUserRequest* request, 
                               ::pb_user::UpdateUserResponse* response) override;
    ::grpc::Status ChangePassword(::grpc::ServerContext* context, 
                                   const ::pb_user::ChangePasswordRequest* request, 
                                   ::pb_user::ChangePasswordResponse* response) override;
    ::grpc::Status DeleteUser(::grpc::ServerContext* context, 
                               const ::pb_user::DeleteUserRequest* request, 
                               ::pb_user::DeleteUserResponse* response) override;
    ::grpc::Status GetUser(::grpc::ServerContext* context, 
                            const ::pb_user::GetUserRequest* request, 
                            ::pb_user::GetUserResponse* response) override;
    ::grpc::Status ListUsers(::grpc::ServerContext* context, 
                              const ::pb_user::ListUsersRequest* request, 
                              ::pb_user::ListUsersResponse* response) override;

private:
    std::shared_ptr<UserService> user_service_;
    std::shared_ptr<Authenticator> authenticator_;
};

}  // namespace user_service
