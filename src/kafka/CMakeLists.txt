add_library(user_message kafka_producer.cpp)

target_include_directories(user_message PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/thirdparty
    ${RDKAFKA_INCLUDE_DIRS}
)

target_link_libraries(user_message 
    PUBLIC
    ${RDKAFKA_LIBRARIES}
    user_config
)
