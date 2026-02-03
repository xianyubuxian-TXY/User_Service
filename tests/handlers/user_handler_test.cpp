#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "handlers/user_handler.h"
#include "mock_services.h"
#include "common/error_codes.h"
#include "common/auth_type.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;

class UserHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_user_service_ = std::make_shared<MockUserService>();
        mock_authenticator_ = std::make_shared<MockAuthenticator>();  // 改这里
        handler_ = std::make_unique<UserHandler>(mock_user_service_, mock_authenticator_);
    }

    // 设置认证成功
    void SetupAuthSuccess(UserRole role = UserRole::User) {
        AuthContext auth_ctx;
        auth_ctx.user_id = 123;
        auth_ctx.user_uuid = "test-uuid-123";
        auth_ctx.mobile = "13800138000";
        auth_ctx.role = role;

        EXPECT_CALL(*mock_authenticator_, Authenticate(_))
            .WillOnce(Return(Result<AuthContext>::Ok(auth_ctx)));
    }

    // 设置认证失败
    void SetupAuthFailed(ErrorCode code = ErrorCode::Unauthenticated,
                          const std::string& msg = "") {
        EXPECT_CALL(*mock_authenticator_, Authenticate(_))
            .WillOnce(Return(Result<AuthContext>::Fail(code, msg)));
    }

    UserEntity MakeUser(const std::string& uuid = "test-uuid-123",
                        const std::string& mobile = "13800138000") {
        UserEntity user;
        user.id = 123;
        user.uuid = uuid;
        user.mobile = mobile;
        user.display_name = "测试用户";
        user.role = UserRole::User;
        user.disabled = false;
        return user;
    }

    std::shared_ptr<MockUserService> mock_user_service_;
    std::shared_ptr<MockAuthenticator> mock_authenticator_;  // 改这里
    std::unique_ptr<UserHandler> handler_;
};

// ============================================================================
// GetCurrentUser 测试
// ============================================================================

TEST_F(UserHandlerTest, GetCurrentUser_Success) {
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;

    SetupAuthSuccess();

    auto user = MakeUser();
    EXPECT_CALL(*mock_user_service_, GetCurrentUser("test-uuid-123"))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    grpc::ServerContext context;
    auto status = handler_->GetCurrentUser(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user().id(), "test-uuid-123");
    EXPECT_EQ(response.user().mobile(), "13800138000");
}

TEST_F(UserHandlerTest, GetCurrentUser_Unauthenticated_NoToken) {
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;

    SetupAuthFailed(ErrorCode::Unauthenticated, "缺少认证信息");

    EXPECT_CALL(*mock_user_service_, GetCurrentUser(_)).Times(0);

    grpc::ServerContext context;
    handler_->GetCurrentUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::UNAUTHENTICATED));
}

TEST_F(UserHandlerTest, GetCurrentUser_TokenExpired) {
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;

    SetupAuthFailed(ErrorCode::TokenExpired);

    EXPECT_CALL(*mock_user_service_, GetCurrentUser(_)).Times(0);

    grpc::ServerContext context;
    handler_->GetCurrentUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::TOKEN_EXPIRED));
}

TEST_F(UserHandlerTest, GetCurrentUser_UserNotFound) {
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, GetCurrentUser("test-uuid-123"))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserNotFound)));

    grpc::ServerContext context;
    handler_->GetCurrentUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::USER_NOT_FOUND));
}

// ============================================================================
// UpdateUser 测试
// ============================================================================

TEST_F(UserHandlerTest, UpdateUser_Success) {
    pb_user::UpdateUserRequest request;
    request.mutable_display_name()->set_value("新昵称");

    pb_user::UpdateUserResponse response;

    SetupAuthSuccess();

    auto updated_user = MakeUser();
    updated_user.display_name = "新昵称";

    EXPECT_CALL(*mock_user_service_, 
                UpdateUser("test-uuid-123", std::optional<std::string>("新昵称")))
        .WillOnce(Return(Result<UserEntity>::Ok(updated_user)));

    grpc::ServerContext context;
    handler_->UpdateUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user().display_name(), "新昵称");
}

TEST_F(UserHandlerTest, UpdateUser_NoFields) {
    pb_user::UpdateUserRequest request;
    // 不设置任何字段

    pb_user::UpdateUserResponse response;

    SetupAuthSuccess();

    auto user = MakeUser();
    EXPECT_CALL(*mock_user_service_, 
                UpdateUser("test-uuid-123", std::optional<std::string>{}))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    grpc::ServerContext context;
    handler_->UpdateUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(UserHandlerTest, UpdateUser_Unauthenticated) {
    pb_user::UpdateUserRequest request;
    pb_user::UpdateUserResponse response;

    SetupAuthFailed(ErrorCode::Unauthenticated);

    EXPECT_CALL(*mock_user_service_, UpdateUser(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->UpdateUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::UNAUTHENTICATED));
}

TEST_F(UserHandlerTest, UpdateUser_UserDisabled) {
    pb_user::UpdateUserRequest request;
    request.mutable_display_name()->set_value("新昵称");

    pb_user::UpdateUserResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, 
                UpdateUser("test-uuid-123", std::optional<std::string>("新昵称")))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserDisabled)));

    grpc::ServerContext context;
    handler_->UpdateUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::USER_DISABLED));
}

