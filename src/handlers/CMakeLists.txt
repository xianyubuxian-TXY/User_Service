add_library(user_handler auth_handler.cpp)

target_include_directories(user_handler PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/handlers
)

# 链接 proto_lib，自动继承其 include 路径
target_link_libraries(user_handler PUBLIC
    proto_lib
)