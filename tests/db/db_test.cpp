// tests/db/db_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include "db/user_db.h"
#include "db/mysql_connection.h"
#include "pool/connection_pool.h"
#include "config/config.h"
#include "entity/user_entity.h"
#include "entity/page.h"

namespace user_service {

// ==================== UserDB 测试 Fixture ====================

class UserDBTestFixture : public ::testing::Test {
protected:
    using MySQLPool = UserDB::MySQLPool;

    void SetUp() override {
        CreateTestConfig();
        CreateTestPool();
        CreateTestTable();
        user_db_ = std::make_shared<UserDB>(pool_);
    }

    void TearDown() override {
        CleanupTestTable();
    }

    void CreateTestConfig() {
        config_ = std::make_shared<Config>();
        config_->mysql.host = GetEnvOrDefault("TEST_MYSQL_HOST", "localhost");
        config_->mysql.port = std::stoi(GetEnvOrDefault("TEST_MYSQL_PORT", "3306"));
        config_->mysql.username = GetEnvOrDefault("TEST_MYSQL_USER", "test_user");
        config_->mysql.password = GetEnvOrDefault("TEST_MYSQL_PASSWORD", "@txylj2864");
        config_->mysql.database = "test_db";
        config_->mysql.pool_size = 5;
        config_->mysql.connection_timeout_ms = 5000;
        config_->mysql.read_timeout_ms = 3000;
        config_->mysql.write_timeout_ms = 3000;
        config_->mysql.max_retries = 3;
        config_->mysql.retry_interval_ms = 1000;
        config_->mysql.auto_reconnect = true;
        config_->mysql.charset = "utf8mb4";
    }

    void CreateTestPool() {
        // TemplateConnectionPool 需要 shared_ptr<Config> 和 CreateFunc
        pool_ = std::make_shared<MySQLPool>(
            config_,
            [](const MySQLConfig& cfg) {
                return std::make_unique<MySQLConnection>(cfg);
            }
        );
    }

