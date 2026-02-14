// tests/db/user_db_test.cpp

#include "db_test_fixture.h"
#include "common/error_codes.h"

namespace user_service {
namespace testing {

class UserDBTest : public DBTestFixture {
protected:
    void SetUp() override {
        DBTestFixture::SetUp();
        user_db_ = std::make_shared<UserDB>(pool_);
    }

protected:
    std::shared_ptr<UserDB> user_db_;
};

// ==================== Create 测试 ====================

TEST_F(UserDBTest, Create_Success) {
    UserEntity user;
    user.mobile = "19900000100";
    user.display_name = "Create Test User";
    user.password_hash = "$sha256$salt$hash_create";
    user.role = UserRole::User;
    user.disabled = false;
    
    auto result = user_db_->Create(user);
    
    ASSERT_TRUE(result.IsOk()) << result.message;
    
    const auto& created = result.Value();
    EXPECT_FALSE(created.uuid.empty());
    EXPECT_EQ(created.mobile, "19900000100");
    EXPECT_EQ(created.display_name, "Create Test User");
    EXPECT_GT(created.id, 0);
    EXPECT_FALSE(created.created_at.empty());
}

TEST_F(UserDBTest, Create_DuplicateMobile) {
    UserEntity user1;
    user1.mobile = "19900000101";
    user1.display_name = "First User";
    user1.password_hash = "$sha256$salt$hash1";
    
    auto result1 = user_db_->Create(user1);
    ASSERT_TRUE(result1.IsOk());
    
    // 尝试创建相同手机号
    UserEntity user2;
    user2.mobile = "19900000101";  // 重复
    user2.display_name = "Second User";
    user2.password_hash = "$sha256$salt$hash2";
    
    auto result2 = user_db_->Create(user2);
    
    EXPECT_FALSE(result2.IsOk());
    EXPECT_EQ(result2.code, ErrorCode::MobileTaken);
}

TEST_F(UserDBTest, Create_WithRole) {
    UserEntity admin;
    admin.mobile = "19900000102";
    admin.display_name = "Admin User";
    admin.password_hash = "$sha256$salt$hash_admin";
    admin.role = UserRole::Admin;
    
    auto result = user_db_->Create(admin);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().role, UserRole::Admin);
}

// ==================== Read 测试 ====================

TEST_F(UserDBTest, FindById_Success) {
    // 先创建用户
    std::string uuid = CreateTestUser("19900000110", "Find By Id");
    int64_t user_id = GetUserIdByMobile("19900000110");
    
    auto result = user_db_->FindById(user_id);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().uuid, uuid);
    EXPECT_EQ(result.Value().mobile, "19900000110");
}

TEST_F(UserDBTest, FindById_NotFound) {
    auto result = user_db_->FindById(99999999);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserDBTest, FindByUUID_Success) {
    std::string uuid = CreateTestUser("19900000111", "Find By UUID");
    
    auto result = user_db_->FindByUUID(uuid);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().mobile, "19900000111");
    EXPECT_EQ(result.Value().display_name, "Find By UUID");
}

TEST_F(UserDBTest, FindByUUID_NotFound) {
    auto result = user_db_->FindByUUID("nonexistent-uuid");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserDBTest, FindByMobile_Success) {
    CreateTestUser("19900000112", "Find By Mobile");
    
    auto result = user_db_->FindByMobile("19900000112");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().display_name, "Find By Mobile");
}

TEST_F(UserDBTest, FindByMobile_NotFound) {
    auto result = user_db_->FindByMobile("00000000000");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

// ==================== Update 测试 ====================

TEST_F(UserDBTest, Update_Success) {
    std::string uuid = CreateTestUser("19900000120", "Before Update");
    
    // 获取用户
    auto find_result = user_db_->FindByUUID(uuid);
    ASSERT_TRUE(find_result.IsOk());
    
    // 修改
    UserEntity user = find_result.Value();
    user.display_name = "After Update";
    user.disabled = true;
    
    auto update_result = user_db_->Update(user);
    ASSERT_TRUE(update_result.IsOk());
    
    // 验证
    auto verify_result = user_db_->FindByUUID(uuid);
    ASSERT_TRUE(verify_result.IsOk());
    EXPECT_EQ(verify_result.Value().display_name, "After Update");
    EXPECT_TRUE(verify_result.Value().disabled);
}

TEST_F(UserDBTest, Update_NotFound) {
    UserEntity user;
    user.uuid = "nonexistent-uuid";
    user.display_name = "Test";
    user.password_hash = "hash";
    
    auto result = user_db_->Update(user);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserDBTest, Update_Role) {
    std::string uuid = CreateTestUser("19900000121", "Role Update Test");
    
    auto find_result = user_db_->FindByUUID(uuid);
    ASSERT_TRUE(find_result.IsOk());
    
    UserEntity user = find_result.Value();
    user.role = UserRole::Admin;
    
    auto update_result = user_db_->Update(user);
    ASSERT_TRUE(update_result.IsOk());
    
    // 验证
    auto verify_result = user_db_->FindByUUID(uuid);
    EXPECT_EQ(verify_result.Value().role, UserRole::Admin);
}

// ==================== Delete 测试 ====================

TEST_F(UserDBTest, Delete_Success) {
    CreateTestUser("19900000130", "To Delete");
    int64_t user_id = GetUserIdByMobile("19900000130");
    
    auto result = user_db_->Delete(user_id);
    ASSERT_TRUE(result.IsOk());
    
    // 验证已删除
    auto find_result = user_db_->FindById(user_id);
    EXPECT_FALSE(find_result.IsOk());
    EXPECT_EQ(find_result.code, ErrorCode::UserNotFound);
}

TEST_F(UserDBTest, Delete_NotFound) {
    auto result = user_db_->Delete(99999999);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::UserNotFound);
}

