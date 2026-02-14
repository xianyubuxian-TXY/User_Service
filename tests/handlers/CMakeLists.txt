# tests/handlers/CMakeLists.txt

# 添加测试可执行文件
add_executable(handlers_test
    auth_handler_test.cpp
    user_handler_test.cpp
)

# 包含目录
target_include_directories(handlers_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/handlers
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接库
target_link_libraries(handlers_test PRIVATE
    user_handler
    user_service
    user_auth
    proto_lib
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
)

# 添加到 CTest
include(GoogleTest)
gtest_discover_tests(handlers_test)
