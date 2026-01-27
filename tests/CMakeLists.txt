# ======================================= Logger Test ======================================

# add_executable(logger_test logger_test.cpp)

# target_include_directories(logger_test PUBLIC
#         ${PROJECT_SOURCE_DIR}/include
# )

# target_link_directories(logger_test PUBLIC
#     ${PROJECT_SOURCE_DIR}/build/lib
# )

# target_link_libraries(logger_test PRIVATE
#     user_common
#     GTest::GTest
#     GTest::Main
#     pthread
# )

# # 只在测试代码中启用 debug 级别日志
# target_compile_definitions(logger_test PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)


# # 设置输出路径
# set_target_properties(logger_test PROPERTIES 
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests
# )

# ======================================= config Test ======================================

# add_executable(config_test config_test.cpp)

# target_include_directories(config_test PUBLIC 
#     ${PROJECT_SOURCE_DIR}/include
# )

# target_link_directories(config_test PUBLIC
#     ${PROJECT_SOURCE_DIR}/include/lib
# )

# target_link_libraries(config_test PRIVATE
#     user_config
#     GTest::GTest
#     GTest::Main
#     pthread
#     yaml-cpp  # Config 模块需要 yaml-cpp 库
# )

# # set_target_properties：CMake 内置命令，用于设置目标（target）的属性
# # config_test：目标名称
# # PROPERTIES：关键字，表示后面是要设置的属性及其值
# # RUNTIME_OUTPUT_DIRECTORY：目标属性名
# # ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests：属性值（路径）
# set_target_properties(config_test PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests
# )


# # ======================================= redis Test ======================================
# add_executable(db_test db_test.cpp)

# target_include_directories(db_test PUBLIC
#     ${PROJECT_SOURCE_DIR}/include
#     ${PROJECT_SOURCE_DIR}/src
# )

# target_link_directories(db_test PUBLIC
#     ${PROJECT_SOURCE_DIR}/include/lib
# )

# target_link_libraries(db_test PRIVATE
#     user_common
#     user_config
#     user_db
#     GTest::gtest
#     GTest::gtest_main
#     GTest::gmock
# )

# set_target_properties(db_test PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests
# )

# # 添加测试
# add_test(NAME db_test COMMAND db_test)

# # 设置测试环境变量（可选）
# set_tests_properties(db_test PROPERTIES
#     ENVIRONMENT "TEST_MYSQL_HOST=localhost;TEST_MYSQL_PORT=3306;TEST_MYSQL_USER=test_user;TEST_MYSQL_PASSWORD=test_pass"
# )

# # ======================================= redis Test ======================================
# add_executable(redis_test redis_test.cpp)

# target_include_directories(redis_test PUBLIC
#     ${PROJECT_SOURCE_DIR}/include
# )

# target_link_directories(redis_test PUBLIC
#     ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib
# )

# target_link_libraries(redis_test PRIVATE
#     user_cache        # 你的 Redis 客户端库
#     user_config       # 配置管理
#     user_common       # 日志等
#     ${REDIS_PLUS_PLUS_LIB}  # redis++ 库
#     GTest::gtest
#     GTest::gtest_main
#     GTest::gmock
# )

# set_target_properties(redis_test PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tests
# )

# # 添加测试
# add_test(NAME redis_test COMMAND redis_test)

# # 设置测试环境变量
# set_tests_properties(redis_test PROPERTIES
#     ENVIRONMENT "TEST_REDIS_HOST=localhost;TEST_REDIS_PORT=6379;TEST_REDIS_DB=15"
#     TIMEOUT 60  # 60 秒超时
# )
