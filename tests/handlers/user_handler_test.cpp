// tests/handlers/user_handler_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <grpcpp/grpcpp.h>

#include "handlers/user_handler.h"
#include "mock_services.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::Eq;

class UserHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_user_service_ = std::make_shared<MockUserService>();
        mock_authenticator_ = std::make_shared<MockAuthenticator>();
        handler_ = std::make_unique<UserHandler>(mock_user_service_, mock_authenticator_);
    }

    void TearDown() override {
        handler_.reset();
        mock_user_service_.reset();
        mock_authenticator_.reset();
    }

    std::unique_ptr<grpc::ServerContext> CreateContext() {
        return std::make_unique<grpc::ServerContext>();
    }

    // 设置认证成功的期望
    void ExpectAuthSuccess(const AuthContext& auth_ctx = CreateTestAuthContext()) {
        EXPECT_CALL(*mock_authenticator_, Authenticate(_))
            .WillOnce(Return(Result<AuthContext>::Ok(auth_ctx)));
    }

    // 设置认证失败的期望
    void ExpectAuthFailure(ErrorCode code = ErrorCode::Unauthenticated, 
                           const std::string& msg = "未认证") {
        EXPECT_CALL(*mock_authenticator_, Authenticate(_))
            .WillOnce(Return(Result<AuthContext>::Fail(code, msg)));
    }

    std::shared_ptr<MockUserService> mock_user_service_;
    std::shared_ptr<MockAuthenticator> mock_authenticator_;
    std::unique_ptr<UserHandler> handler_;
};

// ============================================================================
// GetCurrentUser 测试
// ============================================================================

TEST_F(UserHandlerTest, GetCurrentUser_Success) {
    // Arrange
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    auto context = CreateContext();

    auto auth_ctx = CreateTestAuthContext();
    ExpectAuthSuccess(auth_ctx);

    auto user = CreateTestUser();
    EXPECT_CALL(*mock_user_service_, GetCurrentUser("test-uuid-123"))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    // Act
    auto status = handler_->GetCurrentUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().id(), "test-uuid-123");
    EXPECT_EQ(response.user().mobile(), "13800138000");
    EXPECT_EQ(response.user().display_name(), "TestUser");
}

TEST_F(UserHandlerTest, GetCurrentUser_Unauthenticated) {
    // Arrange
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    auto context = CreateContext();

    ExpectAuthFailure(ErrorCode::Unauthenticated, "缺少认证信息");

    // Act
    auto status = handler_->GetCurrentUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

TEST_F(UserHandlerTest, GetCurrentUser_TokenExpired) {
    // Arrange
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    auto context = CreateContext();

    ExpectAuthFailure(ErrorCode::TokenExpired, "Token 已过期");

    // Act
    auto status = handler_->GetCurrentUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_EXPIRED);
}

TEST_F(UserHandlerTest, GetCurrentUser_UserNotFound) {
    // Arrange
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    EXPECT_CALL(*mock_user_service_, GetCurrentUser(_))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserNotFound, "用户不存在")));

    // Act
    auto status = handler_->GetCurrentUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::USER_NOT_FOUND);
}

// ============================================================================
// UpdateUser 测试
// ============================================================================

TEST_F(UserHandlerTest, UpdateUser_Success) {
    // Arrange
    pb_user::UpdateUserRequest request;
    request.mutable_display_name()->set_value("NewName");

    pb_user::UpdateUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    auto updated_user = CreateTestUser();
    updated_user.display_name = "NewName";
    
    EXPECT_CALL(*mock_user_service_, UpdateUser("test-uuid-123", std::optional<std::string>("NewName")))
        .WillOnce(Return(Result<UserEntity>::Ok(updated_user)));

    // Act
    auto status = handler_->UpdateUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().display_name(), "NewName");
}

