#include "user_service_impl.h"

#include "db/user_db.h"

namespace user_service{

::grpc::Status UserServiceImpl::CreateUser(::grpc::ServerContext* context, 
                                            const ::user_service::CreateUserRequest* request, 
                                            ::user_service::CreateUserResponse* response){
    user_service::User user;
    



}





}