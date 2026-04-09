cmake_minimum_required(VERSION 3.20)
project(owt_agent LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

set(OWT_AGENT_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if (NOT EXISTS "${OWT_AGENT_THIRD_PARTY_DIR}/spdlog/CMakeLists.txt")
  set(OWT_AGENT_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../owt-net/third_party")
endif()

if (NOT EXISTS "${OWT_AGENT_THIRD_PARTY_DIR}/spdlog/CMakeLists.txt")
  message(FATAL_ERROR "third_party/spdlog not found, expected in owt-agent/third_party or ../owt-net/third_party")
endif()
if (NOT EXISTS "${OWT_AGENT_THIRD_PARTY_DIR}/nlohmann/CMakeLists.txt")
  message(FATAL_ERROR "third_party/nlohmann not found, expected in owt-agent/third_party or ../owt-net/third_party")
endif()

set(OWT_AGENT_SQLITE3_SOURCE "${OWT_AGENT_THIRD_PARTY_DIR}/sqlite3/sqlite3.c")
if (NOT EXISTS "${OWT_AGENT_SQLITE3_SOURCE}")
  message(FATAL_ERROR "third_party/sqlite3/sqlite3.c not found")
endif()

if(NOT DEFINED OPENSSL_ROOT_DIR OR OPENSSL_ROOT_DIR STREQUAL "")
  file(
    GLOB _owt_sdk_openssl_roots
    "${CMAKE_CURRENT_SOURCE_DIR}/../openwrt-sdk-*/staging_dir/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../openwrt-sdk-*/staging_dir/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/../openwrt/openwrt-sdk-*/staging_dir/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../openwrt/openwrt-sdk-*/staging_dir/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/../openwrt-src/openwrt/staging_dir/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../openwrt-src/openwrt/staging_dir/host"
  )
  list(LENGTH _owt_sdk_openssl_roots _owt_sdk_openssl_root_count)
  if(_owt_sdk_openssl_root_count GREATER 0)
    list(GET _owt_sdk_openssl_roots 0 _owt_sdk_openssl_root)
    set(OPENSSL_ROOT_DIR "${_owt_sdk_openssl_root}" CACHE PATH "OpenSSL root path" FORCE)
    message(STATUS "Auto-detected OPENSSL_ROOT_DIR from OpenWrt SDK: ${OPENSSL_ROOT_DIR}")
  endif()
endif()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL)
find_path(LIBSSH2_INCLUDE_DIR libssh2.h)
find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2)
find_package(Protobuf QUIET)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(${OWT_AGENT_THIRD_PARTY_DIR}/spdlog ${CMAKE_CURRENT_BINARY_DIR}/third_party/spdlog EXCLUDE_FROM_ALL)
add_subdirectory(${OWT_AGENT_THIRD_PARTY_DIR}/nlohmann ${CMAKE_CURRENT_BINARY_DIR}/third_party/nlohmann EXCLUDE_FROM_ALL)

set(OWT_AGENT_TARGET owt_agent)
set(OWT_AGENT_ENABLE_GRPC_TRANSPORT 0)
set(OWT_AGENT_GRPC_LINK_LIBS)
set(OWT_AGENT_GRPC_INCLUDE_DIRS)
set(OWT_AGENT_PROTO_GENERATED_SOURCES)
set(OWT_AGENT_PROTO_GENERATED_HEADERS)

if (NOT Protobuf_PROTOC_EXECUTABLE)
  find_program(Protobuf_PROTOC_EXECUTABLE protoc)
endif()

if (PKG_CONFIG_FOUND)
  pkg_check_modules(GRPCPP QUIET grpc++)
endif()

find_path(GRPCPP_INCLUDE_DIR grpcpp/grpcpp.h)
find_library(GRPCPP_LIBRARY NAMES grpc++)
find_library(GRPC_LIBRARY NAMES grpc)
find_library(GPR_LIBRARY NAMES gpr)

set(_OWT_AGENT_GRPC_DEPS_READY FALSE)
if (Protobuf_PROTOC_EXECUTABLE AND GRPC_CPP_PLUGIN_EXECUTABLE AND (GRPCPP_FOUND OR (GRPCPP_INCLUDE_DIR AND GRPCPP_LIBRARY)))
  set(_OWT_AGENT_GRPC_DEPS_READY TRUE)
endif()

if (_OWT_AGENT_GRPC_DEPS_READY)
  set(OWT_AGENT_ENABLE_GRPC_TRANSPORT 1)
  set(OWT_AGENT_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/control_channel.proto)
  set(OWT_AGENT_PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/proto)
  set(OWT_AGENT_PROTO_GENERATED_SOURCES
    ${OWT_AGENT_PROTO_GEN_DIR}/control_channel.pb.cc
    ${OWT_AGENT_PROTO_GEN_DIR}/control_channel.grpc.pb.cc
  )
  set(OWT_AGENT_PROTO_GENERATED_HEADERS
    ${OWT_AGENT_PROTO_GEN_DIR}/control_channel.pb.h
    ${OWT_AGENT_PROTO_GEN_DIR}/control_channel.grpc.pb.h
  )

  add_custom_command(
    OUTPUT ${OWT_AGENT_PROTO_GENERATED_SOURCES} ${OWT_AGENT_PROTO_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OWT_AGENT_PROTO_GEN_DIR}
    COMMAND
      ${Protobuf_PROTOC_EXECUTABLE}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
      --cpp_out=${OWT_AGENT_PROTO_GEN_DIR}
      --grpc_out=${OWT_AGENT_PROTO_GEN_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
      ${OWT_AGENT_PROTO_FILE}
    DEPENDS ${OWT_AGENT_PROTO_FILE}
    VERBATIM
  )

  if (GRPCPP_FOUND)
    list(APPEND OWT_AGENT_GRPC_LINK_LIBS ${GRPCPP_LIBRARIES})
  else()
    list(APPEND OWT_AGENT_GRPC_LINK_LIBS ${GRPCPP_LIBRARY} ${GRPC_LIBRARY} ${GPR_LIBRARY})
  endif()

  if (TARGET protobuf::libprotobuf)
    list(APPEND OWT_AGENT_GRPC_LINK_LIBS protobuf::libprotobuf)
  else()
    find_library(PROTOBUF_LIBRARY NAMES protobuf)
    if (PROTOBUF_LIBRARY)
      list(APPEND OWT_AGENT_GRPC_LINK_LIBS ${PROTOBUF_LIBRARY})
    endif()
  endif()

  if (GRPCPP_FOUND)
    list(APPEND OWT_AGENT_GRPC_INCLUDE_DIRS ${GRPCPP_INCLUDE_DIRS})
  else()
    list(APPEND OWT_AGENT_GRPC_INCLUDE_DIRS ${GRPCPP_INCLUDE_DIR})
  endif()
  message(STATUS "gRPC transport enabled")
else()
  message(WARNING "gRPC transport disabled: missing protoc/grpc_cpp_plugin/grpc++ development files")
endif()

set(OWT_AGENT_SOURCES
  src/core/main.cpp
  src/core/log.cpp
  src/core/config.cpp
  src/core/command_store.cpp
  src/core/host_probe_agent.cpp
  src/core/params_store.cpp
  src/core/wakeonlan_sender.cpp
  src/core/ssh_executor.cpp
  src/core/control/agent_runtime.cpp
  src/core/control/control_protocol.cpp
  src/core/control/control_json_codec.cpp
  src/core/control/grpc_codec.cpp
  src/core/control/wss_control_channel.cpp
  src/core/control/grpc_control_channel.cpp
  ${OWT_AGENT_SQLITE3_SOURCE}
)

if (OWT_AGENT_ENABLE_GRPC_TRANSPORT)
  list(APPEND OWT_AGENT_SOURCES ${OWT_AGENT_PROTO_GENERATED_SOURCES})
endif()

add_executable(${OWT_AGENT_TARGET} ${OWT_AGENT_SOURCES})

set_target_properties(${OWT_AGENT_TARGET} PROPERTIES OUTPUT_NAME "owt-agent")

target_include_directories(${OWT_AGENT_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OWT_AGENT_THIRD_PARTY_DIR}/boost
    ${OWT_AGENT_THIRD_PARTY_DIR}/sqlite3
    ${OWT_AGENT_GRPC_INCLUDE_DIRS}
)

if (OWT_AGENT_ENABLE_GRPC_TRANSPORT)
  target_include_directories(${OWT_AGENT_TARGET} PRIVATE ${OWT_AGENT_PROTO_GEN_DIR})
endif()

target_compile_definitions(${OWT_AGENT_TARGET}
  PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
    OWT_CTRL_ENABLE_GRPC_TRANSPORT=$<BOOL:${OWT_AGENT_ENABLE_GRPC_TRANSPORT}>
)

if (OpenSSL_FOUND)
  target_compile_definitions(${OWT_AGENT_TARGET} PRIVATE OWT_CTRL_ENABLE_WSS_TLS=1)
  target_link_libraries(${OWT_AGENT_TARGET} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
  target_compile_definitions(${OWT_AGENT_TARGET} PRIVATE OWT_CTRL_ENABLE_WSS_TLS=0)
  message(WARNING "OpenSSL not found, WSS TLS client support will be disabled")
endif()

if (LIBSSH2_INCLUDE_DIR AND LIBSSH2_LIBRARY)
  target_compile_definitions(${OWT_AGENT_TARGET} PRIVATE OWT_CTRL_ENABLE_LIBSSH2=1)
  target_include_directories(${OWT_AGENT_TARGET} PRIVATE ${LIBSSH2_INCLUDE_DIR})
  target_link_libraries(${OWT_AGENT_TARGET} PRIVATE ${LIBSSH2_LIBRARY})
else()
  target_compile_definitions(${OWT_AGENT_TARGET} PRIVATE OWT_CTRL_ENABLE_LIBSSH2=0)
endif()

target_link_libraries(${OWT_AGENT_TARGET}
  PRIVATE
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
    ${OWT_AGENT_GRPC_LINK_LIBS}
)
