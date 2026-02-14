// tests/service/user_service_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "service/user_service.h"
#include "mock_services.h"
#include "common/password_helper.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::AnyNumber;

class UserServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = TestDataFactory::CreateTestConfig();
        mock_user_db_ = std::make_shared<MockUserDB>();
        mock_token_repo_ = std::make_shared<MockTokenRepository>();
        mock_redis_ = std::make_shared<MockRedisClient>();
        
        sms_service_ = std::make_shared<SmsService>(mock_redis_, config_->sms);
        
        user_service_ = std::make_shared<UserService>(
            config_,
            mock_user_db_,
            mock_token_repo_,
            sms_service_
        );
    }

    std::shared_ptr<Config> config_;
    std::shared_ptr<MockUserDB> mock_user_db_;
    std::shared_ptr<MockTokenRepository> mock_token_repo_;
    std::shared_ptr<MockRedisClient> mock_redis_;
    std::shared_ptr<SmsService> sms_service_;
    std::shared_ptr<UserService> user_service_;
};

// ==================== GetCurrentUser 测试 ====================

TEST_F(UserServiceTest, GetCurrentUser_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    auto result = user_service_->GetCurrentUser("test-uuid-1");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().uuid, test_user.uuid);
    EXPECT_TRUE(result.Value().password_hash.empty());  // 密码应被清除
}

TEST_F(UserServiceTest, GetCurrentUser_EmptyUuid) {
    auto result = user_service_->GetCurrentUser("");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(UserServiceTest, GetCurrentUser_NotFound) {
    EXPECT_CALL(*mock_user_db_, FindByUUID("nonexistent"))
        .WillOnce(Return(Result<UserEntity>::Fail(ErrorCode::UserNotFound)));
    
    auto result = user_service_->GetCurrentUser("nonexistent");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

// ==================== UpdateUser 测试 ====================

TEST_F(UserServiceTest, UpdateUser_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    EXPECT_CALL(*mock_user_db_, Update(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = user_service_->UpdateUser("test-uuid-1", "NewDisplayName");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().display_name, "NewDisplayName");
}

TEST_F(UserServiceTest, UpdateUser_NoChanges) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 不传任何更新字段
    auto result = user_service_->UpdateUser("test-uuid-1", std::nullopt);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().display_name, test_user.display_name);  // 未变
}

TEST_F(UserServiceTest, UpdateUser_UserDisabled) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.disabled = true;
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    auto result = user_service_->UpdateUser("test-uuid-1", "NewName");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserDisabled);
}

TEST_F(UserServiceTest, UpdateUser_InvalidDisplayName) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 昵称超长（>32字符）
    std::string long_name(100, 'a');
    auto result = user_service_->UpdateUser("test-uuid-1", long_name);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

// ==================== ChangePassword 测试 ====================