TEST_F(UserDBTest, DeleteByUUID_Success) {
    std::string uuid = CreateTestUser("19900000131", "Delete By UUID");
    
    auto result = user_db_->DeleteByUUID(uuid);
    ASSERT_TRUE(result.IsOk());
    
    // 验证
    auto find_result = user_db_->FindByUUID(uuid);
    EXPECT_FALSE(find_result.IsOk());
}

// ==================== 分页查询测试 ====================

TEST_F(UserDBTest, FindAll_Pagination) {
    // 创建多个用户
    for (int i = 0; i < 15; ++i) {
        std::string mobile = "1990000014" + std::to_string(i);
        CreateTestUser(mobile, "Page User " + std::to_string(i));
    }
    
    // 查询第一页
    PageParams page1;
    page1.page = 1;
    page1.page_size = 5;
    
    auto result1 = user_db_->FindAll(page1, "1990000014");
    
    ASSERT_TRUE(result1.IsOk()) << "Query failed: " << result1.message;
    auto& [users1, page_info1] = result1.Value();
    
    EXPECT_EQ(users1.size(), 5);
    EXPECT_EQ(page_info1.total_records, 15);
    EXPECT_EQ(page_info1.total_pages, 3);
    EXPECT_EQ(page_info1.page, 1);
    
    // 查询第二页
    PageParams page2;
    page2.page = 2;
    page2.page_size = 5;
    
    auto result2 = user_db_->FindAll(page2, "1990000014");
    
    ASSERT_TRUE(result2.IsOk()) << "Query failed: " << result2.message;
    auto& [users2, page_info2] = result2.Value();
    
    EXPECT_EQ(users2.size(), 5);
    EXPECT_EQ(page_info2.page, 2);
}

TEST_F(UserDBTest, FindAll_WithFilter) {
    CreateTestUser("19900000150", "Filter A");
    CreateTestUser("19900000151", "Filter B");
    
    // 精确过滤
    PageParams page;
    page.page = 1;
    page.page_size = 10;
    
    auto result = user_db_->FindAll(page, "19900000150");
    
    ASSERT_TRUE(result.IsOk()) << "Query failed: " << result.message;
    EXPECT_EQ(result.Value().first.size(), 1);
    EXPECT_EQ(result.Value().first[0].display_name, "Filter A");
}

TEST_F(UserDBTest, FindAll_WithQueryParams) {
    CreateTestUser("19900000160", "Active User");
    
    // 创建禁用用户
    std::string uuid = CreateTestUser("19900000161", "Disabled User");
    auto user_result = user_db_->FindByUUID(uuid);
    UserEntity disabled_user = user_result.Value();
    disabled_user.disabled = true;
    user_db_->Update(disabled_user);
    
    // 查询活跃用户
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.mobile_like = "199000001";
    params.disabled = false;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    // 只返回未禁用的用户
    for (const auto& user : result.Value()) {
        EXPECT_FALSE(user.disabled);
    }
}

// ==================== 辅助查询测试 ====================

TEST_F(UserDBTest, ExistsByMobile_True) {
    CreateTestUser("19900000170", "Exists Test");
    
    auto result = user_db_->ExistsByMobile("19900000170");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_TRUE(result.Value());
}

TEST_F(UserDBTest, ExistsByMobile_False) {
    auto result = user_db_->ExistsByMobile("00000000000");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_FALSE(result.Value());
}

TEST_F(UserDBTest, Count_WithParams) {
    // 创建用户
    CreateTestUser("19900000180", "Count User 1");
    CreateTestUser("19900000181", "Count User 2");
    CreateTestUser("19900000182", "Count User 3");
    
    UserQueryParams params;
    params.mobile_like = "199000001";
    
    auto result = user_db_->Count(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_GE(result.Value(), 3);
}

// ==================== 边界条件测试 ====================

TEST_F(UserDBTest, EmptyDisplayName) {
    UserEntity user;
    user.mobile = "19900000190";
    user.display_name = "";  // 空昵称
    user.password_hash = "$sha256$salt$hash";
    
    auto result = user_db_->Create(user);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_TRUE(result.Value().display_name.empty());
}

TEST_F(UserDBTest, SpecialCharactersInDisplayName) {
    UserEntity user;
    user.mobile = "19900000191";
    user.display_name = "Test'User\"With<Special>Chars&123";
    user.password_hash = "$sha256$salt$hash";
    
    auto result = user_db_->Create(user);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().display_name, "Test'User\"With<Special>Chars&123");
}

TEST_F(UserDBTest, UnicodeDisplayName) {
    UserEntity user;
    user.mobile = "19900000192";
    user.display_name = "测试用户🎉";
    user.password_hash = "$sha256$salt$hash";
    
    auto result = user_db_->Create(user);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().display_name, "测试用户🎉");
}

} // namespace testing
} // namespace user_service
