cmake_minimum_required(VERSION 3.16)
project(UserService
        VERSION 1.0.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)	# 指定使用 C++17 标准
set(CMAKE_CXX_STANDARD_REQUIRED ON) #强制要求编译器支持 C++17，若编译器不支持，直接报错
set(CMAKE_CXX_EXTENSIONS OFF) #禁用编译器扩展语法， 保证代码在不同编译器下行为一致

# 核心逻辑：若用户未指定 CMAKE_BUILD_TYPE（如编译时未加 -DCMAKE_BUILD_TYPE=Debug），
# 默认设置为 Release（优化编译，去除调试信息，适合部署）；
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif()

option(BUILD_TESTS "Build unit tests" ON) # 控制是否编译单元测试代码（tests 子目录）
# 控制生成 “动态库（.so/.dll）” 还是 “静态库（.a/.lib）”，默认 OFF 即生成静态库；
option(BUILD_SHARED_LIBS "Build shared instead of static libs" OFF) 


# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# YAML 配置
find_package(yaml-cpp CONFIG REQUIRED)

# spdlog 日志
find_package(spdlog CONFIG REQUIRED)

# MySQL
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# hiredis 库 
find_path(HIREDIS_HEADER hiredis)
find_library(HIREDIS_LIB hiredis)

# redis++ 库 
# 注意：这里是 sw，不是 redis++
find_path(REDIS_PLUS_PLUS_HEADER sw)
find_library(REDIS_PLUS_PLUS_LIB redis++)

# Kafka
find_package(PkgConfig REQUIRED)
pkg_check_modules(RdKafka REQUIRED IMPORTED_TARGET rdkafka++)

# gRPC
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)

# 查找 OpenSSL（jwt_service 需要）
find_package(OpenSSL REQUIRED)

# 查找 fmt
find_package(fmt REQUIRED)

add_subdirectory(api)
add_subdirectory(src)


# GTest 包
find_package(GTest REQUIRED)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()