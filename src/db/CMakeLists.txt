add_library(user_db user_db.cpp mysql_connection.cpp mysql_result.cpp)

target_include_directories(user_db PUBLIC
            ${PROJECT_SOURCE_DIR}/include/db
            ${PROJECT_SOURCE_DIR}/include
            ${MYSQL_INCLUDE_DIRS} #MYSQL 头文件                    
)

target_link_libraries(user_db
    PUBLIC
        ${MYSQL_LIBRARIES}
        user_config
        uuid
)