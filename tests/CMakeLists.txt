# add_subdirectory(auth)
# add_subdirectory(db)
# add_subdirectory(service)
add_subdirectory(handlers)
add_subdirectory(auth)
add_subdirectory(integration)

# ==================== gRPC 测试客户端 ====================
add_executable(test_client
    test_client.cpp
)

target_link_libraries(test_client
    proto_lib           # proto 生成的库
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
)

target_include_directories(test_client PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

set_target_properties(test_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test
)