    void CreateTestTable() {
        auto conn = pool_->CreateConnection();
        
        // 创建 users 表
        conn->Execute(R"(
            CREATE TABLE IF NOT EXISTS users (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                uuid VARCHAR(36) NOT NULL,
                mobile VARCHAR(20) NOT NULL,
                password_hash VARCHAR(128) NOT NULL,
                display_name VARCHAR(64),
                disabled TINYINT(1) DEFAULT 0,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                UNIQUE KEY uk_uuid (uuid),
                UNIQUE KEY uk_mobile (mobile),
                INDEX idx_created_at (created_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");
    }

    void CleanupTestTable() {
        auto conn = pool_->CreateConnection();
        conn->Execute("TRUNCATE TABLE users");
    }

    // 辅助方法：插入单个测试用户
    void InsertTestUser(const std::string& uuid, const std::string& mobile, 
                        const std::string& display_name, bool disabled = false) {
        auto conn = pool_->CreateConnection();
        conn->Execute(
            "INSERT INTO users (uuid, mobile, password_hash, display_name, disabled) "
            "VALUES (?, ?, ?, ?, ?)",
            {uuid, mobile, "hash_" + mobile, display_name, disabled ? "1" : "0"}
        );
    }

    // 辅助方法：批量插入测试用户
    void InsertTestUsers(int count, bool some_disabled = false) {
        for (int i = 1; i <= count; ++i) {
            std::string uuid = "uuid_" + std::to_string(i);
            
            // 直接用数字：13800000001, 13800000002, ..., 13800000099
            std::string mobile = "138" + std::to_string(100000000 + i);
            
            std::string name = "用户" + std::to_string(i);
            bool disabled = some_disabled && (i % 3 == 0);
            InsertTestUser(uuid, mobile, name, disabled);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::string GetEnvOrDefault(const char* name, const std::string& default_val) {
        const char* val = std::getenv(name);
        return val ? val : default_val;
    }

    std::shared_ptr<Config> config_;
    std::shared_ptr<MySQLPool> pool_;
    std::shared_ptr<UserDB> user_db_;
};

// ==================== 基础 CRUD 测试 ====================

TEST_F(UserDBTestFixture, Create_Success) {
    UserEntity user;
    user.mobile = "13800000001";
    user.password_hash = "hash_test";
    user.display_name = "测试用户";
    
    auto result = user_db_->Create(user);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_FALSE(result.Value().uuid.empty());
    EXPECT_EQ(result.Value().mobile, "13800000001");
    EXPECT_EQ(result.Value().display_name, "测试用户");
}

TEST_F(UserDBTestFixture, Create_DuplicateMobile) {
    InsertTestUser("uuid_dup", "13800000002", "已存在用户");
    
    UserEntity user;
    user.mobile = "13800000002";  // 重复手机号
    user.password_hash = "hash_test";
    user.display_name = "新用户";
    
    auto result = user_db_->Create(user);
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.GetErrCode(), ErrorCode::MobileTaken);
}

TEST_F(UserDBTestFixture, FindByMobile_Success) {
    InsertTestUser("uuid_find", "13800000003", "查找用户");
    
    auto result = user_db_->FindByMobile("13800000003");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().uuid, "uuid_find");
    EXPECT_EQ(result.Value().display_name, "查找用户");
}

TEST_F(UserDBTestFixture, FindByMobile_NotFound) {
    auto result = user_db_->FindByMobile("13899999999");
    
    ASSERT_FALSE(result.IsOk());
    EXPECT_EQ(result.GetErrCode(), ErrorCode::UserNotFound);
}

TEST_F(UserDBTestFixture, FindByUUID_Success) {
    InsertTestUser("uuid_by_uuid", "13800000004", "UUID查找");
    
    auto result = user_db_->FindByUUID("uuid_by_uuid");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().mobile, "13800000004");
}

TEST_F(UserDBTestFixture, Update_Success) {
    InsertTestUser("uuid_update", "13800000005", "原名称");
    
    auto find_result = user_db_->FindByUUID("uuid_update");
    ASSERT_TRUE(find_result.IsOk());
    
    UserEntity user = find_result.Value();
    user.display_name = "新名称";
    
    auto update_result = user_db_->Update(user);
    ASSERT_TRUE(update_result.IsOk());
    
    // 验证更新
    auto verify_result = user_db_->FindByUUID("uuid_update");
    EXPECT_EQ(verify_result.Value().display_name, "新名称");
}

TEST_F(UserDBTestFixture, ExistsByMobile_True) {
    InsertTestUser("uuid_exists", "13800000006", "存在用户");
    
    auto result = user_db_->ExistsByMobile("13800000006");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_TRUE(result.Value());
}

TEST_F(UserDBTestFixture, ExistsByMobile_False) {
    auto result = user_db_->ExistsByMobile("13899999998");
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_FALSE(result.Value());
}

// ==================== FindAll(UserQueryParams) 测试 ====================

TEST_F(UserDBTestFixture, FindAllWithParams_NoFilter) {
    InsertTestUsers(5);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 5);
}

TEST_F(UserDBTestFixture, FindAllWithParams_MobileFilter) {
    InsertTestUser("uuid_m1", "13811112222", "用户A");
    InsertTestUser("uuid_m2", "13811113333", "用户B");
    InsertTestUser("uuid_m3", "13922224444", "用户C");
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.mobile_like = "1381111";  // 模糊匹配
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 2);  // 匹配 A 和 B
}

TEST_F(UserDBTestFixture, FindAllWithParams_DisabledFilter_True) {
    InsertTestUser("uuid_d1", "13800001001", "启用用户1", false);
    InsertTestUser("uuid_d2", "13800001002", "禁用用户1", true);
    InsertTestUser("uuid_d3", "13800001003", "启用用户2", false);
    InsertTestUser("uuid_d4", "13800001004", "禁用用户2", true);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.disabled = true;  // 只查禁用的
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 2);
    
    for (const auto& user : result.Value()) {
        EXPECT_TRUE(user.disabled);
    }
}

TEST_F(UserDBTestFixture, FindAllWithParams_DisabledFilter_False) {
    InsertTestUser("uuid_e1", "13800002001", "启用用户1", false);
    InsertTestUser("uuid_e2", "13800002002", "禁用用户1", true);
    InsertTestUser("uuid_e3", "13800002003", "启用用户2", false);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.disabled = false;  // 只查启用的
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 2);
    
    for (const auto& user : result.Value()) {
        EXPECT_FALSE(user.disabled);
    }
}

TEST_F(UserDBTestFixture, FindAllWithParams_CombinedFilters) {
    InsertTestUser("uuid_c1", "13811110001", "用户A", false);
    InsertTestUser("uuid_c2", "13811110002", "用户B", true);   // 匹配手机+禁用
    InsertTestUser("uuid_c3", "13811110003", "用户C", true);   // 匹配手机+禁用
    InsertTestUser("uuid_c4", "13922220001", "用户D", true);   // 不匹配手机
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.mobile_like = "1381111";
    params.disabled = true;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 2);  // B 和 C
}

TEST_F(UserDBTestFixture, FindAllWithParams_Pagination_FirstPage) {
    InsertTestUsers(15);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 5;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 5);
}

TEST_F(UserDBTestFixture, FindAllWithParams_Pagination_MiddlePage) {
    InsertTestUsers(15);
    
    UserQueryParams params;
    params.page_params.page = 2;
    params.page_params.page_size = 5;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 5);
}

