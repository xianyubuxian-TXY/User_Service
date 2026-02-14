# api/proto/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

find_program(PROTOC protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# 使用 find_package 查找 Protobuf（更完整）
find_package(Protobuf REQUIRED)

# 使用 pkg-config 查找 gRPC
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++ grpc)

set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/pb_common/result.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/pb_auth/auth.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/pb_user/user.proto
)

set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(PROTO_SRCS "")
set(GRPC_SRCS "")

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
    list(APPEND GRPC_SRCS ${GRPC_CC})
    
    add_custom_command(
        OUTPUT ${PROTO_CC} ${PROTO_H} ${GRPC_CC} ${GRPC_H}
        COMMAND ${PROTOC}
            --cpp_out=${PROTO_OUTPUT_DIR}
            --grpc_out=${PROTO_OUTPUT_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            -I${CMAKE_CURRENT_SOURCE_DIR}
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating: ${PROTO_REL_PATH}"
        VERBATIM
    )
    
    set_source_files_properties(
        ${PROTO_CC} ${PROTO_H} ${GRPC_CC} ${GRPC_H}
        PROPERTIES GENERATED TRUE
    )
endforeach()

# 创建库
add_library(proto_lib STATIC 
    ${PROTO_SRCS} 
    ${GRPC_SRCS}
)

target_include_directories(proto_lib 
    PUBLIC 
        ${PROTO_OUTPUT_DIR}
        ${Protobuf_INCLUDE_DIRS}
        ${GRPC_INCLUDE_DIRS}
)

# 关键：正确链接所有 protobuf 库
target_link_libraries(proto_lib 
    PUBLIC 
        ${Protobuf_LIBRARIES}
        ${GRPC_LIBRARIES}
)

# 添加链接目录（pkg-config 可能需要）
target_link_directories(proto_lib
    PUBLIC
        ${GRPC_LIBRARY_DIRS}
)

target_compile_features(proto_lib PUBLIC cxx_std_17)

target_compile_options(proto_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-parameter -Wno-deprecated-declarations>
)
