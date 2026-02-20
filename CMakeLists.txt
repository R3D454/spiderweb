cmake_minimum_required(VERSION 3.16)
project(spiderweb CXX)

# Export compile_commands.json so language servers (clangd/VS Code) see
# the build-generated include paths (e.g. build/generated_proto).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Threads
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Protobuf  (prefer submodule, fall back to system)
# ---------------------------------------------------------------------------
set(PROTOBUF_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/protobuf")
if(EXISTS "${PROTOBUF_SUBMODULE}/CMakeLists.txt")
    set(protobuf_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROTOBUF_SUBMODULE}" "${CMAKE_BINARY_DIR}/protobuf_build" EXCLUDE_FROM_ALL)
    set(PROTOC_EXECUTABLE $<TARGET_FILE:protoc>)
    set(PROTOBUF_LIBRARIES protobuf::libprotobuf)
    set(PROTOBUF_INCLUDE_DIRS "${PROTOBUF_SUBMODULE}/src")
else()
    find_package(Protobuf REQUIRED)
    set(PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}")
    set(PROTOBUF_LIBRARIES ${Protobuf_LIBRARIES})
    set(PROTOBUF_INCLUDE_DIRS ${Protobuf_INCLUDE_DIRS})
endif()

# ---------------------------------------------------------------------------
# libzmq  (prefer submodule, fall back to system)
# ---------------------------------------------------------------------------
set(LIBZMQ_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/libzmq")
if(EXISTS "${LIBZMQ_SUBMODULE}/CMakeLists.txt")
    set(WITH_PERF_TOOL     OFF CACHE BOOL "" FORCE)
    set(ZMQ_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(ENABLE_CPACK       OFF CACHE BOOL "" FORCE)
    add_subdirectory("${LIBZMQ_SUBMODULE}" "${CMAKE_BINARY_DIR}/libzmq_build" EXCLUDE_FROM_ALL)
    set(ZMQ_LIBRARIES libzmq)
    set(ZMQ_INCLUDE_DIRS "${LIBZMQ_SUBMODULE}/include")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ZMQ REQUIRED libzmq)
endif()

# ---------------------------------------------------------------------------
# cppzmq  (prefer submodule, fall back to system header)
# ---------------------------------------------------------------------------
set(CPPZMQ_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/cppzmq")
if(EXISTS "${CPPZMQ_SUBMODULE}/zmq.hpp")
    set(CPPZMQ_INCLUDE_DIR "${CPPZMQ_SUBMODULE}")
else()
    # Try a system location
    find_path(CPPZMQ_INCLUDE_DIR zmq.hpp PATH_SUFFIXES zmqpp)
    if(NOT CPPZMQ_INCLUDE_DIR)
        message(FATAL_ERROR "cppzmq not found. Please init submodules or install cppzmq.")
    endif()
endif()

# ---------------------------------------------------------------------------
# stduuid  (optional header-only submodule for UUID generation)
# ---------------------------------------------------------------------------
set(STDUUID_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/stduuid")
if(EXISTS "${STDUUID_SUBMODULE}/include/uuid.h")
    set(STDUUID_INCLUDE_DIR "${STDUUID_SUBMODULE}/include")
elseif(EXISTS "${STDUUID_SUBMODULE}/uuid.h")
    set(STDUUID_INCLUDE_DIR "${STDUUID_SUBMODULE}")
else()
    set(STDUUID_INCLUDE_DIR "")
endif()

# ---------------------------------------------------------------------------
# Catch2 (unit tests)
# ---------------------------------------------------------------------------
set(CATCH2_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/Catch2")
if(EXISTS "${CATCH2_SUBMODULE}/CMakeLists.txt")
    add_subdirectory("${CATCH2_SUBMODULE}" "${CMAKE_BINARY_DIR}/catch2_build" EXCLUDE_FROM_ALL)
    set(HAS_CATCH2 TRUE)
else()
    set(HAS_CATCH2 FALSE)
endif()

# ---------------------------------------------------------------------------
# loguru (optional logging library)
# ---------------------------------------------------------------------------
set(LOGURU_SUBMODULE "${CMAKE_SOURCE_DIR}/third_party/loguru")
if(EXISTS "${LOGURU_SUBMODULE}/CMakeLists.txt")
    # loguru provides headers and a small library; prefer submodule when present
    add_subdirectory("${LOGURU_SUBMODULE}" "${CMAKE_BINARY_DIR}/loguru_build" EXCLUDE_FROM_ALL)
    set(LOGURU_INCLUDE_DIR "${LOGURU_SUBMODULE}/src")
else()
    set(LOGURU_INCLUDE_DIR "")
endif()

# ---------------------------------------------------------------------------
# libuuid  (optional – used for UUID generation when uuid/uuid.h is present)
# ---------------------------------------------------------------------------
find_library(UUID_LIBRARY NAMES uuid)
if(UUID_LIBRARY)
    set(UUID_LIBRARIES "${UUID_LIBRARY}")
else()
    set(UUID_LIBRARIES "")
endif()

# ---------------------------------------------------------------------------
# Protobuf code generation
# ---------------------------------------------------------------------------
set(GEN_PROTO_DIR "${CMAKE_BINARY_DIR}/generated_proto")
file(MAKE_DIRECTORY "${GEN_PROTO_DIR}")

set(PROTO_SRCS
    "${CMAKE_SOURCE_DIR}/proto/transport.proto"
    "${CMAKE_SOURCE_DIR}/proto/event.proto"
)

set(GENERATED_SRCS "")
set(GENERATED_HDRS "")
foreach(proto ${PROTO_SRCS})
    get_filename_component(proto_name "${proto}" NAME_WE)
    set(gen_src "${GEN_PROTO_DIR}/${proto_name}.pb.cc")
    set(gen_hdr "${GEN_PROTO_DIR}/${proto_name}.pb.h")
    list(APPEND GENERATED_SRCS "${gen_src}")
    list(APPEND GENERATED_HDRS "${gen_hdr}")

    add_custom_command(
        OUTPUT  "${gen_src}" "${gen_hdr}"
        COMMAND "${PROTOC_EXECUTABLE}"
                "--proto_path=${CMAKE_SOURCE_DIR}/proto"
                "--proto_path=${PROTOBUF_INCLUDE_DIRS}"
                "--cpp_out=${GEN_PROTO_DIR}"
                "${proto}"
        DEPENDS "${proto}"
        COMMENT "Generating protobuf for ${proto_name}.proto"
    )
endforeach()

add_custom_target(generate_protos ALL DEPENDS ${GENERATED_SRCS} ${GENERATED_HDRS})

# ---------------------------------------------------------------------------
# spiderweb executable
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/udp_transport.cpp
    src/storage.cpp
    src/deduplicator.cpp
    src/zmq_fetch.cpp
    src/heartbeat.cpp
    src/spiderweb_node.cpp
    ${GENERATED_SRCS}
)

add_executable(spiderweb ${SOURCES})
add_dependencies(spiderweb generate_protos)

# Enable SSE/MMX intrinsics compile options when building for x86 targets so
# headers like <mmintrin.h> see the proper builtins. This ensures compile
# flags are exported to `compile_commands.json` for language servers.
set(SSE_COMPILE_OPTIONS "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
        # Ensure MMX/SSE builtins are available to headers like <mmintrin.h>.
        # Add -mmmx as some intrinsics require explicit MMX target support.
        set(SSE_COMPILE_OPTIONS -mmmx -msse -msse2)
    elseif(MSVC)
        set(SSE_COMPILE_OPTIONS /arch:SSE2)
    endif()
endif()

if(SSE_COMPILE_OPTIONS)
    target_compile_options(spiderweb PRIVATE ${SSE_COMPILE_OPTIONS})
endif()

target_include_directories(spiderweb PRIVATE
    src
    "${GEN_PROTO_DIR}"
    ${PROTOBUF_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    "${CPPZMQ_INCLUDE_DIR}"
)

if(STDUUID_INCLUDE_DIR)
    target_include_directories(spiderweb PRIVATE "${STDUUID_INCLUDE_DIR}")
endif()

target_link_libraries(spiderweb PRIVATE
    ${PROTOBUF_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${UUID_LIBRARIES}
    Threads::Threads
    loguru::loguru
)

# ---------------------------------------------------------------------------
# Unit tests (Catch2)
# ---------------------------------------------------------------------------
option(ENABLE_TESTS "Build unit tests" ON)
if(ENABLE_TESTS)
    if(NOT HAS_CATCH2)
        message(FATAL_ERROR "Catch2 not found. Please init submodules or install Catch2.")
    endif()

    enable_testing()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

    add_executable(unit_tests
        tests/test_main.cpp
        ${GENERATED_SRCS}
    )

    if(SSE_COMPILE_OPTIONS)
        target_compile_options(unit_tests PRIVATE ${SSE_COMPILE_OPTIONS})
    endif()

    target_include_directories(unit_tests PRIVATE
        src
        "${GEN_PROTO_DIR}"
        ${PROTOBUF_INCLUDE_DIRS}
    )

    if(LOGURU_INCLUDE_DIR)
        add_compile_definitions(LOGURU_WITH_STREAMS SQLITE_THREADSAFE=2)
        target_include_directories(unit_tests PRIVATE "${LOGURU_INCLUDE_DIR}")
    endif()

    target_link_libraries(unit_tests PRIVATE
        Catch2::Catch2WithMain
        ${PROTOBUF_LIBRARIES}
        ${ZMQ_LIBRARIES}
        Threads::Threads
    )

    add_test(NAME unit_tests COMMAND unit_tests)
endif()