TEST_F(UserHandlerTest, UpdateUser_Unauthenticated) {
    // Arrange
    pb_user::UpdateUserRequest request;
    request.mutable_display_name()->set_value("NewName");

    pb_user::UpdateUserResponse response;
    auto context = CreateContext();

    ExpectAuthFailure();

    // Act
    auto status = handler_->UpdateUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

TEST_F(UserHandlerTest, UpdateUser_NoChanges) {
    // Arrange
    pb_user::UpdateUserRequest request;  // 不设置任何字段

    pb_user::UpdateUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    auto user = CreateTestUser();
    // 修复：使用显式类型的 nullopt
    EXPECT_CALL(*mock_user_service_, UpdateUser("test-uuid-123", kNullString))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    // Act
    auto status = handler_->UpdateUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(UserHandlerTest, UpdateUser_UserDisabled) {
    // Arrange
    pb_user::UpdateUserRequest request;
    request.mutable_display_name()->set_value("NewName");

    pb_user::UpdateUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    EXPECT_CALL(*mock_user_service_, UpdateUser(_, _))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserDisabled, "账号已禁用")));

    // Act
    auto status = handler_->UpdateUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::USER_DISABLED);
}

// ============================================================================
// ChangePassword 测试
// ============================================================================

TEST_F(UserHandlerTest, ChangePassword_Success) {
    // Arrange
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123");
    request.set_new_password("NewPassword123");

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    EXPECT_CALL(*mock_user_service_, ChangePassword("test-uuid-123", "OldPassword123", "NewPassword123"))
        .WillOnce(Return(Result<void>::Ok()));

    // Act
    auto status = handler_->ChangePassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(UserHandlerTest, ChangePassword_Unauthenticated) {
    // Arrange
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123");
    request.set_new_password("NewPassword123");

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    ExpectAuthFailure();

    // Act
    auto status = handler_->ChangePassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

TEST_F(UserHandlerTest, ChangePassword_EmptyOldPassword) {
    // Arrange
    pb_user::ChangePasswordRequest request;
    request.set_old_password("");
    request.set_new_password("NewPassword123");

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    // Act
    auto status = handler_->ChangePassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(UserHandlerTest, ChangePassword_EmptyNewPassword) {
    // Arrange
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123");
    request.set_new_password("");

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    // Act
    auto status = handler_->ChangePassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(UserHandlerTest, ChangePassword_WeakNewPassword) {
    // Arrange
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123");
    request.set_new_password("123");  // 太短

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    // Act
    auto status = handler_->ChangePassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(UserHandlerTest, ChangePassword_WrongOldPassword) {
    // Arrange
    pb_user::ChangePasswordRequest request;
    request.set_old_password("WrongPassword");
    request.set_new_password("NewPassword123");

    pb_user::ChangePasswordResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    EXPECT_CALL(*mock_user_service_, ChangePassword(_, _, _))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::WrongPassword, "旧密码错误")));

    // Act
    auto status = handler_->ChangePassword(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::WRONG_PASSWORD);
}

// ============================================================================
// DeleteUser 测试
// ============================================================================

TEST_F(UserHandlerTest, DeleteUser_Success) {
    // Arrange
    pb_user::DeleteUserRequest request;
    request.set_verify_code("123456");

    pb_user::DeleteUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    EXPECT_CALL(*mock_user_service_, DeleteUser("test-uuid-123", "123456"))
        .WillOnce(Return(Result<void>::Ok()));

    // Act
    auto status = handler_->DeleteUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(UserHandlerTest, DeleteUser_Unauthenticated) {
    // Arrange
    pb_user::DeleteUserRequest request;
    request.set_verify_code("123456");

    pb_user::DeleteUserResponse response;
    auto context = CreateContext();

    ExpectAuthFailure();

    // Act
    auto status = handler_->DeleteUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

TEST_F(UserHandlerTest, DeleteUser_InvalidVerifyCode) {
    // Arrange
    pb_user::DeleteUserRequest request;
    request.set_verify_code("12345");  // 长度不对

    pb_user::DeleteUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    // Act
    auto status = handler_->DeleteUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(UserHandlerTest, DeleteUser_WrongVerifyCode) {
    // Arrange
    pb_user::DeleteUserRequest request;
    request.set_verify_code("000000");

    pb_user::DeleteUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess();

    EXPECT_CALL(*mock_user_service_, DeleteUser(_, _))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::CaptchaWrong, "验证码错误")));

    // Act
    auto status = handler_->DeleteUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::CAPTCHA_WRONG);
}

// ============================================================================
// GetUser 测试（管理员接口）
// ============================================================================

TEST_F(UserHandlerTest, GetUser_Success) {
    // Arrange
    pb_user::GetUserRequest request;
    request.set_id("target-user-uuid");

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    auto admin_ctx = CreateAdminAuthContext();
    ExpectAuthSuccess(admin_ctx);

    auto user = CreateTestUser(2, "target-user-uuid", "13900000001", "TargetUser");
    EXPECT_CALL(*mock_user_service_, GetUser("target-user-uuid"))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    // Act
    auto status = handler_->GetUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().id(), "target-user-uuid");
    EXPECT_EQ(response.user().display_name(), "TargetUser");
}