// ============================================================================
// ChangePassword 测试
// ============================================================================

TEST_F(UserHandlerTest, ChangePassword_Success) {
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123!");
    request.set_new_password("NewPassword123!");

    pb_user::ChangePasswordResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, 
                ChangePassword("test-uuid-123", "OldPassword123!", "NewPassword123!"))
        .WillOnce(Return(Result<void>::Ok()));

    grpc::ServerContext context;
    handler_->ChangePassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(UserHandlerTest, ChangePassword_EmptyOldPassword) {
    pb_user::ChangePasswordRequest request;
    request.set_old_password("");
    request.set_new_password("NewPassword123!");

    pb_user::ChangePasswordResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, ChangePassword(_, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->ChangePassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(UserHandlerTest, ChangePassword_EmptyNewPassword) {
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123!");
    request.set_new_password("");

    pb_user::ChangePasswordResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, ChangePassword(_, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->ChangePassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(UserHandlerTest, ChangePassword_WeakNewPassword) {
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123!");
    request.set_new_password("weak");

    pb_user::ChangePasswordResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, ChangePassword(_, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->ChangePassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(UserHandlerTest, ChangePassword_WrongOldPassword) {
    pb_user::ChangePasswordRequest request;
    request.set_old_password("WrongOldPassword!");
    request.set_new_password("NewPassword123!");

    pb_user::ChangePasswordResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, 
                ChangePassword("test-uuid-123", "WrongOldPassword!", "NewPassword123!"))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::WrongPassword)));

    grpc::ServerContext context;
    handler_->ChangePassword(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::WRONG_PASSWORD));
}

// ============================================================================
// DeleteUser 测试
// ============================================================================

TEST_F(UserHandlerTest, DeleteUser_Success) {
    pb_user::DeleteUserRequest request;
    request.set_verify_code("123456");

    pb_user::DeleteUserResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, DeleteUser("test-uuid-123", "123456"))
        .WillOnce(Return(Result<void>::Ok()));

    grpc::ServerContext context;
    handler_->DeleteUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(UserHandlerTest, DeleteUser_InvalidVerifyCode) {
    pb_user::DeleteUserRequest request;
    request.set_verify_code("12");  // 太短

    pb_user::DeleteUserResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, DeleteUser(_, _)).Times(0);

    grpc::ServerContext context;
    handler_->DeleteUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(UserHandlerTest, DeleteUser_CaptchaWrong) {
    pb_user::DeleteUserRequest request;
    request.set_verify_code("000000");

    pb_user::DeleteUserResponse response;

    SetupAuthSuccess();

    EXPECT_CALL(*mock_user_service_, DeleteUser("test-uuid-123", "000000"))
        .WillOnce(Return(Result<void>::Fail(ErrorCode::CaptchaWrong)));

    grpc::ServerContext context;
    handler_->DeleteUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::CAPTCHA_WRONG));
}

// ============================================================================
// GetUser 测试（管理员接口）
// ============================================================================

TEST_F(UserHandlerTest, GetUser_AdminSuccess) {
    pb_user::GetUserRequest request;
    request.set_id("target-uuid");

    pb_user::GetUserResponse response;

    SetupAuthSuccess(UserRole::Admin);

    auto user = MakeUser("target-uuid", "13900139000");
    EXPECT_CALL(*mock_user_service_, GetUser("target-uuid"))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    grpc::ServerContext context;
    handler_->GetUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.user().id(), "target-uuid");
}

TEST_F(UserHandlerTest, GetUser_SuperAdminSuccess) {
    pb_user::GetUserRequest request;
    request.set_id("target-uuid");

    pb_user::GetUserResponse response;

    SetupAuthSuccess(UserRole::SuperAdmin);

    auto user = MakeUser("target-uuid", "13900139000");
    EXPECT_CALL(*mock_user_service_, GetUser("target-uuid"))
        .WillOnce(Return(Result<UserEntity>::Ok(user)));

    grpc::ServerContext context;
    handler_->GetUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(UserHandlerTest, GetUser_NotAdmin) {
    pb_user::GetUserRequest request;
    request.set_id("target-uuid");

    pb_user::GetUserResponse response;

    SetupAuthSuccess(UserRole::User);  // 普通用户

    EXPECT_CALL(*mock_user_service_, GetUser(_)).Times(0);

    grpc::ServerContext context;
    handler_->GetUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::ADMIN_REQUIRED));
}

