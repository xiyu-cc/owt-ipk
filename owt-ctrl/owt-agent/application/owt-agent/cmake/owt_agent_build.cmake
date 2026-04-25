set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_path(LIBSSH2_INCLUDE_DIR NAMES libssh2.h)
find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2)

option(OWT_AGENT_REQUIRE_LIBSSH2 "Fail configure when libssh2 is unavailable" OFF)
option(OWT_AGENT_ALLOW_SSH_STUB "Allow SSH stub build when libssh2 is unavailable" ON)
set(OWT_AGENT_BUILD_TESTING_REQUESTED "${BUILD_TESTING}")

set(OWT_AGENT_HAS_LIBSSH2 OFF)
if(LIBSSH2_INCLUDE_DIR AND LIBSSH2_LIBRARY)
  set(OWT_AGENT_HAS_LIBSSH2 ON)
endif()
if(OWT_AGENT_REQUIRE_LIBSSH2 AND NOT OWT_AGENT_HAS_LIBSSH2)
  message(FATAL_ERROR "OWT_AGENT_REQUIRE_LIBSSH2=ON but libssh2 headers/libraries were not found")
endif()
if(NOT OWT_AGENT_HAS_LIBSSH2 AND NOT OWT_AGENT_ALLOW_SSH_STUB)
  message(FATAL_ERROR "libssh2 headers/libraries not found and OWT_AGENT_ALLOW_SSH_STUB=OFF")
endif()

set(OWT_AGENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(OWT_AGENT_TP_ROOT "${OWT_AGENT_ROOT}/third_party")
set(OWT_NET_FALLBACK_TP_ROOT "${OWT_AGENT_ROOT}/../owt-net/third_party")
if(NOT EXISTS "${OWT_AGENT_TP_ROOT}/spdlog" AND EXISTS "${OWT_NET_FALLBACK_TP_ROOT}/spdlog")
  set(OWT_AGENT_TP_ROOT "${OWT_NET_FALLBACK_TP_ROOT}")
endif()

set(OWT_PROTOCOL_INCLUDE_DIR "${OWT_AGENT_ROOT}/owt-protocol/include")
if(NOT EXISTS "${OWT_PROTOCOL_INCLUDE_DIR}/owt/protocol/v5/contract.h")
  set(OWT_PROTOCOL_INCLUDE_DIR "${OWT_AGENT_ROOT}/../owt-protocol/include")
endif()

set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)

set(BUILD_CTL OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_ORM OFF CACHE BOOL "" FORCE)
set(BUILD_BROTLI OFF CACHE BOOL "" FORCE)
set(BUILD_YAML_CONFIG OFF CACHE BOOL "" FORCE)
set(USE_SUBMODULE ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_LIBS_ONLY ON CACHE BOOL "" FORCE)
set(USE_SPDLOG OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory("${OWT_AGENT_TP_ROOT}/spdlog" "${CMAKE_BINARY_DIR}/third_party/spdlog" EXCLUDE_FROM_ALL)
add_subdirectory("${OWT_AGENT_TP_ROOT}/nlohmann" "${CMAKE_BINARY_DIR}/third_party/nlohmann" EXCLUDE_FROM_ALL)
add_subdirectory("${OWT_AGENT_TP_ROOT}/jsoncpp" "${CMAKE_BINARY_DIR}/third_party/jsoncpp" EXCLUDE_FROM_ALL)

set(JSONCPP_INCLUDE_DIRS "${OWT_AGENT_TP_ROOT}/jsoncpp/include" CACHE PATH "" FORCE)
set(JSONCPP_LIBRARIES "jsoncpp_static" CACHE STRING "" FORCE)

add_subdirectory("${OWT_AGENT_TP_ROOT}/drogon" "${CMAKE_BINARY_DIR}/third_party/drogon" EXCLUDE_FROM_ALL)
set(BUILD_TESTING "${OWT_AGENT_BUILD_TESTING_REQUESTED}" CACHE BOOL "" FORCE)

set(OWT_AGENT_CORE_SOURCES
  src/core/log.cpp
  src/core/config.cpp
  src/core/host_probe_agent.cpp
  src/core/params_store.cpp
  src/core/wakeonlan_sender.cpp
  src/core/ssh_executor.cpp
  src/core/control/agent_runtime.cpp
  src/core/control/agent_command_executor_registry.cpp
  src/core/control/agent_runtime_execution_worker.cpp
  src/core/control/agent_runtime_heartbeat_builder.cpp
  src/core/control/agent_runtime_message_router.cpp
  src/core/control/runtime_event_dispatcher.cpp
  src/core/control/control_protocol.cpp
  src/core/control/control_json_codec.cpp
  src/core/control/wss_control_channel.cpp
)

add_executable(owt_agent
  src/core/main.cpp
  ${OWT_AGENT_CORE_SOURCES}
)
set_target_properties(owt_agent PROPERTIES OUTPUT_NAME "owt-agent")

function(owt_agent_apply_target_settings target_name)
  target_include_directories(${target_name}
    PRIVATE
      ${OWT_AGENT_ROOT}/include
      ${OWT_PROTOCOL_INCLUDE_DIR}
  )
  if(OWT_AGENT_HAS_LIBSSH2)
    target_include_directories(${target_name} PRIVATE ${LIBSSH2_INCLUDE_DIR})
  else()
    target_compile_definitions(${target_name} PRIVATE OWT_AGENT_SSH_STUB=1)
  endif()

  target_link_libraries(${target_name}
    PRIVATE
      Drogon::Drogon
      spdlog::spdlog_header_only
      nlohmann_json::nlohmann_json
      Threads::Threads
      ssl
      crypto
  )
  if(OWT_AGENT_HAS_LIBSSH2)
    target_link_libraries(${target_name} PRIVATE ${LIBSSH2_LIBRARY})
  endif()
endfunction()

if(NOT OWT_AGENT_HAS_LIBSSH2)
  message(WARNING "libssh2 headers/libraries not found; building with SSH stub")
endif()

owt_agent_apply_target_settings(owt_agent)

if(BUILD_TESTING)
  add_executable(owt_agent_runtime_tests
    tests/agent_runtime_tests.cpp
    ${OWT_AGENT_CORE_SOURCES}
  )
  owt_agent_apply_target_settings(owt_agent_runtime_tests)
  add_test(NAME owt-agent-runtime-tests COMMAND owt_agent_runtime_tests)
endif()
