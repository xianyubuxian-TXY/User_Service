add_library(user_service user_service_impl.cpp)


target_include_directories(user_service 
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/service
    ${CMAKE_BINARY_DIR}/api/proto

)

target_link_libraries(user_service 
    PUBLIC
    proto_lib
)