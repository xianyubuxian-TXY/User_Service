# tests/discovery/CMakeLists.txt

# ==================== 单元测试（不需要真实 ZK）====================
set(DISCOVERY_UNIT_TEST_SOURCES
    service_instance_test.cpp
    service_registry_test.cpp
    service_discovery_test.cpp
    zk_client_test.cpp
)

add_executable(discovery_test ${DISCOVERY_UNIT_TEST_SOURCES})

target_include_directories(discovery_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/discovery
)

target_link_libraries(discovery_test
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    user_discovery
    user_common
)

add_test(NAME discovery_test COMMAND discovery_test)

# ==================== 集成测试（需要真实 ZK）====================
# 通过 CMake 选项控制是否编译
option(BUILD_INTEGRATION_TESTS "Build integration tests that require external services" OFF)

if(BUILD_INTEGRATION_TESTS)
    add_executable(discovery_integration_test
        zk_integration_test.cpp
    )
    
    target_include_directories(discovery_integration_test PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests/discovery
    )
    
    target_link_libraries(discovery_integration_test
        GTest::gtest
        GTest::gtest_main
        user_discovery
        user_common
        user_config
    )
    
    # 不自动加入 ctest，因为需要外部依赖
    # 手动运行：./bin/discovery_integration_test
    add_test(NAME discovery_integration_test COMMAND discovery_integration_test)
    
    # 标记为需要外部服务
    set_tests_properties(discovery_integration_test PROPERTIES
        LABELS "integration"
        ENVIRONMENT "ZK_HOSTS=localhost:2181"
    )
endif()
