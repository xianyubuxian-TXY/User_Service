# tests/auth/CMakeLists.txt

# ==================== 设置测试可执行文件输出目录 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test)

# ==================== JwtService 单元测试 ====================
add_executable(jwt_service_test jwt_service_test.cpp)

target_link_libraries(jwt_service_test PRIVATE
    user_auth
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(jwt_service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

add_test(NAME JwtServiceTest COMMAND jwt_service_test)

# ==================== TokenRepository 集成测试 ====================
add_executable(token_repository_test token_repository_test.cpp)

target_link_libraries(token_repository_test PRIVATE
    user_auth
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(token_repository_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

add_test(NAME TokenRepositoryTest COMMAND token_repository_test)

# ==================== TokenCleanupTask 测试 ====================
add_executable(token_cleanup_task_test token_cleanup_task_test.cpp)

target_link_libraries(token_cleanup_task_test PRIVATE
    user_auth
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(token_cleanup_task_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

add_test(NAME TokenCleanupTaskTest COMMAND token_cleanup_task_test)

# ==================== SmsService 测试 ====================
add_executable(sms_service_test sms_service_test.cpp)

target_link_libraries(sms_service_test PRIVATE
    user_auth
    user_cache
    user_config
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(sms_service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

add_test(NAME SmsServiceTest COMMAND sms_service_test)