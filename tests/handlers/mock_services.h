// tests/handlers/mock_services.h

#pragma once

#include <gmock/gmock.h>
#include "service/auth_service.h"
#include "service/user_service.h"
#include "auth/authenticator.h"

namespace user_service {
namespace testing {

// ============================================================================
// Mock AuthService
// ============================================================================

inline const std::optional<std::string> kNullString = std::nullopt;
inline const std::optional<bool> kNullBool = std::nullopt;

class MockAuthService : public AuthService {
public:
    MockAuthService() 
        : AuthService(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}

    MOCK_METHOD(Result<int32_t>, SendVerifyCode, 
                (const std::string& mobile, SmsScene scene), (override));

    MOCK_METHOD(Result<AuthResult>, Register,
                (const std::string& mobile,
                 const std::string& verify_code,
                 const std::string& password,
                 const std::string& display_name), (override));

    MOCK_METHOD(Result<AuthResult>, LoginByPassword,
                (const std::string& mobile,
                 const std::string& password), (override));

    MOCK_METHOD(Result<AuthResult>, LoginByCode,
                (const std::string& mobile,
                 const std::string& verify_code), (override));

    MOCK_METHOD(Result<void>, ResetPassword,
                (const std::string& mobile,
                 const std::string& verify_code,
                 const std::string& new_password), (override));

    MOCK_METHOD(Result<TokenPair>, RefreshToken,
                (const std::string& refresh_token), (override));

    MOCK_METHOD(Result<void>, Logout,
                (const std::string& refresh_token), (override));

    MOCK_METHOD(Result<void>, LogoutAll,
                (const std::string& user_uuid), (override));

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
                (const std::string& user_uuid,
                 std::optional<std::string> display_name), (override));

    MOCK_METHOD(Result<void>, ChangePassword,
                (const std::string& user_uuid,
                 const std::string& old_password,
                 const std::string& new_password), (override));

    MOCK_METHOD(Result<void>, DeleteUser,
                (const std::string& user_uuid,
                 const std::string verify_code), (override));

    MOCK_METHOD(Result<UserEntity>, GetUser,
                (const std::string& user_uuid), (override));

    MOCK_METHOD(Result<ListUsersResult>, ListUsers,
                (std::optional<std::string> mobile_filter,
                 std::optional<bool> disabled_filter,
                 int32_t page,
                 int32_t page_size), (override));

    MOCK_METHOD(Result<void>, SetUserDisabled,
                (const std::string& user_uuid, bool disabled), (override));
};

// ============================================================================
// Mock Authenticator
// ============================================================================

class MockAuthenticator : public Authenticator {
public:
    MOCK_METHOD(Result<AuthContext>, Authenticate,
                (::grpc::ServerContext* context), (override));
};

// ============================================================================
// 测试辅助函数
// ============================================================================

// 创建测试用户实体
inline UserEntity CreateTestUser(
    int64_t id = 1,
    const std::string& uuid = "test-uuid-123",
    const std::string& mobile = "13800138000",
    const std::string& display_name = "TestUser",
    UserRole role = UserRole::User,
    bool disabled = false) 
{
    UserEntity user;
    user.id = id;
    user.uuid = uuid;
    user.mobile = mobile;
    user.display_name = display_name;
    user.role = role;
    user.disabled = disabled;
    user.created_at = "2024-01-15 10:30:00";
    user.updated_at = "2024-01-15 10:30:00";
    return user;
}

// 创建测试 TokenPair
inline TokenPair CreateTestTokenPair() {
    return TokenPair{
        .access_token = "test-access-token",
        .refresh_token = "test-refresh-token",
        .expires_in = 900
    };
}

// 创建测试 AuthResult
inline AuthResult CreateTestAuthResult(
    const UserEntity& user = CreateTestUser()) 
{
    return AuthResult{
        .user = user,
        .tokens = CreateTestTokenPair()
    };
}

// 创建测试 AuthContext
inline AuthContext CreateTestAuthContext(
    int64_t user_id = 1,
    const std::string& user_uuid = "test-uuid-123",
    const std::string& mobile = "13800138000",
    UserRole role = UserRole::User) 
{
    return AuthContext{
        .user_id = user_id,
        .user_uuid = user_uuid,
        .mobile = mobile,
        .role = role
    };
}

// 创建管理员 AuthContext
inline AuthContext CreateAdminAuthContext() {
    return AuthContext{
        .user_id = 999,
        .user_uuid = "admin-uuid",
        .mobile = "13900000000",
        .role = UserRole::Admin
    };
}

}  // namespace testing
}  // namespace user_service
