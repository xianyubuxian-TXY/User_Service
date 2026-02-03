#pragma once

#include <gmock/gmock.h>
#include <optional>
#include <string>

// 包含所有需要的头文件
#include "service/auth_service.h"
#include "service/user_service.h"
#include "auth/jwt_service.h"
#include "common/result.h"
#include "common/auth_type.h"       // TokenPayload, TokenPair, UserRole 等
#include "entity/user_entity.h"     // UserEntity
#include "config/config.h"          // SecurityConfig
#include "auth/authenticator.h"

namespace user_service {
namespace testing {

// ============================================================================
// Mock AuthService
// ============================================================================
class MockAuthService : public AuthService {
public:
    MockAuthService() 
        : AuthService(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}

    MOCK_METHOD(Result<int32_t>, SendVerifyCode,
                (const std::string& mobile, SmsScene scene), (override));
    
    MOCK_METHOD(Result<AuthResult>, Register,
                (const std::string& mobile, const std::string& verify_code,
                 const std::string& password, const std::string& display_name), (override));
    
    MOCK_METHOD(Result<AuthResult>, LoginByPassword,
                (const std::string& mobile, const std::string& password), (override));
    
    MOCK_METHOD(Result<AuthResult>, LoginByCode,
                (const std::string& mobile, const std::string& verify_code), (override));
    
    MOCK_METHOD(Result<TokenPair>, RefreshToken,
                (const std::string& refresh_token), (override));
    
    MOCK_METHOD(Result<void>, Logout,
                (const std::string& refresh_token), (override));
    
    MOCK_METHOD(Result<void>, ResetPassword,
                (const std::string& mobile, const std::string& verify_code,
                 const std::string& new_password), (override));
    
    MOCK_METHOD(Result<TokenValidationResult>, ValidateAccessToken,
                (const std::string& access_token), (override));
};

// ============================================================================
// Mock UserService
// ============================================================================
class MockUserService : public UserService {
public:
    MockUserService() 
        : UserService(nullptr, nullptr, nullptr, nullptr) {}

    MOCK_METHOD(Result<UserEntity>, GetCurrentUser,
                (const std::string& user_uuid), (override));
    
    MOCK_METHOD(Result<UserEntity>, UpdateUser,
                (const std::string& user_uuid, std::optional<std::string> display_name), (override));
    
    MOCK_METHOD(Result<void>, ChangePassword,
                (const std::string& user_uuid, const std::string& old_password,
                 const std::string& new_password), (override));
    
    MOCK_METHOD(Result<void>, DeleteUser,
                (const std::string& user_uuid, const std::string verify_code), (override));
    
    MOCK_METHOD(Result<UserEntity>, GetUser,
                (const std::string& user_uuid), (override));
    
    MOCK_METHOD(Result<ListUsersResult>, ListUsers,
                (std::optional<std::string> mobile_filter, std::optional<bool> disabled_filter,
                 int32_t page, int32_t page_size), (override));
};

// ============================================================================
// Mock JwtService
// ============================================================================
class MockJwtService : public JwtService {
public:
    MockJwtService() : JwtService(GetDefaultConfig()) {}

    MOCK_METHOD(Result<AccessTokenPayload>, VerifyAccessToken,
                (const std::string& token), (override));
    
    MOCK_METHOD(TokenPair, GenerateTokenPair,
                (const UserEntity& user), (override));

private:
    static const SecurityConfig& GetDefaultConfig() {
        static SecurityConfig config;
        return config;
    }
};

class MockAuthenticator : public Authenticator {
public:
    MOCK_METHOD(Result<AuthContext>, Authenticate,
                (::grpc::ServerContext* context), (override));
};

}  // namespace testing
}  // namespace user_service
