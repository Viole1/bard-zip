add_executable(bardzip_tests
    main.cpp
    crc32_tests.cpp
    arguments_tests.cpp
    compression_tests.cpp
    container_tests.cpp
    poem_codec_tests.cpp
    integration_tests.cpp
    streaming_tests.cpp
    compatibility_tests.cpp
    temporary_file_tests.cpp

    ${PROJECT_SOURCE_DIR}/src/cli/arguments.cpp
)

target_link_libraries(bardzip_tests PRIVATE bardzip_core)

target_compile_definitions(bardzip_tests PRIVATE
    BARDZIP_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    BARDZIP_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
)

add_test(NAME bardzip_tests COMMAND bardzip_tests)
