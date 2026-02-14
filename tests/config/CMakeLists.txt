# tests/config/CMakeLists.txt

add_executable(config_test config_test.cpp)

target_link_libraries(config_test
    PRIVATE
        user_config
        user_common
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_include_directories(config_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# 复制测试配置文件到构建目录
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.yaml
    ${CMAKE_CURRENT_BINARY_DIR}/test_config.yaml
    COPYONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.yaml
    ${CMAKE_CURRENT_BINARY_DIR}/invalid_config.yaml
    COPYONLY
)

add_test(NAME config_test COMMAND config_test)
