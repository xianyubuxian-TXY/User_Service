# ============================================================
# Proto 文件列表
# ============================================================
# 列出所有需要编译的 .proto 文件
# 添加新 proto 只需在这里加一行
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/user_service.proto
)

# ============================================================
# 输出目录配置
# ============================================================
# 生成的 .pb.cc/.pb.h 文件放在构建目录
set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================
# 收集生成文件的列表（用于后续编译）
# ============================================================
set(PROTO_SRCS "")   # 存放 xxx.pb.cc 文件路径
set(GRPC_SRCS "")    # 存放 xxx.grpc.pb.cc 文件路径

# ============================================================
# 遍历每个 proto 文件，生成编译命令
# ============================================================
foreach(PROTO_FILE ${PROTO_FILES})
    
    # 提取文件名（不含路径和扩展名）
    # 例如：/path/to/auth.proto → auth
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    
    # 将生成的源文件路径添加到列表
    # auth.proto → auth.pb.cc, auth.grpc.pb.cc
    list(APPEND PROTO_SRCS "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.cc")
    list(APPEND GRPC_SRCS "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.cc")
    
    # --------------------------------------------------------
    # 核心：自定义命令，调用 protoc 生成 C++ 代码
    # --------------------------------------------------------
    add_custom_command(
        # OUTPUT: 声明此命令会生成哪些文件
        # CMake 用这个判断是否需要重新执行
        OUTPUT 
            "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.cc"      # protobuf 消息类
            "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.h"
            "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.cc" # gRPC 服务类
            "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.h"
        
        # COMMAND: 实际执行的命令
        # 等价于终端执行：
        # protoc --cpp_out=build/ --grpc_out=build/ \
        #        --plugin=protoc-gen-grpc=grpc_cpp_plugin \
        #        -I./proto auth.proto
        COMMAND protobuf::protoc
            --cpp_out=${PROTO_OUTPUT_DIR}       # 生成 .pb.cc/.pb.h
            --grpc_out=${PROTO_OUTPUT_DIR}      # 生成 .grpc.pb.cc/.grpc.pb.h
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}  # gRPC 插件路径
            -I${CMAKE_CURRENT_SOURCE_DIR}       # proto 文件搜索路径（处理 import）
            ${PROTO_FILE}                       # 输入的 proto 文件
        
        # DEPENDS: 依赖项，proto 文件改变时重新生成
        DEPENDS ${PROTO_FILE}
        
        # COMMENT: 编译时显示的消息
        COMMENT "Generating: ${PROTO_NAME}"
    )
endforeach()

# ============================================================
# 创建静态库：把所有生成的代码编译成一个库
# ============================================================
# 其他目标只需链接 proto_lib，不用关心具体文件
add_library(proto_lib STATIC ${PROTO_SRCS} ${GRPC_SRCS})

# ============================================================
# 设置头文件搜索路径
# ============================================================
# PUBLIC: 链接 proto_lib 的目标也能 #include 生成的头文件
# 这样其他代码可以直接写：
#   #include "auth.pb.h"
#   #include "auth.grpc.pb.h"
target_include_directories(proto_lib 
    PUBLIC ${PROTO_OUTPUT_DIR}
)

# ============================================================
# 链接依赖库
# ============================================================
# PUBLIC: 依赖会传递给链接 proto_lib 的目标
# 即：链接 proto_lib 自动获得 protobuf 和 grpc++ 的链接
target_link_libraries(proto_lib 
    PUBLIC 
        protobuf::libprotobuf  # protobuf 运行时库
        gRPC::grpc++           # gRPC C++ 库
)