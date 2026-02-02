# ============================================================
# Proto 编译配置
# ============================================================
cmake_minimum_required(VERSION 3.16)

# 查找 grpc_cpp_plugin
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# Proto 文件列表
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/pb_common/result.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/pb_auth/auth.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/pb_user/user.proto
)

# 输出目录
set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

# 收集生成文件
set(PROTO_SRCS "")
set(PROTO_HDRS "")  # 新增：收集头文件
set(GRPC_SRCS "")
set(GRPC_HDRS "")   # 新增

foreach(PROTO_FILE ${PROTO_FILES})
    file(RELATIVE_PATH PROTO_REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILE})
    get_filename_component(PROTO_REL_DIR ${PROTO_REL_PATH} DIRECTORY)
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    
    file(MAKE_DIRECTORY "${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}")
    
    set(PROTO_CC "${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.cc")
    set(PROTO_H  "${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.pb.h")
    set(GRPC_CC  "${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.grpc.pb.cc")
    set(GRPC_H   "${PROTO_OUTPUT_DIR}/${PROTO_REL_DIR}/${PROTO_NAME}.grpc.pb.h")
    
    list(APPEND PROTO_SRCS ${PROTO_CC})
    list(APPEND PROTO_HDRS ${PROTO_H})
    list(APPEND GRPC_SRCS ${GRPC_CC})
    list(APPEND GRPC_HDRS ${GRPC_H})
    
    add_custom_command(
        OUTPUT ${PROTO_CC} ${PROTO_H} ${GRPC_CC} ${GRPC_H}
        
        COMMAND protobuf::protoc
            --cpp_out=${PROTO_OUTPUT_DIR}
            --grpc_out=${PROTO_OUTPUT_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            -I${CMAKE_CURRENT_SOURCE_DIR}
            ${PROTO_FILE}
        
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating: ${PROTO_REL_PATH}"
        VERBATIM  # 新增：正确处理特殊字符
    )
endforeach()

# 创建库
add_library(proto_lib STATIC 
    ${PROTO_SRCS} 
    ${GRPC_SRCS}
)

# 设置属性
target_include_directories(proto_lib PUBLIC ${PROTO_OUTPUT_DIR})
target_link_libraries(proto_lib PUBLIC protobuf::libprotobuf gRPC::grpc++)

# 新增：设置 C++ 标准（proto 生成的代码需要）
target_compile_features(proto_lib PUBLIC cxx_std_17)

# 新增：抑制 proto 生成代码的警告
target_compile_options(proto_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-parameter>
)
