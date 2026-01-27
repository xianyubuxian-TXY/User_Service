add_library(user_config STATIC config.cpp)

target_include_directories(user_config PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(user_config PUBLIC
    yaml-cpp::yaml-cpp
    user_common
)