// #include "auth_handler.h"
// #include "service/auth_service.h"
// #include "common/validator.h"
// #include "common/logger.h"

// AuthHandler(std::shared_ptr<AuthService> auth_service){

// }

// // 验证码
// ::grpc::Status SendVerifyCode(::grpc::ServerContext* context, 
//                                const ::auth::SendVerifyCodeRequest* request, 
//                                ::auth::SendVerifyCodeResponse* response){


// }

// // 注册
// ::grpc::Status Register(::grpc::ServerContext* context, 
//                         const ::auth::RegisterRequest* request, 
//                         ::auth::RegisterResponse* response){

                        
// }

// // 登录
// ::grpc::Status LoginByPassword(::grpc::ServerContext* context, 
//                                 const ::auth::LoginByPasswordRequest* request, 
//                                 ::auth::LoginByPasswordResponse* response){


// }

// ::grpc::Status LoginByCode(::grpc::ServerContext* context, 
//                             const ::auth::LoginByCodeRequest* request, 
//                             ::auth::LoginByCodeResponse* response){

// }

// // Token 管理
// ::grpc::Status RefreshToken(::grpc::ServerContext* context, 
//                              const ::auth::RefreshTokenRequest* request, 
//                              ::auth::RefreshTokenResponse* response){


// }

// ::grpc::Status Logout(::grpc::ServerContext* context, 
//                       const ::auth::LogoutRequest* request, 
//                       ::auth::LogoutResponse* response){

//                     }

// // 密码
// ::grpc::Status ResetPassword(::grpc::ServerContext* context, 
//                               const ::auth::ResetPasswordRequest* request, 
//                               ::auth::ResetPasswordResponse* response){


// }

// // 内部验证
// ::grpc::Status ValidateToken(::grpc::ServerContext* context, 
//                               const ::auth::ValidateTokenRequest* request, 
//                               ::auth::ValidateTokenResponse* response){


// }