TEST_F(UserServiceTest, ChangePassword_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.password_hash = PasswordHelper::Hash("OldPassword123");
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    EXPECT_CALL(*mock_user_db_, Update(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = user_service_->ChangePassword(
        "test-uuid-1", "OldPassword123", "NewPassword123"
    );
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(UserServiceTest, ChangePassword_WrongOldPassword) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.password_hash = PasswordHelper::Hash("OldPassword123");
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    auto result = user_service_->ChangePassword(
        "test-uuid-1", "WrongOldPassword1", "NewPassword123"
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::WrongPassword);
}

TEST_F(UserServiceTest, ChangePassword_SamePassword) {
    auto result = user_service_->ChangePassword(
        "test-uuid-1", "Password123", "Password123"  // 新旧密码相同
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}

TEST_F(UserServiceTest, ChangePassword_WeakNewPassword) {
    auto result = user_service_->ChangePassword(
        "test-uuid-1", "OldPassword123", "weak"  // 新密码太弱
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
}
// ==================== DeleteUser 测试 ====================

TEST_F(UserServiceTest, DeleteUser_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // SMS 验证成功
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("123456")));
    EXPECT_CALL(*mock_redis_, Del(_))
        .WillRepeatedly(Return(Result<bool>::Ok(true)));
    
    // 撤销所有 Token
    EXPECT_CALL(*mock_token_repo_, DeleteByUserId(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 软删除用户
    EXPECT_CALL(*mock_user_db_, Update(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = user_service_->DeleteUser("test-uuid-1", "123456");
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(UserServiceTest, DeleteUser_WrongVerifyCode) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // SMS 验证失败
    EXPECT_CALL(*mock_redis_, Exists(_))
        .WillRepeatedly(Return(Result<bool>::Ok(false)));
    EXPECT_CALL(*mock_redis_, Get(_))
        .WillOnce(Return(Result<std::optional<std::string>>::Ok("654321")));  // 错误验证码
    EXPECT_CALL(*mock_redis_, Incr(_))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    EXPECT_CALL(*mock_redis_, PExpire(_, _))
        .WillOnce(Return(Result<bool>::Ok(true)));
    
    auto result = user_service_->DeleteUser("test-uuid-1", "123456");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::CaptchaWrong);
}

// ==================== GetUser 测试（管理员）====================

TEST_F(UserServiceTest, GetUser_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    auto result = user_service_->GetUser("test-uuid-1");
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().uuid, test_user.uuid);
}

// ==================== ListUsers 测试（管理员）====================

TEST_F(UserServiceTest, ListUsers_Success) {
    std::vector<UserEntity> users = {
        TestDataFactory::CreateTestUser(1),
        TestDataFactory::CreateTestUser(2),
        TestDataFactory::CreateTestUser(3)
    };
    
    EXPECT_CALL(*mock_user_db_, Count(_))
        .WillOnce(Return(Result<int64_t>::Ok(3)));
    
    EXPECT_CALL(*mock_user_db_, FindAll(_))
        .WillOnce(Return(Result<std::vector<UserEntity>>::Ok(users)));
    
    auto result = user_service_->ListUsers(std::nullopt, std::nullopt, 1, 20);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().users.size(), 3);
    EXPECT_EQ(result.Value().page_res.total_records, 3);
}

TEST_F(UserServiceTest, ListUsers_WithFilters) {
    std::vector<UserEntity> users = {
        TestDataFactory::CreateTestUser(1)
    };
    
    EXPECT_CALL(*mock_user_db_, Count(_))
        .WillOnce(Return(Result<int64_t>::Ok(1)));
    
    EXPECT_CALL(*mock_user_db_, FindAll(_))
        .WillOnce(Return(Result<std::vector<UserEntity>>::Ok(users)));
    
    auto result = user_service_->ListUsers("138", false, 1, 10);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().users.size(), 1);
}

TEST_F(UserServiceTest, ListUsers_Pagination) {
    std::vector<UserEntity> users;
    for (int i = 0; i < 10; ++i) {
        users.push_back(TestDataFactory::CreateTestUser(i + 1));
    }
    
    EXPECT_CALL(*mock_user_db_, Count(_))
        .WillOnce(Return(Result<int64_t>::Ok(50)));  // 总共50条
    
    EXPECT_CALL(*mock_user_db_, FindAll(_))
        .WillOnce(Return(Result<std::vector<UserEntity>>::Ok(users)));
    
    auto result = user_service_->ListUsers(std::nullopt, std::nullopt, 2, 10);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().page_res.total_records, 50);
    EXPECT_EQ(result.Value().page_res.total_pages, 5);
    EXPECT_EQ(result.Value().page_res.page, 2);
    EXPECT_EQ(result.Value().page_res.page_size, 10);
}

// ==================== SetUserDisabled 测试（管理员）====================

TEST_F(UserServiceTest, SetUserDisabled_Success) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.disabled = false;
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    EXPECT_CALL(*mock_user_db_, Update(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 禁用时应撤销所有 Token
    EXPECT_CALL(*mock_token_repo_, DeleteByUserId(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    auto result = user_service_->SetUserDisabled("test-uuid-1", true);
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(UserServiceTest, SetUserDisabled_NoChange) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.disabled = true;  // 已禁用
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    // 状态相同，不应调用 Update
    auto result = user_service_->SetUserDisabled("test-uuid-1", true);
    
    EXPECT_TRUE(result.IsOk());
}

TEST_F(UserServiceTest, SetUserDisabled_EnableUser) {
    auto test_user = TestDataFactory::CreateTestUser();
    test_user.disabled = true;
    
    EXPECT_CALL(*mock_user_db_, FindByUUID("test-uuid-1"))
        .WillOnce(Return(Result<UserEntity>::Ok(test_user)));
    
    EXPECT_CALL(*mock_user_db_, Update(_))
        .WillOnce(Return(Result<void>::Ok()));
    
    // 启用用户不需要撤销 Token
    auto result = user_service_->SetUserDisabled("test-uuid-1", false);
    
    EXPECT_TRUE(result.IsOk());
}
