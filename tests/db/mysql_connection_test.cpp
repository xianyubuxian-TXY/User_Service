// tests/db/mysql_connection_test.cpp

#include "db_test_fixture.h"
#include "exception/mysql_exception.h"

namespace user_service {
namespace testing {

class MySQLConnectionTest : public DBTestFixture {};

// ==================== 连接测试 ====================

TEST_F(MySQLConnectionTest, ConnectSuccess) {
    // 测试成功连接
    ASSERT_NO_THROW({
        MySQLConnection conn(config_);
        EXPECT_TRUE(conn.Valid());
    });
}

TEST_F(MySQLConnectionTest, ConnectFailure_WrongPassword) {
    // 测试错误密码
    MySQLConfig bad_config = config_;
    bad_config.password = "wrong_password";
    bad_config.max_retries = 0;  // 不重试
    
    EXPECT_THROW({
        MySQLConnection conn(bad_config);
    }, MySQLException);
}

TEST_F(MySQLConnectionTest, ConnectFailure_WrongHost) {
    // 测试错误主机
    MySQLConfig bad_config = config_;
    bad_config.host = "non_existent_host";
    bad_config.max_retries = 0;
    bad_config.connection_timeout_ms = 1000;
    
    EXPECT_THROW({
        MySQLConnection conn(bad_config);
    }, MySQLException);
}

// ==================== 查询测试 ====================

TEST_F(MySQLConnectionTest, QuerySimple) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT 1 AS num, 'hello' AS str", {});
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetInt("num").value(), 1);
    EXPECT_EQ(res.GetString("str").value(), "hello");
    EXPECT_FALSE(res.Next());  // 只有一行
}

TEST_F(MySQLConnectionTest, QueryWithParameters) {
    auto guard = pool_->CreateConnection();
    
    // 创建测试用户
    std::string mobile = "19900000001";
    std::string uuid = CreateTestUser(mobile, "Query Test");
    
    // 参数化查询
    auto res = guard->Query(
        "SELECT uuid, display_name FROM users WHERE mobile = ?",
        {mobile}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("uuid").value(), uuid);
    EXPECT_EQ(res.GetString("display_name").value(), "Query Test");
}

TEST_F(MySQLConnectionTest, QueryWithMultipleParameters) {
    auto guard = pool_->CreateConnection();
    
    // 创建测试用户
    CreateTestUser("19900000002", "User A");
    CreateTestUser("19900000003", "User B");
    
    // 多参数查询
    auto res = guard->Query(
        "SELECT * FROM users WHERE mobile LIKE ? AND disabled = ? ORDER BY mobile",
        {std::string("1990000000%"), false}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("mobile").value(), "19900000002");
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("mobile").value(), "19900000003");
    
    EXPECT_FALSE(res.Next());
}

TEST_F(MySQLConnectionTest, QueryEmpty) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query(
        "SELECT * FROM users WHERE mobile = ?",
        {std::string("nonexistent")}
    );
    
    EXPECT_FALSE(res.Next());
    EXPECT_TRUE(res.Empty());
}

TEST_F(MySQLConnectionTest, QuerySyntaxError) {
    auto guard = pool_->CreateConnection();
    
    EXPECT_THROW({
        guard->Query("SELECT * FORM users", {});  // FORM 拼写错误
    }, MySQLException);
}

// ==================== 执行测试 ====================

TEST_F(MySQLConnectionTest, ExecuteInsert) {
    auto guard = pool_->CreateConnection();
    
    uint64_t affected = guard->Execute(
        "INSERT INTO users (uuid, mobile, display_name, password_hash, role) "
        "VALUES (?, ?, ?, ?, ?)",
        {
            std::string("test-uuid-insert"),
            std::string("19900000010"),
            std::string("Insert Test"),
            std::string("$sha256$salt$hash"),
            int64_t(0)
        }
    );
    
    EXPECT_EQ(affected, 1);
    
    // 验证插入成功
    auto res = guard->Query("SELECT * FROM users WHERE mobile = ?", 
                            {std::string("19900000010")});
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("display_name").value(), "Insert Test");
}

TEST_F(MySQLConnectionTest, ExecuteUpdate) {
    auto guard = pool_->CreateConnection();
    
    // 先创建用户
    CreateTestUser("19900000011", "Before Update");
    
    // 更新
    uint64_t affected = guard->Execute(
        "UPDATE users SET display_name = ? WHERE mobile = ?",
        {std::string("After Update"), std::string("19900000011")}
    );
    
    EXPECT_EQ(affected, 1);
    
    // 验证更新成功
    auto res = guard->Query("SELECT display_name FROM users WHERE mobile = ?",
                            {std::string("19900000011")});
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("display_name").value(), "After Update");
}

