add_library(user_common STATIC logger.cpp)

target_include_directories(user_common PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(user_common PUBLIC
    spdlog::spdlog
)