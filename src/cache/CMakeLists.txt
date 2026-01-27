add_library(user_cache STATIC redis_client.cpp)

target_include_directories(user_cache PUBLIC
                ${PROJECT_SOURCE_DIR}/include
                ${HIREDIS_HEADER}
                ${REDIS_PLUS_PLUS_HEADER}
)

target_link_libraries(user_cache PUBLIC
        ${REDIS_PLUS_PLUS_LIB}
        ${HIREDIS_LIB}
        user_config
        pthread
)