# tests/CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

# 查找 GTest
find_package(GTest REQUIRED)
include(GoogleTest)

# 启用测试
enable_testing()

# ==================== 设置测试可执行文件输出目录 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

# 公共测试依赖
set(TEST_COMMON_LIBS
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
)

# 公共测试配置（MySQL连接信息）
add_compile_definitions(
    TEST_MYSQL_HOST="localhost"
    TEST_MYSQL_PORT=3307
    TEST_MYSQL_USER="root"
    TEST_MYSQL_PASSWORD="@txylj2864"
    TEST_MYSQL_DATABASE="user_service"
)

# 添加子目录
add_subdirectory(logger)
add_subdirectory(config)
add_subdirectory(cache)
add_subdirectory(db)

add_subdirectory(auth)
add_subdirectory(discovery)
add_subdirectory(service)
add_subdirectory(handlers)

add_subdirectory(server)
add_subdirectory(client)
