# tests/integration/CMakeLists.txt 添加

# ==================== 设置测试可执行文件输出目录 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test)

add_executable(user_flow_integration_test
    user_flow_integration_test.cpp
)

target_link_libraries(user_flow_integration_test
    GTest::gtest_main
    GTest::gmock 
    user_service
    user_auth
    user_db
    user_cache
    user_common
    user_config
)

target_include_directories(user_flow_integration_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

add_test(NAME user_flow_integration_test COMMAND user_flow_integration_test)
