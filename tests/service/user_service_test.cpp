#include "service_test_fixture.h"

namespace user_service {
namespace test {

class UserServiceTest : public ServiceTestFixture {};

// ============================================================================
// GetCurrentUser 测试
// ============================================================================

TEST_F(UserServiceTest, GetCurrentUser_Success) {
    CreateTestUser("13800010001", "password123", "测试用户");
    
    auto result = user_service_->GetCurrentUser("test_uuid_13800010001");
    
    ASSERT_TRUE(result.IsOk()) << "GetCurrentUser failed: " << result.message;
    EXPECT_EQ(result.data.value().mobile, "13800010001");
    EXPECT_EQ(result.data.value().display_name, "测试用户");
    EXPECT_TRUE(result.data.value().password_hash.empty());  // 密码不返回
}

TEST_F(UserServiceTest, GetCurrentUser_NotFound) {
    auto result = user_service_->GetCurrentUser("non_existent_uuid");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserServiceTest, GetCurrentUser_EmptyUUID) {
    auto result = user_service_->GetCurrentUser("");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

// ============================================================================
// UpdateUser 测试
// ============================================================================

TEST_F(UserServiceTest, UpdateUser_Success) {
    CreateTestUser("13800011001", "password123", "原名称");
    
    auto result = user_service_->UpdateUser(
        "test_uuid_13800011001",
        std::make_optional<std::string>("新名称")
    );
    
    ASSERT_TRUE(result.IsOk()) << "UpdateUser failed: " << result.message;
    EXPECT_EQ(result.data.value().display_name, "新名称");
    EXPECT_TRUE(result.data.value().password_hash.empty());
}

TEST_F(UserServiceTest, UpdateUser_NoChange) {
    CreateTestUser("13800011002", "password123", "原名称");
    
    auto result = user_service_->UpdateUser(
        "test_uuid_13800011002",
        std::nullopt  // 不更新任何字段
    );
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().display_name, "原名称");
}

TEST_F(UserServiceTest, UpdateUser_UserDisabled) {
    CreateTestUser("13800011003", "password123", "禁用用户", true);
    
    auto result = user_service_->UpdateUser(
        "test_uuid_13800011003",
        std::make_optional<std::string>("新名称")
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

TEST_F(UserServiceTest, UpdateUser_UserNotFound) {
    auto result = user_service_->UpdateUser(
        "non_existent_uuid",
        std::make_optional<std::string>("新名称")
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserServiceTest, UpdateUser_InvalidDisplayName_TooLong) {
    CreateTestUser("13800011004", "password123", "测试用户");
    
    std::string too_long_name(200, 'A');  // 太长的名字
    auto result = user_service_->UpdateUser(
        "test_uuid_13800011004",
        std::make_optional(too_long_name)
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

// ============================================================================
// ChangePassword 测试
// ============================================================================

TEST_F(UserServiceTest, ChangePassword_Success) {
    CreateTestUser("13800012001", "oldpassword", "测试用户");
    ClearLoginFailure("13800012001");
    
    auto result = user_service_->ChangePassword(
        "test_uuid_13800012001",
        "oldpassword",
        "newpassword123"
    );
    
    ASSERT_TRUE(result.IsOk()) << "ChangePassword failed: " << result.message;
    
    // 验证新密码可以登录
    auto login_result = auth_service_->LoginByPassword("13800012001", "newpassword123");
    EXPECT_TRUE(login_result.IsOk());
    
    // 验证旧密码不能登录
    auto old_login = auth_service_->LoginByPassword("13800012001", "oldpassword");
    EXPECT_FALSE(old_login.IsOk());
}

TEST_F(UserServiceTest, ChangePassword_WrongOldPassword) {
    CreateTestUser("13800012002", "oldpassword", "测试用户");
    
    auto result = user_service_->ChangePassword(
        "test_uuid_13800012002",
        "wrongpassword",
        "newpassword123"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::WrongPassword);
}

TEST_F(UserServiceTest, ChangePassword_SamePassword) {
    CreateTestUser("13800012003", "password123", "测试用户");
    
    auto result = user_service_->ChangePassword(
        "test_uuid_13800012003",
        "password123",
        "password123"  // 新旧密码相同
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(UserServiceTest, ChangePassword_UserDisabled) {
    CreateTestUser("13800012004", "oldpassword", "禁用用户", true);
    
    auto result = user_service_->ChangePassword(
        "test_uuid_13800012004",
        "oldpassword",
        "newpassword123"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

TEST_F(UserServiceTest, ChangePassword_UserNotFound) {
    auto result = user_service_->ChangePassword(
        "non_existent_uuid",
        "oldpassword",
        "newpassword123"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserServiceTest, ChangePassword_InvalidNewPassword) {
    CreateTestUser("13800012005", "oldpassword", "测试用户");
    
    auto result = user_service_->ChangePassword(
        "test_uuid_13800012005",
        "oldpassword",
        "123"  // 太短
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

// ============================================================================
// DeleteUser 测试
// ============================================================================

TEST_F(UserServiceTest, DeleteUser_Success) {
    CreateTestUser("13800013001", "password123", "待删除用户");
    SendTestVerifyCode(SmsScene::DeleteUser, "13800013001");
    
    auto result = user_service_->DeleteUser(
        "test_uuid_13800013001",
        GetTestVerifyCode(),
        "13800013001"
    );
    
    ASSERT_TRUE(result.IsOk()) << "DeleteUser failed: " << result.message;
    
    // 验证用户已被软删除（无法正常登录）
    ClearLoginFailure("13800013001");
    auto login_result = auth_service_->LoginByPassword("13800013001", "password123");
    EXPECT_FALSE(login_result.IsOk());
}

TEST_F(UserServiceTest, DeleteUser_WrongVerifyCode) {
    CreateTestUser("13800013002", "password123", "测试用户");
    SendTestVerifyCode(SmsScene::DeleteUser, "13800013002");
    
    auto result = user_service_->DeleteUser(
        "test_uuid_13800013002",
        "999999",  // 错误验证码
        "13800013002"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

TEST_F(UserServiceTest, DeleteUser_UserNotFound) {
    auto result = user_service_->DeleteUser(
        "non_existent_uuid",
        GetTestVerifyCode(),
        "13800013003"
    );
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserServiceTest, DeleteUser_InvalidatesTokens) {
    CreateTestUser("13800013004", "password123", "测试用户");
    ClearLoginFailure("13800013004");
    
    // 先登录获取 token
    auto login_result = auth_service_->LoginByPassword("13800013004", "password123");
    ASSERT_TRUE(login_result.IsOk());
    std::string refresh_token = login_result.data.value().tokens.refresh_token;
    
    // 删除用户
    SendTestVerifyCode(SmsScene::DeleteUser, "13800013004");
    auto result = user_service_->DeleteUser(
        "test_uuid_13800013004",
        GetTestVerifyCode(),
        "13800013004"
    );
    ASSERT_TRUE(result.IsOk());
    
    // Token 应该失效
    auto refresh_result = auth_service_->RefreshToken(refresh_token);
    EXPECT_FALSE(refresh_result.IsOk());
}

// ============================================================================
// GetUser (管理员) 测试
// ============================================================================

TEST_F(UserServiceTest, GetUser_Success) {
    CreateTestUser("13800014001", "password123", "查询用户");
    
    auto result = user_service_->GetUser("test_uuid_13800014001");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().mobile, "13800014001");
    EXPECT_TRUE(result.data.value().password_hash.empty());
}

TEST_F(UserServiceTest, GetUser_NotFound) {
    auto result = user_service_->GetUser("non_existent_uuid");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

// ============================================================================
// ListUsers 测试
// ============================================================================

TEST_F(UserServiceTest, ListUsers_NoFilter) {
    CreateTestUser("13800015001", "password123", "用户1");
    CreateTestUser("13800015002", "password123", "用户2");
    CreateTestUser("13800015003", "password123", "用户3");
    
    auto result = user_service_->ListUsers(std::nullopt, std::nullopt, 1, 10);
    
    ASSERT_TRUE(result.IsOk()) << "ListUsers failed: " << result.message;
    EXPECT_EQ(result.data.value().users.size(), 3);
    EXPECT_EQ(result.data.value().page_res.total_records, 3);
    EXPECT_EQ(result.data.value().page_res.page, 1);
}

TEST_F(UserServiceTest, ListUsers_MobileFilter) {
    CreateTestUser("13800016001", "password123", "用户A");
    CreateTestUser("13800016002", "password123", "用户B");
    CreateTestUser("13900017001", "password123", "用户C");
    
    auto result = user_service_->ListUsers(
        std::make_optional<std::string>("138000160"),
        std::nullopt,
        1, 10
    );
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().users.size(), 2);  // 只匹配 A 和 B
}

TEST_F(UserServiceTest, ListUsers_DisabledFilter_True) {
    CreateTestUser("13800017001", "password123", "启用用户", false);
    CreateTestUser("13800017002", "password123", "禁用用户", true);
    
    auto result = user_service_->ListUsers(
        std::nullopt,
        std::make_optional(true),  // 只查禁用的
        1, 10
    );
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().users.size(), 1);
    EXPECT_TRUE(result.data.value().users[0].disabled);
}

TEST_F(UserServiceTest, ListUsers_DisabledFilter_False) {
    CreateTestUser("13800017003", "password123", "启用用户", false);
    CreateTestUser("13800017004", "password123", "禁用用户", true);
    
    auto result = user_service_->ListUsers(
        std::nullopt,
        std::make_optional(false),  // 只查启用的
        1, 10
    );
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().users.size(), 1);
    EXPECT_FALSE(result.data.value().users[0].disabled);
}

TEST_F(UserServiceTest, ListUsers_Pagination) {
    for (int i = 1; i <= 15; ++i) {
        std::string mobile = "138000180" + std::to_string(10 + i);
        CreateTestUser(mobile, "password123", "用户" + std::to_string(i));
    }
    
    // 第一页
    auto page1 = user_service_->ListUsers(std::nullopt, std::nullopt, 1, 5);
    ASSERT_TRUE(page1.IsOk());
    EXPECT_EQ(page1.data.value().users.size(), 5);
    EXPECT_EQ(page1.data.value().page_res.total_records, 15);
    EXPECT_EQ(page1.data.value().page_res.total_pages, 3);
    EXPECT_EQ(page1.data.value().page_res.page, 1);
    
    // 第二页
    auto page2 = user_service_->ListUsers(std::nullopt, std::nullopt, 2, 5);
    ASSERT_TRUE(page2.IsOk());
    EXPECT_EQ(page2.data.value().users.size(), 5);
    EXPECT_EQ(page2.data.value().page_res.page, 2);
    
    // 第三页
    auto page3 = user_service_->ListUsers(std::nullopt, std::nullopt, 3, 5);
    ASSERT_TRUE(page3.IsOk());
    EXPECT_EQ(page3.data.value().users.size(), 5);
    EXPECT_EQ(page3.data.value().page_res.page, 3);
}

TEST_F(UserServiceTest, ListUsers_MaxPageSize) {
    for (int i = 1; i <= 5; ++i) {
        std::string mobile = "138000190" + std::to_string(10 + i);
        CreateTestUser(mobile, "password123", "用户" + std::to_string(i));
    }
    
    auto result = user_service_->ListUsers(std::nullopt, std::nullopt, 1, 1000);
    
    ASSERT_TRUE(result.IsOk());
    // page_size 应该被限制为 100
    EXPECT_LE(result.data.value().page_res.page_size, 100);
}

TEST_F(UserServiceTest, ListUsers_EmptyResult) {
    // 不创建任何用户
    
    auto result = user_service_->ListUsers(std::nullopt, std::nullopt, 1, 10);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().users.size(), 0);
    EXPECT_EQ(result.data.value().page_res.total_records, 0);
}

TEST_F(UserServiceTest, ListUsers_InvalidPage) {
    CreateTestUser("13800020001", "password123", "用户1");
    
    // 页码为 0 应该被修正为 1
    auto result = user_service_->ListUsers(std::nullopt, std::nullopt, 0, 10);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.data.value().page_res.page, 1);
}

// ============================================================================
// SetUserDisabled 测试
// ============================================================================

TEST_F(UserServiceTest, SetUserDisabled_Enable) {
    CreateTestUser("13800019001", "password123", "禁用用户", true);
    
    auto result = user_service_->SetUserDisabled("test_uuid_13800019001", false);
    
    ASSERT_TRUE(result.IsOk());
    
    // 验证用户已启用
    auto user = user_service_->GetUser("test_uuid_13800019001");
    EXPECT_FALSE(user.data.value().disabled);
    
    // 可以正常登录
    ClearLoginFailure("13800019001");
    auto login = auth_service_->LoginByPassword("13800019001", "password123");
    EXPECT_TRUE(login.IsOk());
}

TEST_F(UserServiceTest, SetUserDisabled_Disable) {
    CreateTestUser("13800019002", "password123", "正常用户", false);
    ClearLoginFailure("13800019002");
    
    // 先登录获取 token
    auto login_result = auth_service_->LoginByPassword("13800019002", "password123");
    ASSERT_TRUE(login_result.IsOk());
    std::string refresh_token = login_result.data.value().tokens.refresh_token;
    
    // 禁用用户
    auto result = user_service_->SetUserDisabled("test_uuid_13800019002", true);
    ASSERT_TRUE(result.IsOk());
    
    // 验证用户已禁用
    auto user = user_service_->GetUser("test_uuid_13800019002");
    EXPECT_TRUE(user.data.value().disabled);
    
    // Token 应该失效
    auto refresh_result = auth_service_->RefreshToken(refresh_token);
    EXPECT_FALSE(refresh_result.IsOk());
    
    // 无法登录
    auto login = auth_service_->LoginByPassword("13800019002", "password123");
    EXPECT_FALSE(login.IsOk());
    EXPECT_EQ(login.code, ErrorCode::UserDisabled);
}

TEST_F(UserServiceTest, SetUserDisabled_SameState_NoOp) {
    CreateTestUser("13800019003", "password123", "正常用户", false);
    
    auto result = user_service_->SetUserDisabled("test_uuid_13800019003", false);
    
    ASSERT_TRUE(result.IsOk());  // 状态相同，直接返回成功
}

TEST_F(UserServiceTest, SetUserDisabled_UserNotFound) {
    auto result = user_service_->SetUserDisabled("non_existent_uuid", true);
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserServiceTest, SetUserDisabled_EmptyUUID) {
    auto result = user_service_->SetUserDisabled("", true);
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

}  // namespace test
}  // namespace user_service
