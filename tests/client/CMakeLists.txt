# tests/client/CMakeLists.txt

# Auth Client 测试
add_executable(auth_client_test
    auth_client_test.cpp
)

target_include_directories(auth_client_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(auth_client_test PRIVATE
    user_client_lib
    user_common
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME auth_client_test COMMAND auth_client_test)

# User Client 测试
add_executable(user_client_test
    user_client_test.cpp
)

target_include_directories(user_client_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(user_client_test PRIVATE
    user_client_lib
    user_common
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME user_client_test COMMAND user_client_test)
