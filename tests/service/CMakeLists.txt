# tests/service/CMakeLists.txt

# AuthService 测试
add_executable(auth_service_test
    auth_service_test.cpp
)

target_include_directories(auth_service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/service
)

target_link_libraries(auth_service_test
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    user_service
    user_auth
    user_db
    user_cache
    user_common
    user_config
)

add_test(NAME auth_service_test COMMAND auth_service_test)

# UserService 测试
add_executable(user_service_test
    user_service_test.cpp
)

target_include_directories(user_service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/service
)

target_link_libraries(user_service_test
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    user_service
    user_auth
    user_db
    user_cache
    user_common
    user_config
)

add_test(NAME user_service_test COMMAND user_service_test)
