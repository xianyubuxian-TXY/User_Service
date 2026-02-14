# tests/db/CMakeLists.txt

# 添加测试可执行文件
add_executable(db_tests
    db_test_fixture.h
    mysql_connection_test.cpp
    mysql_result_test.cpp
    user_db_test.cpp
)

# 链接依赖
target_link_libraries(db_tests
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        user_db
        user_config
        user_common
)

# 包含目录
target_include_directories(db_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests/db
)

# 注册测试
include(GoogleTest)
gtest_discover_tests(db_tests)
