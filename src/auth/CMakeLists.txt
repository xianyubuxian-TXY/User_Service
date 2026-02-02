add_library(user_auth token_cleanup_task.cpp token_repository.cpp jwt_service.cpp sms_service.cpp)

target_include_directories(user_auth PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/auth
)

target_link_libraries(user_auth PUBLIC
    user_db
    user_common
    user_cache
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
)
