# tests/logger/CMakeLists.txt

add_executable(logger_test
    logger_test.cpp
)

target_include_directories(logger_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(logger_test PRIVATE
    user_common
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

# 添加到 CTest
add_test(NAME LoggerTest COMMAND logger_test)

# 设置测试属性
set_tests_properties(LoggerTest PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
