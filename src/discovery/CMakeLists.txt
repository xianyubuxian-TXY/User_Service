add_library(user_discovery 
    service_registry.cpp 
    service_discovery.cpp 
    zk_client.cpp
)

target_include_directories(user_discovery PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/discovery
    ${PROJECT_SOURCE_DIR}/thirdparty
    ${ZOOKEEPER_INCLUDE_DIRS}   # pkg_check_modules 设置的是 _DIRS（复数）
)

# 关键：定义 THREADED 宏，启用 ZooKeeper 同步 API
target_compile_definitions(user_discovery PUBLIC
    THREADED
)

target_link_libraries(user_discovery PUBLIC
    ${ZOOKEEPER_LIBRARIES}      # pkg_check_modules 设置的是 _LIBRARIES（复数）
    user_common
)
