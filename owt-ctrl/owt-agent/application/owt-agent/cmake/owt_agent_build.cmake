set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_path(LIBSSH2_INCLUDE_DIR NAMES libssh2.h)
find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2)
find_path(LIBWEBSOCKETS_INCLUDE_DIR NAMES libwebsockets.h)
find_library(LIBWEBSOCKETS_LIBRARY NAMES websockets libwebsockets)

option(OWT_AGENT_REQUIRE_LIBSSH2 "Fail configure when libssh2 is unavailable" OFF)
option(OWT_AGENT_ALLOW_SSH_STUB "Allow SSH stub build when libssh2 is unavailable" ON)

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
if(NOT LIBWEBSOCKETS_INCLUDE_DIR OR NOT LIBWEBSOCKETS_LIBRARY)
  message(FATAL_ERROR "libwebsockets headers/libraries were not found")
endif()

set(OWT_AGENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT DEFINED OWT_AGENT_TP_ROOT OR OWT_AGENT_TP_ROOT STREQUAL "")
  message(FATAL_ERROR "OWT_AGENT_TP_ROOT is required and must point to third_party root")
endif()
if(NOT IS_ABSOLUTE "${OWT_AGENT_TP_ROOT}")
  get_filename_component(OWT_AGENT_TP_ROOT "${OWT_AGENT_TP_ROOT}" ABSOLUTE BASE_DIR "${OWT_AGENT_ROOT}")
endif()
if(NOT EXISTS "${OWT_AGENT_TP_ROOT}/spdlog" OR
   NOT EXISTS "${OWT_AGENT_TP_ROOT}/nlohmann" OR
   NOT EXISTS "${OWT_AGENT_TP_ROOT}/jsoncpp")
  message(FATAL_ERROR "OWT_AGENT_TP_ROOT='${OWT_AGENT_TP_ROOT}' is missing required third_party components")
endif()

set(OWT_PROTOCOL_INCLUDE_DIR "${OWT_AGENT_ROOT}/owt-protocol/include")
if(NOT EXISTS "${OWT_PROTOCOL_INCLUDE_DIR}/owt/protocol/v5/contract.h")
  set(OWT_PROTOCOL_INCLUDE_DIR "${OWT_AGENT_ROOT}/../owt-protocol/include")
endif()

set(_OWT_AGENT_BUILD_TESTING_REQUESTED "${BUILD_TESTING}")

set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_EXAMPLE_HO OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_BUILD_SHARED OFF)

set(JSONCPP_WITH_TESTS OFF)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF)

set(BUILD_TESTING OFF)

add_subdirectory("${OWT_AGENT_TP_ROOT}/spdlog" "${CMAKE_BINARY_DIR}/third_party/spdlog" EXCLUDE_FROM_ALL)
add_subdirectory("${OWT_AGENT_TP_ROOT}/nlohmann" "${CMAKE_BINARY_DIR}/third_party/nlohmann" EXCLUDE_FROM_ALL)
add_subdirectory("${OWT_AGENT_TP_ROOT}/jsoncpp" "${CMAKE_BINARY_DIR}/third_party/jsoncpp" EXCLUDE_FROM_ALL)
set(JSONCPP_INCLUDE_DIRS "${OWT_AGENT_TP_ROOT}/jsoncpp/include")
set(JSONCPP_LIBRARIES "jsoncpp_static")

set(BUILD_TESTING "${_OWT_AGENT_BUILD_TESTING_REQUESTED}")

option(
  OWT_AGENT_ENABLE_NO_FILE_WRITE_CHECKS
  "Enable no-file-write validation tests for owt-agent process"
  ON)
option(
  OWT_AGENT_ENABLE_NO_FILE_WRITE_RUNTIME_CHECK
  "Enable strace-based runtime no-file-write validation test"
  ON)

set(OWT_AGENT_CORE_SOURCES
  src/core/log.cpp
  src/core/config.cpp
  src/core/host_probe_agent.cpp
  src/core/params_store.cpp
  src/core/wakeonlan_sender.cpp
  src/core/ssh_stream_reader.cpp
  src/core/ssh_executor.cpp
  src/core/control/agent_runtime.cpp
  src/core/control/agent_command_executor_registry.cpp
  src/core/control/agent_runtime_execution_worker.cpp
  src/core/control/agent_runtime_heartbeat_builder.cpp
  src/core/control/agent_runtime_message_router.cpp
  src/core/control/default_command_executors.cpp
  src/core/control/default_control_channel_factory.cpp
  src/core/control/runtime_event_dispatcher.cpp
  src/core/control/control_protocol.cpp
  src/core/control/control_json_codec.cpp
  src/core/control/wss_control_channel.cpp
  src/core/control/ports/defaults.cpp
  src/core/control/ports/json_mappers.cpp
  src/core/control/command_handlers/wol_handler.cpp
  src/core/control/command_handlers/host_power_handlers.cpp
  src/core/control/command_handlers/host_probe_handler.cpp
  src/core/control/command_handlers/params_handlers.cpp
  src/core/control/json_codec/json_codec_field_rules.cpp
  src/core/control/json_codec/json_codec_payload.cpp
  src/core/control/json_codec/json_codec_envelope.cpp
)