TEST_F(UserHandlerTest, GetUser_Unauthenticated) {
    // Arrange
    pb_user::GetUserRequest request;
    request.set_id("target-user-uuid");

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    ExpectAuthFailure();

    // Act
    auto status = handler_->GetUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

TEST_F(UserHandlerTest, GetUser_NotAdmin) {
    // Arrange
    pb_user::GetUserRequest request;
    request.set_id("target-user-uuid");

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    // 普通用户尝试访问管理员接口
    auto user_ctx = CreateTestAuthContext();
    user_ctx.role = UserRole::User;
    ExpectAuthSuccess(user_ctx);

    // Act
    auto status = handler_->GetUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::ADMIN_REQUIRED);
}

TEST_F(UserHandlerTest, GetUser_EmptyId) {
    // Arrange
    pb_user::GetUserRequest request;
    request.set_id("");

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    // Act
    auto status = handler_->GetUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::INVALID_ARGUMENT);
}

TEST_F(UserHandlerTest, GetUser_UserNotFound) {
    // Arrange
    pb_user::GetUserRequest request;
    request.set_id("non-existent-uuid");

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    EXPECT_CALL(*mock_user_service_, GetUser("non-existent-uuid"))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserNotFound, "用户不存在")));

    // Act
    auto status = handler_->GetUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::USER_NOT_FOUND);
}

// ============================================================================
// ListUsers 测试（管理员接口）
// ============================================================================

TEST_F(UserHandlerTest, ListUsers_Success) {
    // Arrange
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(1);
    request.mutable_page()->set_page_size(10);

    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    ListUsersResult list_result;
    list_result.users = {
        CreateTestUser(1, "uuid-1", "13800000001", "User1"),
        CreateTestUser(2, "uuid-2", "13800000002", "User2"),
        CreateTestUser(3, "uuid-3", "13800000003", "User3")
    };
    list_result.page_res = PageResult::Create(1, 10, 3);

    // 修复：使用显式类型的 nullopt 常量
    EXPECT_CALL(*mock_user_service_, ListUsers(kNullString, kNullBool, 1, 10))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.users_size(), 3);
    EXPECT_EQ(response.users(0).id(), "uuid-1");
    EXPECT_EQ(response.users(1).id(), "uuid-2");
    EXPECT_EQ(response.users(2).id(), "uuid-3");
    EXPECT_EQ(response.page_info().total_records(), 3);
    EXPECT_EQ(response.page_info().total_pages(), 1);
    EXPECT_EQ(response.page_info().page(), 1);
    EXPECT_EQ(response.page_info().page_size(), 10);
}

