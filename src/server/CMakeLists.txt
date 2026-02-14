# src/server/CMakeLists.txt

add_library(user_server_lib
    grpc_server.cpp
    server_builder.cpp
)

target_include_directories(user_server_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/thirdparty
    ${GRPC_INCLUDE_DIRS}
)

target_link_libraries(user_server_lib PUBLIC
    proto_lib
    user_handler
    user_service
    user_auth
    user_db
    user_cache
    user_common
    user_config
    user_discovery
    ${GRPC_LIBRARIES}
)

add_dependencies(user_server_lib proto_lib)

# User Service 可执行文件
add_executable(user_server user_main.cpp)
target_link_libraries(user_server PRIVATE user_server_lib)

# Auth Service 可执行文件
add_executable(auth_server auth_main.cpp)
target_link_libraries(auth_server PRIVATE user_server_lib)