TEST_F(UserHandlerTest, GetUser_EmptyId) {
    pb_user::GetUserRequest request;
    request.set_id("");

    pb_user::GetUserResponse response;

    SetupAuthSuccess(UserRole::Admin);

    EXPECT_CALL(*mock_user_service_, GetUser(_)).Times(0);

    grpc::ServerContext context;
    handler_->GetUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::INVALID_ARGUMENT));
}

TEST_F(UserHandlerTest, GetUser_UserNotFound) {
    pb_user::GetUserRequest request;
    request.set_id("non-existent-uuid");

    pb_user::GetUserResponse response;

    SetupAuthSuccess(UserRole::Admin);

    EXPECT_CALL(*mock_user_service_, GetUser("non-existent-uuid"))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserNotFound)));

    grpc::ServerContext context;
    handler_->GetUser(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::USER_NOT_FOUND));
}

// ============================================================================
// ListUsers 测试（管理员接口）
// ============================================================================

TEST_F(UserHandlerTest, ListUsers_AdminSuccess) {
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(1);
    request.mutable_page()->set_page_size(10);

    pb_user::ListUsersResponse response;

    SetupAuthSuccess(UserRole::Admin);

    ListUsersResult list_result;
    list_result.users.push_back(MakeUser("uuid-1", "13800138001"));
    list_result.users.push_back(MakeUser("uuid-2", "13800138002"));
    list_result.page_res.total_records = 2;
    list_result.page_res.total_pages = 1;
    list_result.page_res.page = 1;
    list_result.page_res.page_size = 10;

    EXPECT_CALL(*mock_user_service_, 
                ListUsers(std::optional<std::string>{}, std::optional<bool>{}, 1, 10))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    grpc::ServerContext context;
    handler_->ListUsers(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
    EXPECT_EQ(response.users_size(), 2);
    EXPECT_EQ(response.page_info().total_records(), 2);
}

TEST_F(UserHandlerTest, ListUsers_WithMobileFilter) {
    pb_user::ListUsersRequest request;
    request.set_mobile_filter("138");

    pb_user::ListUsersResponse response;

    SetupAuthSuccess(UserRole::Admin);

    ListUsersResult list_result;
    list_result.page_res.total_records = 0;
    list_result.page_res.total_pages = 0;
    list_result.page_res.page = 1;
    list_result.page_res.page_size = 20;

    EXPECT_CALL(*mock_user_service_, 
                ListUsers(std::optional<std::string>("138"), std::optional<bool>{}, 1, 20))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    grpc::ServerContext context;
    handler_->ListUsers(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(UserHandlerTest, ListUsers_WithDisabledFilter) {
    pb_user::ListUsersRequest request;
    request.mutable_disabled_filter()->set_value(true);

    pb_user::ListUsersResponse response;

    SetupAuthSuccess(UserRole::Admin);

    ListUsersResult list_result;
    list_result.page_res.total_records = 0;
    list_result.page_res.page = 1;
    list_result.page_res.page_size = 20;

    EXPECT_CALL(*mock_user_service_, 
                ListUsers(std::optional<std::string>{}, std::optional<bool>(true), 1, 20))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    grpc::ServerContext context;
    handler_->ListUsers(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}

TEST_F(UserHandlerTest, ListUsers_NotAdmin) {
    pb_user::ListUsersRequest request;
    pb_user::ListUsersResponse response;

    SetupAuthSuccess(UserRole::User);  // 普通用户

    EXPECT_CALL(*mock_user_service_, ListUsers(_, _, _, _)).Times(0);

    grpc::ServerContext context;
    handler_->ListUsers(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::ADMIN_REQUIRED));
}

TEST_F(UserHandlerTest, ListUsers_DefaultPagination) {
    pb_user::ListUsersRequest request;
    // 不设置分页参数

    pb_user::ListUsersResponse response;

    SetupAuthSuccess(UserRole::Admin);

    ListUsersResult list_result;
    list_result.page_res.page = 1;
    list_result.page_res.page_size = 20;  // 默认值

    EXPECT_CALL(*mock_user_service_, 
                ListUsers(std::optional<std::string>{}, std::optional<bool>{}, 1, 20))
        .WillOnce(Return(Result<ListUsersResult>::Ok(list_result)));

    grpc::ServerContext context;
    handler_->ListUsers(&context, &request, &response);

    EXPECT_EQ(response.result().code(), static_cast<int32_t>(pb_common::OK));
}