TEST_F(MySQLConnectionTest, ExecuteDelete) {
    auto guard = pool_->CreateConnection();
    
    // 先创建用户
    CreateTestUser("19900000012", "To Delete");
    
    // 删除
    uint64_t affected = guard->Execute(
        "DELETE FROM users WHERE mobile = ?",
        {std::string("19900000012")}
    );
    
    EXPECT_EQ(affected, 1);
    
    // 验证删除成功
    auto res = guard->Query("SELECT * FROM users WHERE mobile = ?",
                            {std::string("19900000012")});
    EXPECT_FALSE(res.Next());
}

TEST_F(MySQLConnectionTest, ExecuteDuplicateKey) {
    auto guard = pool_->CreateConnection();
    
    // 先创建用户
    CreateTestUser("19900000013", "Original");
    
    // 尝试插入相同手机号
    EXPECT_THROW({
        guard->Execute(
            "INSERT INTO users (uuid, mobile, display_name, password_hash, role) "
            "VALUES (?, ?, ?, ?, ?)",
            {
                std::string("test-uuid-dup"),
                std::string("19900000013"),  // 重复的手机号
                std::string("Duplicate"),
                std::string("$sha256$salt$hash"),
                int64_t(0)
            }
        );
    }, MySQLDuplicateKeyException);
}

TEST_F(MySQLConnectionTest, LastInsertId) {
    auto guard = pool_->CreateConnection();
    
    guard->Execute(
        "INSERT INTO users (uuid, mobile, display_name, password_hash, role) "
        "VALUES (?, ?, ?, ?, ?)",
        {
            std::string("test-uuid-lastid"),
            std::string("19900000014"),
            std::string("Last Insert ID Test"),
            std::string("$sha256$salt$hash"),
            int64_t(0)
        }
    );
    
    uint64_t last_id = guard->LastInsertId();
    EXPECT_GT(last_id, 0);
    
    // 验证 ID 正确
    auto res = guard->Query("SELECT id FROM users WHERE mobile = ?",
                            {std::string("19900000014")});
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetInt("id").value(), static_cast<int64_t>(last_id));
}

// ==================== 参数类型测试 ====================

TEST_F(MySQLConnectionTest, ParameterTypes_Nullptr) {
    auto guard = pool_->CreateConnection();
    
    // 创建用户时 display_name 可以为 NULL（如果表允许）
    // 这里用 SELECT 测试
    auto res = guard->Query("SELECT ? AS val", {nullptr});
    ASSERT_TRUE(res.Next());
    EXPECT_TRUE(res.IsNull("val"));
}

TEST_F(MySQLConnectionTest, ParameterTypes_Bool) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT ? AS t, ? AS f", {true, false});
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetInt("t").value(), 1);
    EXPECT_EQ(res.GetInt("f").value(), 0);
}

TEST_F(MySQLConnectionTest, ParameterTypes_Numbers) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query(
        "SELECT ? AS i64, ? AS u64, ? AS dbl",
        {int64_t(-123), uint64_t(456), double(3.14159)}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetInt("i64").value(), -123);
    EXPECT_EQ(res.GetInt("u64").value(), 456);
    EXPECT_NEAR(res.GetDouble("dbl").value(), 3.14159, 0.0001);
}

TEST_F(MySQLConnectionTest, ParameterTypes_StringEscape) {
    auto guard = pool_->CreateConnection();
    
    // 测试 SQL 注入防护
    std::string dangerous = "'; DROP TABLE users; --";
    auto res = guard->Query("SELECT ? AS val", {dangerous});
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("val").value(), dangerous);
}

// ==================== 连接池测试 ====================

TEST_F(MySQLConnectionTest, ConnectionPool_MultipleConnections) {
    // 获取多个连接
    std::vector<MySQLPool::ConnectionGuard> guards;
    
    for (int i = 0; i < 3; ++i) {
        guards.push_back(pool_->CreateConnection());
        EXPECT_TRUE(guards.back().get() != nullptr);
    }
    
    // 验证连接都可用
    for (auto& guard : guards) {
        auto res = guard->Query("SELECT 1", {});
        EXPECT_TRUE(res.Next());
    }
}

TEST_F(MySQLConnectionTest, ConnectionPool_ReuseConnection) {
    // 获取连接并归还
    {
        auto guard = pool_->CreateConnection();
        guard->Query("SELECT 1", {});
    }
    
    // 再次获取（应该复用）
    {
        auto guard = pool_->CreateConnection();
        EXPECT_TRUE(guard->Valid());
        guard->Query("SELECT 2", {});
    }
}

} // namespace testing
} // namespace user_service
