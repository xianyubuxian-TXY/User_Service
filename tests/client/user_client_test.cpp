// tests/client/user_client_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "client/user_client.h"
#include "common/error_codes.h"
#include "pb_user/user.grpc.pb.h"

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::Invoke;

namespace user_service {
namespace testing {

// ============================================================================
// Mock Stub
// ============================================================================

class MockUserStub : public pb_user::UserService::StubInterface {
public:
    // 同步方法
    MOCK_METHOD(grpc::Status, GetCurrentUser,
        (grpc::ClientContext*, const pb_user::GetCurrentUserRequest&,
         pb_user::GetCurrentUserResponse*), (override));
    
    MOCK_METHOD(grpc::Status, UpdateUser,
        (grpc::ClientContext*, const pb_user::UpdateUserRequest&,
         pb_user::UpdateUserResponse*), (override));
    
    MOCK_METHOD(grpc::Status, ChangePassword,
        (grpc::ClientContext*, const pb_user::ChangePasswordRequest&,
         pb_user::ChangePasswordResponse*), (override));
    
    MOCK_METHOD(grpc::Status, DeleteUser,
        (grpc::ClientContext*, const pb_user::DeleteUserRequest&,
         pb_user::DeleteUserResponse*), (override));
    
    MOCK_METHOD(grpc::Status, GetUser,
        (grpc::ClientContext*, const pb_user::GetUserRequest&,
         pb_user::GetUserResponse*), (override));
    
    MOCK_METHOD(grpc::Status, ListUsers,
        (grpc::ClientContext*, const pb_user::ListUsersRequest&,
         pb_user::ListUsersResponse*), (override));

    // 异步方法（必须实现）
    grpc::ClientAsyncResponseReaderInterface<pb_user::GetCurrentUserResponse>*
    AsyncGetCurrentUserRaw(grpc::ClientContext*, const pb_user::GetCurrentUserRequest&,
                           grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_user::GetCurrentUserResponse>*
    PrepareAsyncGetCurrentUserRaw(grpc::ClientContext*, const pb_user::GetCurrentUserRequest&,
                                  grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_user::UpdateUserResponse>*
    AsyncUpdateUserRaw(grpc::ClientContext*, const pb_user::UpdateUserRequest&,
                       grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_user::UpdateUserResponse>*
    PrepareAsyncUpdateUserRaw(grpc::ClientContext*, const pb_user::UpdateUserRequest&,
                              grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_user::ChangePasswordResponse>*
    AsyncChangePasswordRaw(grpc::ClientContext*, const pb_user::ChangePasswordRequest&,
                           grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_user::ChangePasswordResponse>*
    PrepareAsyncChangePasswordRaw(grpc::ClientContext*, const pb_user::ChangePasswordRequest&,
                                  grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_user::DeleteUserResponse>*
    AsyncDeleteUserRaw(grpc::ClientContext*, const pb_user::DeleteUserRequest&,
                       grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_user::DeleteUserResponse>*
    PrepareAsyncDeleteUserRaw(grpc::ClientContext*, const pb_user::DeleteUserRequest&,
                              grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_user::GetUserResponse>*
    AsyncGetUserRaw(grpc::ClientContext*, const pb_user::GetUserRequest&,
                    grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_user::GetUserResponse>*
    PrepareAsyncGetUserRaw(grpc::ClientContext*, const pb_user::GetUserRequest&,
                           grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_user::ListUsersResponse>*
    AsyncListUsersRaw(grpc::ClientContext*, const pb_user::ListUsersRequest&,
                      grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_user::ListUsersResponse>*
    PrepareAsyncListUsersRaw(grpc::ClientContext*, const pb_user::ListUsersRequest&,
                             grpc::CompletionQueue*) override { return nullptr; }
};

// ============================================================================
// 测试 Fixture
// ============================================================================

class UserClientTest : public ::testing::Test {
protected:
    static pb_common::Result MakeOkResult() {
        pb_common::Result result;
        result.set_code(pb_common::ErrorCode::OK);
        result.set_msg("success");
        return result;
    }

    static pb_common::Result MakeErrorResult(pb_common::ErrorCode code,
                                             const std::string& msg) {
        pb_common::Result result;
        result.set_code(code);
        result.set_msg(msg);
        return result;
    }

    static void FillUserProto(pb_user::User* user) {
        user->set_id("usr_test-uuid-12345");
        user->set_mobile("13800138000");
        user->set_display_name("TestUser");
        user->set_role(pb_auth::UserRole::USER_ROLE_USER);
        user->set_disabled(false);
    }
};

// ============================================================================
// GetCurrentUser 测试
// ============================================================================

TEST_F(UserClientTest, GetCurrentUser_Success) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, GetCurrentUser(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::GetCurrentUserRequest&,
                      pb_user::GetCurrentUserResponse* resp) {
                *resp->mutable_result() = MakeOkResult();
                FillUserProto(resp->mutable_user());
                return grpc::Status::OK;
            })
        ));
    
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->GetCurrentUser(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().mobile(), "13800138000");
}

TEST_F(UserClientTest, GetCurrentUser_Unauthenticated) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, GetCurrentUser(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::GetCurrentUserRequest&,
                      pb_user::GetCurrentUserResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::UNAUTHENTICATED,
                    "请先登录"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_user::GetCurrentUserRequest request;
    pb_user::GetCurrentUserResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->GetCurrentUser(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::UNAUTHENTICATED);
}

// ============================================================================
// UpdateUser 测试
// ============================================================================

TEST_F(UserClientTest, UpdateUser_Success) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, UpdateUser(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::UpdateUserRequest& req,
                      pb_user::UpdateUserResponse* resp) {
                EXPECT_TRUE(req.has_display_name());
                EXPECT_EQ(req.display_name().value(), "NewName");
                
                *resp->mutable_result() = MakeOkResult();
                auto* user = resp->mutable_user();
                FillUserProto(user);
                user->set_display_name("NewName");
                return grpc::Status::OK;
            })
        ));
    
    pb_user::UpdateUserRequest request;
    request.mutable_display_name()->set_value("NewName");
    
    pb_user::UpdateUserResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->UpdateUser(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().display_name(), "NewName");
}

// ============================================================================
// ChangePassword 测试
// ============================================================================

TEST_F(UserClientTest, ChangePassword_Success) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, ChangePassword(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::ChangePasswordRequest& req,
                      pb_user::ChangePasswordResponse* resp) {
                EXPECT_EQ(req.old_password(), "OldPassword123");
                EXPECT_EQ(req.new_password(), "NewPassword456");
                
                *resp->mutable_result() = MakeOkResult();
                return grpc::Status::OK;
            })
        ));
    
