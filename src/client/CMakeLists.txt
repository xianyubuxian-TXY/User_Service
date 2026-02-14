add_library(user_client_lib STATIC
    auth_client.cpp
    user_client.cpp
)

target_include_directories(user_client_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(user_client_lib PUBLIC
    proto_lib
    user_common
    ${GRPC_LIBRARIES}
)

# 使用选项控制是否构建示例程序
option(BUILD_CLIENT_EXAMPLE "Build client example program" OFF)

if(BUILD_CLIENT_EXAMPLE)
    add_executable(client_example main.cpp)
    
    target_include_directories(client_example PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )
    
    target_link_libraries(client_example PRIVATE
        user_client_lib
        user_config
    )
endif()