function(owt_agent_apply_target_settings target_name)
  target_include_directories(${target_name}
    PRIVATE
      ${OWT_AGENT_ROOT}/include
      ${OWT_AGENT_ROOT}/src
      ${OWT_AGENT_ROOT}/src/core
      ${OWT_PROTOCOL_INCLUDE_DIR}
      ${LIBWEBSOCKETS_INCLUDE_DIR}
  )
  if(OWT_AGENT_HAS_LIBSSH2)
    target_include_directories(${target_name} PRIVATE ${LIBSSH2_INCLUDE_DIR})
  else()
    target_compile_definitions(${target_name} PRIVATE OWT_AGENT_SSH_STUB=1)
  endif()

  target_link_libraries(${target_name}
    PRIVATE
      ${LIBWEBSOCKETS_LIBRARY}
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

add_executable(owt_agent
  src/core/main.cpp
  ${OWT_AGENT_CORE_SOURCES}
)
set_target_properties(owt_agent PROPERTIES OUTPUT_NAME "owt-agent")
owt_agent_apply_target_settings(owt_agent)

if(BUILD_TESTING)
  add_executable(owt_agent_runtime_tests
    tests/agent_runtime_tests.cpp
    ${OWT_AGENT_CORE_SOURCES}
  )
  owt_agent_apply_target_settings(owt_agent_runtime_tests)
  add_test(NAME owt-agent-runtime-tests COMMAND owt_agent_runtime_tests)

  add_executable(owt_agent_protocol_tests
    tests/agent_protocol_tests.cpp
    src/core/control/control_protocol.cpp
    src/core/control/control_json_codec.cpp
    src/core/control/json_codec/json_codec_field_rules.cpp
    src/core/control/json_codec/json_codec_payload.cpp
    src/core/control/json_codec/json_codec_envelope.cpp
  )
  owt_agent_apply_target_settings(owt_agent_protocol_tests)
  add_test(NAME owt-agent-protocol-tests COMMAND owt_agent_protocol_tests)

  add_executable(owt_agent_ssh_stream_reader_tests
    tests/ssh_stream_reader_tests.cpp
    src/core/ssh_stream_reader.cpp
  )
  owt_agent_apply_target_settings(owt_agent_ssh_stream_reader_tests)
  add_test(NAME owt-agent-ssh-stream-reader-tests COMMAND owt_agent_ssh_stream_reader_tests)

  add_executable(owt_agent_log_safety_tests
    tests/log_safety_tests.cpp
    src/core/log.cpp
  )
  owt_agent_apply_target_settings(owt_agent_log_safety_tests)
  add_test(NAME owt-agent-log-safety-tests COMMAND owt_agent_log_safety_tests)

  if(OWT_AGENT_ENABLE_NO_FILE_WRITE_CHECKS)
    set(OWT_AGENT_NO_FILE_WRITE_STATIC_SCRIPT "${OWT_AGENT_ROOT}/tests/scripts/no_file_write_static_check.sh")
    if(NOT EXISTS "${OWT_AGENT_NO_FILE_WRITE_STATIC_SCRIPT}")
      message(FATAL_ERROR "Missing no-file-write static check script: ${OWT_AGENT_NO_FILE_WRITE_STATIC_SCRIPT}")
    endif()
    add_test(
      NAME owt-agent-no-file-write-static
      COMMAND /bin/sh "${OWT_AGENT_NO_FILE_WRITE_STATIC_SCRIPT}" "${OWT_AGENT_ROOT}")

    if(OWT_AGENT_ENABLE_NO_FILE_WRITE_RUNTIME_CHECK)
      find_program(OWT_AGENT_STRACE_EXECUTABLE NAMES strace)
      if(NOT OWT_AGENT_STRACE_EXECUTABLE)
        message(FATAL_ERROR "OWT_AGENT_ENABLE_NO_FILE_WRITE_RUNTIME_CHECK=ON but strace was not found")
      endif()

      set(OWT_AGENT_NO_FILE_WRITE_RUNTIME_SCRIPT "${OWT_AGENT_ROOT}/tests/scripts/no_file_write_runtime_check.sh")
      if(NOT EXISTS "${OWT_AGENT_NO_FILE_WRITE_RUNTIME_SCRIPT}")
        message(FATAL_ERROR "Missing no-file-write runtime check script: ${OWT_AGENT_NO_FILE_WRITE_RUNTIME_SCRIPT}")
      endif()
      add_test(
        NAME owt-agent-no-file-write-runtime
        COMMAND /bin/sh "${OWT_AGENT_NO_FILE_WRITE_RUNTIME_SCRIPT}" "${OWT_AGENT_STRACE_EXECUTABLE}" "$<TARGET_FILE:owt_agent_log_safety_tests>")
    endif()
  endif()
endif()
