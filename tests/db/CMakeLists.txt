# ==================== 设置测试可执行文件输出目录 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test)

# ==================== MySQL 数据库测试 ====================
add_executable(db_test db_test.cpp)

target_link_libraries(db_test PRIVATE
    user_db          # 数据库模块（MySQLConnection, UserDB 等）
    user_config      # 配置模块
    GTest::gtest
    GTest::gtest_main
    GTest::gmock     # 如果用到了 gmock
)

target_include_directories(db_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

add_test(NAME DBTest COMMAND db_test)

# ==================== 设置测试环境变量（可选）====================
set_tests_properties(DBTest PROPERTIES
    ENVIRONMENT "TEST_MYSQL_HOST=localhost;TEST_MYSQL_PORT=3306"
)
