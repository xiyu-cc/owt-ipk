cmake_minimum_required(VERSION 3.20)
project(owt_net_ctrl VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

find_package(Threads REQUIRED)
find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)

set(OWT_PROTOCOL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../owt-protocol/include")

set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)
add_subdirectory(third_party/nlohmann EXCLUDE_FROM_ALL)
add_subdirectory(third_party/sqlite3 EXCLUDE_FROM_ALL)

set(OWT_CTRL_CORE_TARGET owt_ctrl_core)
set(OWT_CTRL_CORE_SOURCES
  src/core/domain/types.cpp
  src/core/ports/defaults.cpp
  src/core/infrastructure/sqlite/common.cpp
  src/core/infrastructure/sqlite/store_core.cpp
  src/core/infrastructure/sqlite/store_schema.cpp
  src/core/infrastructure/sqlite/store_commands.cpp
  src/core/infrastructure/sqlite/store_agents.cpp
  src/core/infrastructure/sqlite/store_params.cpp
  src/core/infrastructure/sqlite/store_audits.cpp
  src/core/application/redaction_service.cpp
  src/core/application/rate_limiter_service.cpp
  src/core/application/params_service.cpp
  src/core/application/agent_registry_service.cpp
  src/core/application/command_orchestrator.cpp
  src/core/application/agent_message_service.cpp
  src/core/application/retry_service.cpp
  src/core/application/audit_query_service.cpp
  src/core/adapters/control_ws_use_cases.cpp
)

add_library(${OWT_CTRL_CORE_TARGET} STATIC ${OWT_CTRL_CORE_SOURCES})
target_include_directories(${OWT_CTRL_CORE_TARGET}
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_link_libraries(${OWT_CTRL_CORE_TARGET}
  PUBLIC
    SQLite3
    nlohmann_json::nlohmann_json
    Threads::Threads
)
set_target_properties(${OWT_CTRL_CORE_TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")

set(OWT_CTRL_TEST_TARGET owt_ctrl_tests)
add_executable(${OWT_CTRL_TEST_TARGET}
  tests/ctrl_tests.cpp
  src/app/ws/agent_protocol.cpp
  src/app/ws/jsonrpc_protocol.cpp
)
target_include_directories(${OWT_CTRL_TEST_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_link_libraries(${OWT_CTRL_TEST_TARGET}
  PRIVATE
    ${OWT_CTRL_CORE_TARGET}
)
set_target_properties(${OWT_CTRL_TEST_TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")

add_test(NAME owt-ctrl-tests COMMAND ${OWT_CTRL_TEST_TARGET})

set(OWT_AGENT_PROTOCOL_TEST_TARGET owt_agent_protocol_tests)
add_executable(${OWT_AGENT_PROTOCOL_TEST_TARGET}
  ../owt-agent/tests/agent_protocol_tests.cpp
  ../owt-agent/src/core/control/control_protocol.cpp
  ../owt-agent/src/core/control/control_json_codec.cpp
)
target_include_directories(${OWT_AGENT_PROTOCOL_TEST_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../owt-agent/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_link_libraries(${OWT_AGENT_PROTOCOL_TEST_TARGET}
  PRIVATE
    nlohmann_json::nlohmann_json
)
set_target_properties(${OWT_AGENT_PROTOCOL_TEST_TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
add_test(NAME owt-agent-protocol-tests COMMAND ${OWT_AGENT_PROTOCOL_TEST_TARGET})

set(OWT_NET_TARGET owt_net)
add_executable(${OWT_NET_TARGET}
  src/app/main.cpp
  src/app/config.cpp
  src/app/runtime.cpp
  src/app/bootstrap/runtime_impl.cpp
  src/app/bootstrap/runtime/parse_utils.cpp
  src/app/bootstrap/runtime/app_metrics.cpp
  src/app/bootstrap/runtime/ws_agent_channel.cpp
  src/app/bootstrap/runtime/ui_subscription_store.cpp
  src/app/bootstrap/runtime/ws_ui_publisher.cpp
  src/app/bootstrap/runtime/state_lifecycle.cpp
  src/app/bootstrap/runtime/state_workers.cpp
  src/app/bootstrap/runtime/agent_ws_handler.cpp
  src/app/bootstrap/runtime/ui_rpc_ws_handler.cpp
  src/app/presenter/serializers.cpp
  src/app/ws/agent_protocol.cpp
  src/app/ws/jsonrpc_protocol.cpp
)
set_target_properties(${OWT_NET_TARGET} PROPERTIES OUTPUT_NAME "owt-net")
target_include_directories(${OWT_NET_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_compile_definitions(${OWT_NET_TARGET}
  PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
)
target_compile_features(${OWT_NET_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${OWT_NET_TARGET}
  PRIVATE
    ${OWT_CTRL_CORE_TARGET}
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
)
set_target_properties(${OWT_NET_TARGET} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")

set(OWT_AGENT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../owt-agent/include")
set(OWT_AGENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../owt-agent/src/core")
set(OWT_AGENT_CLANG_TIDY_FILES
  "${OWT_AGENT_SOURCE_DIR}/control/agent_runtime.cpp"
  "${OWT_AGENT_SOURCE_DIR}/control/control_protocol.cpp"
  "${OWT_AGENT_SOURCE_DIR}/control/control_json_codec.cpp"
)

set(OWT_AGENT_CLANG_TIDY_COMMON_ARGS
  "-std=c++17"
  "-DBOOST_ERROR_CODE_HEADER_ONLY"
  "-I${OWT_AGENT_INCLUDE_DIR}"
  "-I${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost"
  "-I${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include"
  "-I${CMAKE_CURRENT_SOURCE_DIR}/third_party"
  "-I${OWT_PROTOCOL_INCLUDE_DIR}"
)

set(OWT_AGENT_CLANG_TIDY_COMMANDS
  COMMAND ${CMAKE_COMMAND} -E echo "Running clang-tidy for owt-agent control sources"
)
foreach(AGENT_FILE IN LISTS OWT_AGENT_CLANG_TIDY_FILES)
  list(APPEND OWT_AGENT_CLANG_TIDY_COMMANDS
    COMMAND ${CLANG_TIDY_EXE} ${AGENT_FILE} -- ${OWT_AGENT_CLANG_TIDY_COMMON_ARGS}
  )
endforeach()

add_custom_target(owt_agent_clang_tidy ALL
  ${OWT_AGENT_CLANG_TIDY_COMMANDS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)
add_dependencies(${OWT_NET_TARGET} owt_agent_clang_tidy)
