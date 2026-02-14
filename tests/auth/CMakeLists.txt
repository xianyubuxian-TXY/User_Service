# tests/auth/CMakeLists.txt

# 查找依赖
find_package(GTest REQUIRED)

# ============================================================================
# JWT Service 测试
# ============================================================================
add_executable(jwt_service_test
    jwt_service_test.cpp
)

target_include_directories(jwt_service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/auth
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jwt_service_test PRIVATE
    user_auth
    user_common
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME jwt_service_test COMMAND jwt_service_test)

# ============================================================================
# SMS Service 测试
# ============================================================================
add_executable(sms_service_test
    sms_service_test.cpp
)

target_include_directories(sms_service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/auth
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sms_service_test PRIVATE
    user_auth
    user_cache
    user_common
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME sms_service_test COMMAND sms_service_test)

# ============================================================================
# Token Repository 集成测试
# ============================================================================
add_executable(token_repository_test
    token_repository_test.cpp
)

target_include_directories(token_repository_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/auth
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(token_repository_test PRIVATE
    user_auth
    user_db
    user_common
    user_config
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME token_repository_test COMMAND token_repository_test)

# ============================================================================
# Token Cleanup Task 测试
# ============================================================================
add_executable(token_cleanup_task_test
    token_cleanup_task_test.cpp
)

target_include_directories(token_cleanup_task_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/auth
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(token_cleanup_task_test PRIVATE
    user_auth
    user_common
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME token_cleanup_task_test COMMAND token_cleanup_task_test)
