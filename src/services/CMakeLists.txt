add_library(user_service auth_service.cpp user_service.cpp)


target_include_directories(user_service 
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/service
    ${CMAKE_CURRENT_SOURCE_DIR}

)

target_link_libraries(user_service 
    PUBLIC
    user_auth
    user_cache
    user_common
    user_config
    user_db
)