    pb_user::ChangePasswordRequest request;
    request.set_old_password("OldPassword123");
    request.set_new_password("NewPassword456");
    
    pb_user::ChangePasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ChangePassword(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(UserClientTest, ChangePassword_WrongOldPassword) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, ChangePassword(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::ChangePasswordRequest&,
                      pb_user::ChangePasswordResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::WRONG_PASSWORD,
                    "旧密码错误"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_user::ChangePasswordRequest request;
    request.set_old_password("WrongPassword");
    request.set_new_password("NewPassword456");
    
    pb_user::ChangePasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ChangePassword(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::WRONG_PASSWORD);
}

// ============================================================================
// DeleteUser 测试
// ============================================================================

TEST_F(UserClientTest, DeleteUser_Success) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, DeleteUser(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::DeleteUserRequest& req,
                      pb_user::DeleteUserResponse* resp) {
                EXPECT_EQ(req.verify_code(), "123456");
                
                *resp->mutable_result() = MakeOkResult();
                return grpc::Status::OK;
            })
        ));
    
    pb_user::DeleteUserRequest request;
    request.set_verify_code("123456");
    
    pb_user::DeleteUserResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->DeleteUser(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

// ============================================================================
// GetUser（管理员）测试
// ============================================================================

TEST_F(UserClientTest, GetUser_Success) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, GetUser(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::GetUserRequest& req,
                      pb_user::GetUserResponse* resp) {
                EXPECT_EQ(req.id(), "usr_target-user-id");
                
                *resp->mutable_result() = MakeOkResult();
                FillUserProto(resp->mutable_user());
                resp->mutable_user()->set_id("usr_target-user-id");
                return grpc::Status::OK;
            })
        ));
    
    pb_user::GetUserRequest request;
    request.set_id("usr_target-user-id");
    
    pb_user::GetUserResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->GetUser(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().id(), "usr_target-user-id");
}

TEST_F(UserClientTest, GetUser_AdminRequired) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, GetUser(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::GetUserRequest&,
                      pb_user::GetUserResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::ADMIN_REQUIRED,
                    "需要管理员权限"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_user::GetUserRequest request;
    request.set_id("usr_target-user-id");
    
    pb_user::GetUserResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->GetUser(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::ADMIN_REQUIRED);
}

// ============================================================================
// ListUsers（管理员）测试
// ============================================================================

TEST_F(UserClientTest, ListUsers_Success) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, ListUsers(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::ListUsersRequest& req,
                      pb_user::ListUsersResponse* resp) {
                EXPECT_EQ(req.page().page(), 1);
                EXPECT_EQ(req.page().page_size(), 20);
                
                *resp->mutable_result() = MakeOkResult();
                
                // 添加两个用户
                auto* user1 = resp->add_users();
                user1->set_id("usr_user-1");
                user1->set_mobile("13800138001");
                
                auto* user2 = resp->add_users();
                user2->set_id("usr_user-2");
                user2->set_mobile("13800138002");
                
                auto* page_info = resp->mutable_page_info();
                page_info->set_total_records(2);
                page_info->set_total_pages(1);
                page_info->set_page(1);
                page_info->set_page_size(20);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(1);
    request.mutable_page()->set_page_size(20);
    
    pb_user::ListUsersResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ListUsers(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.users_size(), 2);
    EXPECT_EQ(response.page_info().total_records(), 2);
}

TEST_F(UserClientTest, ListUsers_WithFilter) {
    auto mock_stub = std::make_unique<MockUserStub>();
    
    EXPECT_CALL(*mock_stub, ListUsers(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*,
                      const pb_user::ListUsersRequest& req,
                      pb_user::ListUsersResponse* resp) {
                EXPECT_EQ(req.mobile_filter(), "138");
                EXPECT_TRUE(req.has_disabled_filter());
                EXPECT_FALSE(req.disabled_filter().value());
                
                *resp->mutable_result() = MakeOkResult();
                
                auto* page_info = resp->mutable_page_info();
                page_info->set_total_records(0);
                page_info->set_total_pages(0);
                page_info->set_page(1);
                page_info->set_page_size(20);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_user::ListUsersRequest request;
    request.mutable_page()->set_page(1);
    request.mutable_page()->set_page_size(20);
    request.set_mobile_filter("138");
    request.mutable_disabled_filter()->set_value(false);
    
    pb_user::ListUsersResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ListUsers(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

} // namespace testing
} // namespace user_service
