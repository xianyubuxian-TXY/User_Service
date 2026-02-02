set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test)

add_executable(service_test
    auth_service_test.cpp
    user_service_test.cpp
)

target_include_directories(service_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_test PRIVATE
    user_service
    user_auth
    user_cache
    user_db
    user_common
    user_config
    uuid
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME ServiceTest COMMAND service_test)
