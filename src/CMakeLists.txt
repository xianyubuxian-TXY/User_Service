add_subdirectory(config)
add_subdirectory(common)
add_subdirectory(db)
add_subdirectory(cache)
add_subdirectory(kafka)
add_subdirectory(services)
add_subdirectory(auth)
add_subdirectory(handlers)

add_executable(user_service_server
    main.cpp
)

target_link_libraries(user_service_server   
    proto_lib          # proto
    user_handler      # auth_handler, user_handler
    user_service       # auth_service, user_service
    user_auth          # jwt_service, sms_service, authenticator, token_repository
    user_db            # user_db, mysql_connection
    user_cache         # redis_client
    user_common        # logger, validator, error_codes
    user_config        # config
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)

target_include_directories(user_service_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)