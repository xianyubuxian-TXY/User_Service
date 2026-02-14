├── .dockerignore
├── .gitignore
├── CMakeLists.txt
├── README.md
├── api
│   ├── CMakeLists.txt
│   ├── README.md
│   └── proto
│       ├── CMakeLists.txt
│       ├── pb_auth
│       │   └── auth.proto
│       ├── pb_common
│       │   └── result.proto
│       └── pb_user
│           └── user.proto
├── benchmark_results
│   ├── concurrency_10.json
│   ├── concurrency_100.json
│   ├── concurrency_200.json
│   ├── concurrency_50.json
│   ├── concurrency_500.json
│   ├── get_user.json
│   ├── login.json
│   ├── report_20260214_110529.md
│   └── validate_token.json
├── configs
│   ├── README.md
│   ├── config.docker.yaml
│   └── config.yaml
├── deploy
│   └── docker
│       ├── Dockerfile
│       ├── README.md
│       └── entrypoint.sh
├── docker-compose.yml
├── include
│   ├── auth
│   │   ├── README.md
│   │   ├── authenticator.h
│   │   ├── jwt_authenticator.h
│   │   ├── jwt_service.h
│   │   ├── sms_service.h
│   │   ├── token_cleanup_task.h
│   │   └── token_repository.h
│   ├── cache
│   │   ├── README.md
│   │   └── redis_client.h
│   ├── client
│   │   ├── README.md
│   │   ├── auth_client.h
│   │   ├── client_options.h
│   │   └── user_client.h
│   ├── common
│   │   ├── README.md
│   │   ├── auth_type.h
│   │   ├── error_codes.h
│   │   ├── logger.h
│   │   ├── password_helper.h
│   │   ├── proto_converter.h
│   │   ├── proto_utils.h
│   │   ├── result.h
│   │   ├── time_utils.h
│   │   ├── uuid.h
│   │   └── validator.h
│   ├── config
│   │   ├── README.md
│   │   └── config.h
│   ├── db
│   │   ├── README.md
│   │   ├── mysql_connection.h
│   │   ├── mysql_result.h
│   │   └── user_db.h
│   ├── discovery
│   │   ├── README.md
│   │   ├── service_discovery.h
│   │   ├── service_instance.h
│   │   ├── service_registry.h
│   │   └── zk_client.h
│   ├── entity
│   │   ├── README.md
│   │   ├── page.h
│   │   ├── token.h
│   │   └── user_entity.h
│   ├── exception
│   │   ├── README.md
│   │   ├── client_exception.h
│   │   ├── exception.h
│   │   └── mysql_exception.h
│   ├── handlers
│   │   ├── READMD.md
│   │   ├── auth_handler.h
│   │   └── user_handler.h
│   ├── pool
│   │   ├── README.md
│   │   └── connection_pool.h
│   ├── server
│   │   ├── README.md
│   │   ├── grpc_server.h
│   │   └── server_builder.h
│   └── service
│       ├── README.md
│       ├── auth_service.h
│       └── user_service.h
├── project_all_code.txt
├── scripts
│   ├── benchmark.sh
│   └── init_db.sql
├── src
│   ├── CMakeLists.txt
│   ├── auth
│   │   ├── CMakeLists.txt
│   │   ├── jwt_service.cpp
│   │   ├── sms_service.cpp
│   │   ├── token_cleanup_task.cpp
│   │   └── token_repository.cpp
│   ├── cache
│   │   ├── CMakeLists.txt
│   │   └── redis_client.cpp
│   ├── client
│   │   ├── CMakeLists.txt
│   │   ├── auth_client.cpp
│   │   └── user_client.cpp
│   ├── common
│   │   ├── CMakeLists.txt
│   │   └── logger.cpp
│   ├── config
│   │   ├── CMakeLists.txt
│   │   └── config.cpp
│   ├── db
│   │   ├── CMakeLists.txt
│   │   ├── mysql_connection.cpp
│   │   ├── mysql_result.cpp
│   │   └── user_db.cpp
│   ├── discovery
│   │   ├── CMakeLists.txt
│   │   ├── service_discovery.cpp
│   │   ├── service_registry.cpp
│   │   └── zk_client.cpp
│   ├── handlers
│   │   ├── CMakeLists.txt
│   │   ├── auth_handler.cpp
│   │   └── user_handler.cpp
│   ├── server
│   │   ├── CMakeLists.txt
│   │   ├── auth_main.cpp
│   │   ├── grpc_server.cpp
│   │   ├── server_builder.cpp
│   │   └── user_main.cpp
│   └── services
│       ├── CMakeLists.txt
│       ├── auth_service.cpp
│       └── user_service.cpp
├── tests
│   ├── CMakeLists.txt
│   ├── auth
│   │   ├── CMakeLists.txt
│   │   ├── jwt_service_test.cpp
│   │   ├── mock_auth_deps.h
│   │   ├── sms_service_test.cpp
│   │   ├── token_cleanup_task_test.cpp
│   │   └── token_repository_test.cpp
│   ├── cache
│   │   ├── CMakeLists.txt
│   │   ├── mock_redis_client.h
│   │   └── redis_client_test.cpp
│   ├── client
│   │   ├── CMakeLists.txt
│   │   ├── auth_client_test.cpp
│   │   ├── mock_auth_service.h
│   │   └── user_client_test.cpp
│   ├── config
│   │   ├── CMakeLists.txt
│   │   ├── config_test.cpp
│   │   ├── invalid_config.yaml
│   │   └── test_config.yaml
│   ├── db
│   │   ├── CMakeLists.txt
│   │   ├── db_test_fixture.h
│   │   ├── mysql_connection_test.cpp
│   │   ├── mysql_result_test.cpp
│   │   └── user_db_test.cpp
│   ├── discovery
│   │   ├── CMakeLists.txt
│   │   ├── mock_zk_client.h
│   │   ├── service_discovery_test.cpp
│   │   ├── service_instance_test.cpp
│   │   ├── service_registry_test.cpp
│   │   ├── zk_client_test.cpp
│   │   └── zk_integration_test.cpp
│   ├── handlers
│   │   ├── CMakeLists.txt
│   │   ├── auth_handler_test.cpp
│   │   ├── mock_services.h
│   │   └── user_handler_test.cpp
│   ├── logger
│   │   ├── CMakeLists.txt
│   │   └── logger_test.cpp
│   ├── server
│   │   ├── CMakeLists.txt
│   │   ├── grpc_server_test.cpp
│   │   ├── mock_dependencies.h
│   │   ├── server_builder_test.cpp
│   │   └── test_config.yaml
│   └── service
│       ├── CMakeLists.txt
│       ├── auth_service_test.cpp
│       ├── mock_services.h
│       └── user_service_test.cpp
└── thirdparty
    └── json
        └── json.hpp
