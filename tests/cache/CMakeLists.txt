# tests/cache/CMakeLists.txt

add_executable(redis_client_test
    redis_client_test.cpp
)

target_include_directories(redis_client_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(redis_client_test
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    user_cache
    user_config
    user_common
)

# 注册测试
add_test(NAME redis_client_test COMMAND redis_client_test)

# 设置测试标签（集成测试需要Redis）
set_tests_properties(redis_client_test PROPERTIES 
    LABELS "integration"
    ENVIRONMENT "REDIS_HOST=localhost;REDIS_PORT=6380"
)