TEST_F(UserHandlerTest, ListUsers_WithMobileFilter) {
    // Arrange
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(1);
    request.mutable_page()->set_page_size(20);
    request.set_mobile_filter("138");

    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    ListUsersResult list_result;
    list_result.users = {CreateTestUser(1, "uuid-1", "13800000001", "User1")};
    list_result.page_res = PageResult::Create(1, 20, 1);

    // 修复：第二个参数使用显式类型的 nullopt 常量
    EXPECT_CALL(*mock_user_service_, ListUsers(
        std::optional<std::string>("138"), kNullBool, 1, 20))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.users_size(), 1);
}

TEST_F(UserHandlerTest, ListUsers_WithDisabledFilter) {
    // Arrange
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(1);
    request.mutable_page()->set_page_size(20);
    request.mutable_disabled_filter()->set_value(true);

    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    ListUsersResult list_result;
    list_result.users = {};
    list_result.page_res = PageResult::Create(1, 20, 0);

    // 修复：第一个参数使用显式类型的 nullopt 常量
    EXPECT_CALL(*mock_user_service_, ListUsers(
        kNullString, std::optional<bool>(true), 1, 20))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.users_size(), 0);
    EXPECT_EQ(response.page_info().total_records(), 0);
}

TEST_F(UserHandlerTest, ListUsers_Unauthenticated) {
    // Arrange
    pb_user::ListUsersRequest request;
    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    ExpectAuthFailure();

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

TEST_F(UserHandlerTest, ListUsers_NotAdmin) {
    // Arrange
    pb_user::ListUsersRequest request;
    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    auto user_ctx = CreateTestAuthContext();
    user_ctx.role = UserRole::User;
    ExpectAuthSuccess(user_ctx);

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::ADMIN_REQUIRED);
}

TEST_F(UserHandlerTest, ListUsers_DefaultPagination) {
    // Arrange
    pb_user::ListUsersRequest request;  // 不设置分页参数

    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    ListUsersResult list_result;
    list_result.users = {};
    list_result.page_res = PageResult::Create(1, 20, 0);

    // 修复：使用显式类型的 nullopt 常量
    EXPECT_CALL(*mock_user_service_, ListUsers(kNullString, kNullBool, 1, 20))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(UserHandlerTest, ListUsers_Pagination) {
    // Arrange
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(2);
    request.mutable_page()->set_page_size(5);

    pb_user::ListUsersResponse response;
    auto context = CreateContext();

    ExpectAuthSuccess(CreateAdminAuthContext());

    ListUsersResult list_result;
    list_result.users = {
        CreateTestUser(6, "uuid-6", "13800000006", "User6"),
        CreateTestUser(7, "uuid-7", "13800000007", "User7")
    };
    list_result.page_res = PageResult::Create(2, 5, 7);

    // 修复：使用显式类型的 nullopt 常量
    EXPECT_CALL(*mock_user_service_, ListUsers(kNullString, kNullBool, 2, 5))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    // Act
    auto status = handler_->ListUsers(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.users_size(), 2);
    EXPECT_EQ(response.page_info().page(), 2);
    EXPECT_EQ(response.page_info().page_size(), 5);
    EXPECT_EQ(response.page_info().total_records(), 7);
    EXPECT_EQ(response.page_info().total_pages(), 2);
}

// ============================================================================
// 超级管理员权限测试
// ============================================================================

TEST_F(UserHandlerTest, GetUser_SuperAdminCanAccess) {
    // Arrange
    pb_user::GetUserRequest request;
    request.set_id("target-user-uuid");

    pb_user::GetUserResponse response;
    auto context = CreateContext();

    AuthContext super_admin_ctx;
    super_admin_ctx.user_id = 1;
    super_admin_ctx.user_uuid = "super-admin-uuid";
    super_admin_ctx.role = UserRole::SuperAdmin;
    ExpectAuthSuccess(super_admin_ctx);

    auto user = CreateTestUser();
    EXPECT_CALL(*mock_user_service_, GetUser(_))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    // Act
    auto status = handler_->GetUser(context.get(), &request, &response);

    // Assert
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}
