set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test)

add_executable(auth_handler_test auth_handler_test.cpp)

target_include_directories(auth_handler_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/api/generated
)

target_link_libraries(auth_handler_test PRIVATE
    user_handler
    proto_lib
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)

add_dependencies(auth_handler_test proto_lib)

add_executable(user_handler_test user_handler_test.cpp)

target_include_directories(user_handler_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/api/generated
)

target_link_libraries(user_handler_test PRIVATE
    user_handler
    proto_lib
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)

add_dependencies(user_handler_test proto_lib)

include(GoogleTest)
gtest_discover_tests(auth_handler_test)
gtest_discover_tests(user_handler_test)