TEST_F(UserDBTestFixture, FindAllWithParams_Pagination_LastPage) {
    InsertTestUsers(12);  // 12条，每页5条，第3页只有2条
    
    UserQueryParams params;
    params.page_params.page = 3;
    params.page_params.page_size = 5;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 2);
}

TEST_F(UserDBTestFixture, FindAllWithParams_Pagination_EmptyPage) {
    InsertTestUsers(5);
    
    UserQueryParams params;
    params.page_params.page = 10;  // 超出范围的页
    params.page_params.page_size = 5;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 0);
}

TEST_F(UserDBTestFixture, FindAllWithParams_OrderByCreatedAtDesc) {
    // 按时间顺序插入
    InsertTestUser("uuid_o1", "13800003001", "第一个");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    InsertTestUser("uuid_o2", "13800003002", "第二个");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    InsertTestUser("uuid_o3", "13800003003", "第三个");
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    ASSERT_EQ(result.Value().size(), 3);
    
    // 应该按 created_at DESC 排序，最新的在前面
    EXPECT_EQ(result.Value()[0].display_name, "第三个");
    EXPECT_EQ(result.Value()[1].display_name, "第二个");
    EXPECT_EQ(result.Value()[2].display_name, "第一个");
}

TEST_F(UserDBTestFixture, FindAllWithParams_EmptyMobileFilter) {
    InsertTestUsers(3);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.mobile_like = "";  // 空字符串应该不过滤
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 3);
}

TEST_F(UserDBTestFixture, FindAllWithParams_NoMatch) {
    InsertTestUsers(5);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.mobile_like = "99999999999";  // 不存在的手机号
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 0);
}

// ==================== Count(UserQueryParams) 测试 ====================

TEST_F(UserDBTestFixture, Count_NoFilter) {
    InsertTestUsers(10);
    
    UserQueryParams params;
    
    auto result = user_db_->Count(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 10);
}

TEST_F(UserDBTestFixture, Count_WithMobileFilter) {
    InsertTestUser("uuid_cm1", "13811115555", "用户A");
    InsertTestUser("uuid_cm2", "13811116666", "用户B");
    InsertTestUser("uuid_cm3", "13922227777", "用户C");
    
    UserQueryParams params;
    params.mobile_like = "1381111";
    
    auto result = user_db_->Count(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 2);
}

TEST_F(UserDBTestFixture, Count_WithDisabledFilter) {
    InsertTestUser("uuid_cd1", "13800004001", "用户1", false);
    InsertTestUser("uuid_cd2", "13800004002", "用户2", true);
    InsertTestUser("uuid_cd3", "13800004003", "用户3", false);
    InsertTestUser("uuid_cd4", "13800004004", "用户4", true);
    InsertTestUser("uuid_cd5", "13800004005", "用户5", true);
    
    // 统计禁用的
    UserQueryParams params_disabled;
    params_disabled.disabled = true;
    
    auto result_disabled = user_db_->Count(params_disabled);
    ASSERT_TRUE(result_disabled.IsOk());
    EXPECT_EQ(result_disabled.Value(), 3);
    
    // 统计启用的
    UserQueryParams params_enabled;
    params_enabled.disabled = false;
    
    auto result_enabled = user_db_->Count(params_enabled);
    ASSERT_TRUE(result_enabled.IsOk());
    EXPECT_EQ(result_enabled.Value(), 2);
}

TEST_F(UserDBTestFixture, Count_CombinedFilters) {
    InsertTestUser("uuid_cc1", "13811118001", "用户A", false);
    InsertTestUser("uuid_cc2", "13811118002", "用户B", true);
    InsertTestUser("uuid_cc3", "13811118003", "用户C", true);
    InsertTestUser("uuid_cc4", "13922228001", "用户D", true);
    
    UserQueryParams params;
    params.mobile_like = "1381111";
    params.disabled = true;
    
    auto result = user_db_->Count(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 2);  // B 和 C
}

TEST_F(UserDBTestFixture, Count_EmptyTable) {
    // 不插入任何数据
    UserQueryParams params;
    
    auto result = user_db_->Count(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 0);
}

TEST_F(UserDBTestFixture, Count_NoMatch) {
    InsertTestUsers(5);
    
    UserQueryParams params;
    params.mobile_like = "99999999999";
    
    auto result = user_db_->Count(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 0);
}

// ==================== Count 和 FindAll 一致性测试 ====================

TEST_F(UserDBTestFixture, CountAndFindAll_Consistency) {
    InsertTestUsers(20, true);  // 20个用户，部分禁用
    
    // 使用相同的过滤条件
    UserQueryParams params;
    params.mobile_like = "13800000";
    params.disabled = false;
    params.page_params.page = 1;
    params.page_params.page_size = 100;  // 足够大，获取所有
    
    auto count_result = user_db_->Count(params);
    auto find_result = user_db_->FindAll(params);
    
    ASSERT_TRUE(count_result.IsOk());
    ASSERT_TRUE(find_result.IsOk());
    
    // Count 结果应该等于 FindAll 返回的数量
    EXPECT_EQ(count_result.Value(), static_cast<int64_t>(find_result.Value().size()));
}

