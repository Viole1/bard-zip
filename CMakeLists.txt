cmake_minimum_required(VERSION 3.25)

project(BardZip
    VERSION 0.2.0
    LANGUAGES CXX
)

option(BARDZIP_BUILD_TESTS "Build BardZip tests" ON)
option(BARDZIP_STATIC_RUNTIME
    "Link the MSVC runtime statically so bardzip.exe runs without the VC++ redistributable" ON)

if(MSVC AND BARDZIP_STATIC_RUNTIME)
    # Must be set before any target (including zstd) is created.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

configure_file(
    cmake/bardzip_version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/bardzip_version.hpp
    @ONLY
)

find_package(zstd CONFIG QUIET)

if(TARGET zstd::libzstd_static)
    set(BARDZIP_ZSTD_TARGET zstd::libzstd_static)
elseif(TARGET zstd::libzstd)
    set(BARDZIP_ZSTD_TARGET zstd::libzstd)
elseif(TARGET zstd::libzstd_shared)
    set(BARDZIP_ZSTD_TARGET zstd::libzstd_shared)
else()
    include(FetchContent)

    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        SOURCE_SUBDIR build/cmake
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(zstd)

    set(BARDZIP_ZSTD_TARGET libzstd_static)
endif()

add_library(bardzip_core STATIC
    src/codec/byte_codec.cpp
    src/codec/dictionary.cpp
    src/codec/poem_codec.cpp
    src/compression/compression.cpp
    src/container/container.cpp
    src/container/crc32.cpp
    src/container/endian.cpp
    src/container/header.cpp
    src/cli/arguments.cpp
    src/cli/commands.cpp
    src/cli/console_progress.cpp
    src/cli/exit_codes.cpp
    src/core/file_operations.cpp
    src/core/size_format.cpp
    src/detect/file_type.cpp
    src/io/file_reader.cpp
    src/io/file_writer.cpp
    src/io/prefetch_stream.cpp
    src/io/stdio.cpp
    src/io/temporary_file.cpp
)

target_include_directories(bardzip_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(bardzip_core PRIVATE ${BARDZIP_ZSTD_TARGET})

target_compile_features(bardzip_core PUBLIC cxx_std_20)

target_compile_options(bardzip_core PUBLIC
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

add_executable(bardzip
    src/main.cpp
)

if(WIN32)
    # Application resources: icon, version information (shown in the file
    # properties) and the manifest (UTF-8 command line, long paths, no elevation).
    enable_language(RC)

    set(BARDZIP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/bardzip.ico")
    configure_file(
        resources/bardzip.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/bardzip.rc
        @ONLY
    )

    target_sources(bardzip PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated/bardzip.rc
        resources/bardzip.manifest
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_BINARY_DIR}/generated/bardzip.rc
        PROPERTIES OBJECT_DEPENDS ${BARDZIP_ICON_PATH}
    )
endif()

target_link_libraries(bardzip PRIVATE bardzip_core)

if(BARDZIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(BARDZIP_BUILD_TOOLS "Build developer tools (stage profiler)" OFF)

if(BARDZIP_BUILD_TOOLS)
    add_executable(bardzip_stage_profile tools/stage_profile.cpp)
    target_link_libraries(bardzip_stage_profile PRIVATE bardzip_core)
endif()

# libFuzzer targets (MSVC AddressSanitizer + libFuzzer). Configure a separate
# build directory with the instrumentation on every target, see docs/*/fuzzing.md:
#   -DCMAKE_C_FLAGS=/fsanitize=fuzzer -DCMAKE_CXX_FLAGS=/fsanitize=fuzzer
#   -DBARDZIP_STATIC_RUNTIME=OFF -DBARDZIP_BUILD_FUZZERS=ON
option(BARDZIP_BUILD_FUZZERS "Build libFuzzer targets" OFF)

if(BARDZIP_BUILD_FUZZERS)
    foreach(name poem header zstd extract)
        add_executable(fuzz_${name} tools/fuzz/fuzz_${name}.cpp)
        target_link_libraries(fuzz_${name} PRIVATE bardzip_core)
    endforeach()
endif()
