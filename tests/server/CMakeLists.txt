# tests/server/CMakeLists.txt

add_executable(server_tests
    server_builder_test.cpp
    grpc_server_test.cpp
)

target_include_directories(server_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(server_tests
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    proto_lib           # 添加 proto_lib，确保能访问生成的头文件
    user_server_lib
)

# 确保 proto 文件先生成
add_dependencies(server_tests proto_lib)

# 复制测试配置文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.yaml
    ${CMAKE_CURRENT_BINARY_DIR}/test_config.yaml
    COPYONLY
)

add_test(NAME ServerTests COMMAND server_tests)