TEST_F(UserDBTestFixture, CountAndFindAll_PaginationConsistency) {
    InsertTestUsers(25);
    
    UserQueryParams params;
    params.page_params.page_size = 10;
    
    // 获取总数
    auto count_result = user_db_->Count(params);
    ASSERT_TRUE(count_result.IsOk());
    EXPECT_EQ(count_result.Value(), 25);
    
    // 分页获取所有，累加应该等于总数
    int64_t total_fetched = 0;
    for (int page = 1; page <= 3; ++page) {
        params.page_params.page = page;
        auto result = user_db_->FindAll(params);
        ASSERT_TRUE(result.IsOk());
        total_fetched += result.Value().size();
    }
    
    EXPECT_EQ(total_fetched, count_result.Value());
}

// ==================== 边界情况测试 ====================

TEST_F(UserDBTestFixture, FindAllWithParams_SpecialCharsInMobileFilter) {
    InsertTestUser("uuid_sp1", "13800005001", "正常用户");
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 10;
    params.mobile_like = "%';--";  // SQL 注入尝试
    
    auto result = user_db_->FindAll(params);
    
    // 应该正常执行，不会 SQL 注入
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 0);  // 没有匹配
}

TEST_F(UserDBTestFixture, FindAllWithParams_LargePageSize) {
    InsertTestUsers(10);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 1000;  // 大于实际数据量
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 10);  // 只返回实际数量
}

TEST_F(UserDBTestFixture, FindAllWithParams_PageSizeOne) {
    InsertTestUsers(5);
    
    UserQueryParams params;
    params.page_params.page = 1;
    params.page_params.page_size = 1;
    
    auto result = user_db_->FindAll(params);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().size(), 1);
}

// ==================== Update bool 参数测试 ====================

TEST_F(UserDBTestFixture, Update_WithBoolDisabled) {
    InsertTestUser("uuid_upd", "13800006001", "更新测试", false);
    
    // 查询原始数据
    auto find_result = user_db_->FindByUUID("uuid_upd");
    ASSERT_TRUE(find_result.IsOk());
    EXPECT_FALSE(find_result.Value().disabled);
    
    // 更新 disabled 为 true
    UserEntity user = find_result.Value();
    user.disabled = true;
    
    auto update_result = user_db_->Update(user);
    ASSERT_TRUE(update_result.IsOk());
    
    // 验证更新结果
    auto verify_result = user_db_->FindByUUID("uuid_upd");
    ASSERT_TRUE(verify_result.IsOk());
    EXPECT_TRUE(verify_result.Value().disabled);
}

TEST_F(UserDBTestFixture, Update_DisabledToggle) {
    InsertTestUser("uuid_tog", "13800006002", "切换测试", true);
    
    // 验证初始状态
    auto find1 = user_db_->FindByUUID("uuid_tog");
    ASSERT_TRUE(find1.IsOk());
    EXPECT_TRUE(find1.Value().disabled);
    
    // 切换为 false
    UserEntity user1 = find1.Value();
    user1.disabled = false;
    ASSERT_TRUE(user_db_->Update(user1).IsOk());
    
    auto find2 = user_db_->FindByUUID("uuid_tog");
    EXPECT_FALSE(find2.Value().disabled);
    
    // 再切换为 true
    UserEntity user2 = find2.Value();
    user2.disabled = true;
    ASSERT_TRUE(user_db_->Update(user2).IsOk());
    
    auto find3 = user_db_->FindByUUID("uuid_tog");
    EXPECT_TRUE(find3.Value().disabled);
}

// ==================== Delete 测试 ====================

TEST_F(UserDBTestFixture, Delete_Success) {
    InsertTestUser("uuid_del", "13800007001", "待删除用户");
    
    auto find_before = user_db_->FindByUUID("uuid_del");
    ASSERT_TRUE(find_before.IsOk());
    
    auto delete_result = user_db_->DeleteByUUID("uuid_del");
    ASSERT_TRUE(delete_result.IsOk());
    
    auto find_after = user_db_->FindByUUID("uuid_del");
    EXPECT_FALSE(find_after.IsOk());
    EXPECT_EQ(find_after.GetErrCode(), ErrorCode::UserNotFound);
}

TEST_F(UserDBTestFixture, Delete_NotFound) {
    auto result = user_db_->DeleteByUUID("uuid_not_exist");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.GetErrCode(), ErrorCode::UserNotFound);
}

}  // namespace user_service
