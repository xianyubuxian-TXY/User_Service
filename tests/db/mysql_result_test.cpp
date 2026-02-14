// tests/db/mysql_result_test.cpp

#include "db_test_fixture.h"
#include "exception/mysql_exception.h"

namespace user_service {
namespace testing {

class MySQLResultTest : public DBTestFixture {};

// ==================== 基本遍历测试 ====================

TEST_F(MySQLResultTest, IterateRows) {
    auto guard = pool_->CreateConnection();
    
    // 创建多个测试用户
    CreateTestUser("19900000020", "User 1");
    CreateTestUser("19900000021", "User 2");
    CreateTestUser("19900000022", "User 3");
    
    auto res = guard->Query(
        "SELECT mobile, display_name FROM users WHERE mobile LIKE '199000000%' ORDER BY mobile",
        {}
    );
    
    int count = 0;
    while (res.Next()) {
        count++;
    }
    
    EXPECT_EQ(count, 3);
}

TEST_F(MySQLResultTest, RowCount) {
    auto guard = pool_->CreateConnection();
    
    CreateTestUser("19900000030", "Count Test 1");
    CreateTestUser("19900000031", "Count Test 2");
    
    auto res = guard->Query(
        "SELECT * FROM users WHERE mobile LIKE '1990000003%'",
        {}
    );
    
    EXPECT_EQ(res.RowCount(), 2);
    EXPECT_EQ(res.FieldCount(), 9);  // users 表有 9 个字段
}

TEST_F(MySQLResultTest, EmptyResult) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query(
        "SELECT * FROM users WHERE mobile = 'nonexistent'",
        {}
    );
    
    EXPECT_TRUE(res.Empty());
    EXPECT_EQ(res.RowCount(), 0);
    EXPECT_FALSE(res.Next());
}

// ==================== 按索引获取字段测试 ====================

TEST_F(MySQLResultTest, GetByIndex_String) {
    auto guard = pool_->CreateConnection();
    
    CreateTestUser("19900000040", "Index Test");
    
    auto res = guard->Query(
        "SELECT mobile, display_name FROM users WHERE mobile = '19900000040'",
        {}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString(0).value(), "19900000040");
    EXPECT_EQ(res.GetString(1).value(), "Index Test");
}

TEST_F(MySQLResultTest, GetByIndex_Int) {
    auto guard = pool_->CreateConnection();
    
    CreateTestUser("19900000041", "Int Test");
    
    auto res = guard->Query(
        "SELECT id, role, disabled FROM users WHERE mobile = '19900000041'",
        {}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_GT(res.GetInt(0).value(), 0);  // id > 0
    EXPECT_EQ(res.GetInt(1).value(), 0);  // role = 0
    EXPECT_EQ(res.GetInt(2).value(), 0);  // disabled = 0
}

TEST_F(MySQLResultTest, GetByIndex_OutOfRange) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT 1, 2", {});
    ASSERT_TRUE(res.Next());
    
    EXPECT_THROW({
        res.GetString(10);  // 越界
    }, MySQLResultException);
}

TEST_F(MySQLResultTest, GetByIndex_BeforeNext) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT 1", {});
    
    // 未调用 Next() 就获取数据
    EXPECT_THROW({
        res.GetString(0);
    }, MySQLResultException);
}

// ==================== 按列名获取字段测试 ====================

TEST_F(MySQLResultTest, GetByName_String) {
    auto guard = pool_->CreateConnection();
    
    CreateTestUser("19900000050", "Name Test");
    
    auto res = guard->Query(
        "SELECT mobile, display_name, uuid FROM users WHERE mobile = '19900000050'",
        {}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_EQ(res.GetString("mobile").value(), "19900000050");
    EXPECT_EQ(res.GetString("display_name").value(), "Name Test");
    EXPECT_FALSE(res.GetString("uuid").value().empty());
}

TEST_F(MySQLResultTest, GetByName_Int) {
    auto guard = pool_->CreateConnection();
    
    CreateTestUser("19900000051", "Int Name Test");
    
    auto res = guard->Query(
        "SELECT id, role FROM users WHERE mobile = '19900000051'",
        {}
    );
    
    ASSERT_TRUE(res.Next());
    EXPECT_GT(res.GetInt("id").value(), 0);
    EXPECT_EQ(res.GetInt("role").value(), 0);
}

TEST_F(MySQLResultTest, GetByName_NotFound) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT 1 AS num", {});
    ASSERT_TRUE(res.Next());
    
    EXPECT_THROW({
        res.GetString("nonexistent_column");
    }, std::out_of_range);
}

// ==================== NULL 值测试 ====================

TEST_F(MySQLResultTest, IsNull_True) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT NULL AS val", {});
    ASSERT_TRUE(res.Next());
    
    EXPECT_TRUE(res.IsNull("val"));
    EXPECT_TRUE(res.IsNull(0));
    EXPECT_FALSE(res.GetString("val").has_value());
}

TEST_F(MySQLResultTest, IsNull_False) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT 'not null' AS val", {});
    ASSERT_TRUE(res.Next());
    
    EXPECT_FALSE(res.IsNull("val"));
    EXPECT_TRUE(res.GetString("val").has_value());
}

// ==================== 类型转换测试 ====================

TEST_F(MySQLResultTest, GetDouble) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT 3.14159 AS pi, 2.71828 AS e", {});
    ASSERT_TRUE(res.Next());
    
    EXPECT_NEAR(res.GetDouble("pi").value(), 3.14159, 0.00001);
    EXPECT_NEAR(res.GetDouble("e").value(), 2.71828, 0.00001);
}

TEST_F(MySQLResultTest, GetIntFromString) {
    auto guard = pool_->CreateConnection();
    
    auto res = guard->Query("SELECT '12345' AS str_num", {});
    ASSERT_TRUE(res.Next());
    
    // MySQL 会自动转换
    EXPECT_EQ(res.GetInt("str_num").value(), 12345);
}

// ==================== 移动语义测试 ====================

TEST_F(MySQLResultTest, MoveConstruct) {
    auto guard = pool_->CreateConnection();
    
    auto res1 = guard->Query("SELECT 1 AS num, 'test' AS str", {});
    
    // 移动构造
    MySQLResult res2(std::move(res1));
    
    ASSERT_TRUE(res2.Next());
    EXPECT_EQ(res2.GetInt("num").value(), 1);
    EXPECT_EQ(res2.GetString("str").value(), "test");
}

TEST_F(MySQLResultTest, MoveAssign) {
    auto guard = pool_->CreateConnection();
    
    auto res1 = guard->Query("SELECT 1 AS a", {});
    auto res2 = guard->Query("SELECT 2 AS b", {});
    
    // 移动赋值
    res2 = std::move(res1);
    
    ASSERT_TRUE(res2.Next());
    EXPECT_EQ(res2.GetInt("a").value(), 1);
}

} // namespace testing
} // namespace user_service
