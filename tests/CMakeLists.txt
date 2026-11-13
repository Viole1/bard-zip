add_executable(bardzip_tests
    main.cpp
    crc32_tests.cpp
    arguments_tests.cpp
    compression_tests.cpp
    container_tests.cpp
    poem_codec_tests.cpp
    integration_tests.cpp
    temporary_file_tests.cpp

    ${PROJECT_SOURCE_DIR}/src/cli/arguments.cpp
)

target_link_libraries(bardzip_tests PRIVATE bardzip_core)

add_test(NAME bardzip_tests COMMAND bardzip_tests)
