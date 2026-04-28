cmake_minimum_required(VERSION 3.20)
project(owt_net_ctrl VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

find_package(Threads REQUIRED)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)

set(OWT_PROTOCOL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../owt-protocol/include")

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

add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)
add_subdirectory(third_party/nlohmann EXCLUDE_FROM_ALL)
add_subdirectory(third_party/sqlite3 EXCLUDE_FROM_ALL)
add_subdirectory(third_party/jsoncpp EXCLUDE_FROM_ALL)
set(JSONCPP_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsoncpp/include" CACHE PATH "" FORCE)
set(JSONCPP_LIBRARIES "jsoncpp_static" CACHE STRING "" FORCE)
add_subdirectory(third_party/drogon EXCLUDE_FROM_ALL)

function(owt_apply_clang_tidy target)
  if(CLANG_TIDY_EXE)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  endif()
endfunction()

set(OWT_CTRL_COMMON_SOURCES
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

set(OWT_CTRL_TEST_TARGET owt_ctrl_tests)
add_executable(${OWT_CTRL_TEST_TARGET}
  tests/ctrl_tests.cpp
  ${OWT_CTRL_COMMON_SOURCES}
  src/app/bootstrap/runtime/session_runtime.cpp
  src/app/bootstrap/runtime/ws_envelope_validator.cpp
  src/app/bootstrap/runtime/agent_action_gateway.cpp
  src/app/presenter/serializers.cpp
  src/app/ws/command_bus_protocol.cpp
  src/app/ws/scheduler/event_scheduler.cpp
)
target_include_directories(${OWT_CTRL_TEST_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/drogon/lib/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/drogon/trantor
    ${CMAKE_BINARY_DIR}/third_party/drogon/lib/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_link_libraries(${OWT_CTRL_TEST_TARGET}
  PRIVATE
    SQLite3
    Drogon::Drogon
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
)
owt_apply_clang_tidy(${OWT_CTRL_TEST_TARGET})
add_test(NAME owt-ctrl-tests COMMAND ${OWT_CTRL_TEST_TARGET})

set(OWT_WS_STRESS_TARGET owt_ws_scheduler_stress)
add_executable(${OWT_WS_STRESS_TARGET}
  tests/ws_scheduler_stress.cpp
  src/app/ws/scheduler/event_scheduler.cpp
)
target_include_directories(${OWT_WS_STRESS_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_link_libraries(${OWT_WS_STRESS_TARGET}
  PRIVATE
    spdlog::spdlog_header_only
    Threads::Threads
)
owt_apply_clang_tidy(${OWT_WS_STRESS_TARGET})

set(OWT_AGENT_PROTOCOL_TEST_TARGET owt_agent_protocol_tests)
add_executable(${OWT_AGENT_PROTOCOL_TEST_TARGET}
  ../owt-agent/tests/agent_protocol_tests.cpp
  ../owt-agent/src/core/control/control_protocol.cpp
  ../owt-agent/src/core/control/control_json_codec.cpp
  ../owt-agent/src/core/control/json_codec/json_codec_field_rules.cpp
  ../owt-agent/src/core/control/json_codec/json_codec_payload.cpp
  ../owt-agent/src/core/control/json_codec/json_codec_envelope.cpp
)
target_include_directories(${OWT_AGENT_PROTOCOL_TEST_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../owt-agent/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../owt-agent/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../owt-agent/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_link_libraries(${OWT_AGENT_PROTOCOL_TEST_TARGET}
  PRIVATE
    nlohmann_json::nlohmann_json
)
owt_apply_clang_tidy(${OWT_AGENT_PROTOCOL_TEST_TARGET})
add_test(NAME owt-agent-protocol-tests COMMAND ${OWT_AGENT_PROTOCOL_TEST_TARGET})

set(OWT_NET_TARGET owt_net)
add_executable(${OWT_NET_TARGET}
  ${OWT_CTRL_COMMON_SOURCES}
  src/app/main.cpp
  src/app/config.cpp
  src/app/runtime.cpp
  src/app/bootstrap/runtime_impl.cpp
  src/app/bootstrap/runtime/runtime_composition.cpp
  src/app/bootstrap/runtime/ws_envelope_validator.cpp
  src/app/bootstrap/runtime/ws_endpoints.cpp
  src/app/bootstrap/runtime/session_runtime.cpp
  src/app/bootstrap/runtime/ui_action_gateway.cpp
  src/app/bootstrap/runtime/agent_action_gateway.cpp
  src/app/bootstrap/runtime/lifecycle.cpp
  src/app/presenter/serializers.cpp
  src/app/ws/command_bus_protocol.cpp
  src/app/ws/scheduler/event_scheduler.cpp
)
set_target_properties(${OWT_NET_TARGET} PROPERTIES OUTPUT_NAME "owt-net")
target_include_directories(${OWT_NET_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
    ${OWT_PROTOCOL_INCLUDE_DIR}
)
target_compile_features(${OWT_NET_TARGET} PRIVATE cxx_std_20)
target_link_libraries(${OWT_NET_TARGET}
  PRIVATE
    SQLite3
    Drogon::Drogon
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
)
owt_apply_clang_tidy(${OWT_NET_TARGET})

install(TARGETS ${OWT_NET_TARGET}
  RUNTIME DESTINATION sbin
)

install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/systemd/owt-net.service"
  DESTINATION lib/systemd/system
)

install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
  DESTINATION etc/owt-net
)

install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/nginx/owt-net.conf"
  DESTINATION etc/nginx/sites-available
)

install(DIRECTORY
  "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist/"
  DESTINATION var/www/owt
)

execute_process(
  COMMAND dpkg --print-architecture
  OUTPUT_VARIABLE OWT_DEB_ARCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE OWT_DPKG_RESULT
)

if(NOT OWT_DPKG_RESULT EQUAL 0 OR OWT_DEB_ARCH STREQUAL "")
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(OWT_DEB_ARCH "amd64")
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(OWT_DEB_ARCH "arm64")
  else()
    set(OWT_DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_INSTALL_DO_STRIP ON)
endif()
set(CPACK_STRIP_FILES TRUE)
set(CPACK_PACKAGE_NAME "owt-net")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "owt-ipk")
set(CPACK_PACKAGE_CONTACT "owt-ipk")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "owt-net control plane service")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "owt-ipk")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${OWT_DEB_ARCH}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd, nginx, libc6 (>= 2.34), libgcc-s1 (>= 3.0), libstdc++6 (>= 11)")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/postinst"
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/prerm"
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/postrm"
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/conffiles"
)

include(CPack)
