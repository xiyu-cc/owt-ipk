cmake_minimum_required(VERSION 3.20)
project(owt_ctrl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_path(LIBSSH2_INCLUDE_DIR libssh2.h)
find_library(LIBSSH2_LIBRARY NAMES ssh2 libssh2)

set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)
add_subdirectory(third_party/nlohmann EXCLUDE_FROM_ALL)

set(OWT_CTRL_TARGET owt_ctrl)

add_executable(${OWT_CTRL_TARGET}
  src/core/main.cpp
  src/core/log.cpp
  src/core/config.cpp
  src/core/utils.cpp
  src/core/host_probe_agent.cpp
  src/core/params_store.cpp
  src/core/wakeonlan_sender.cpp
  src/core/ssh_executor.cpp
  src/api/wol_wake_api.cpp
  src/api/host_control_api.cpp
  src/api/host_probe_api.cpp
  src/api/monitoring_api.cpp
  src/api/params_api.cpp
)

set_target_properties(${OWT_CTRL_TARGET} PROPERTIES OUTPUT_NAME "owt-ctrl")

target_include_directories(${OWT_CTRL_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost
)

target_compile_definitions(${OWT_CTRL_TARGET}
  PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
)

if (LIBSSH2_INCLUDE_DIR AND LIBSSH2_LIBRARY)
  target_compile_definitions(${OWT_CTRL_TARGET} PRIVATE OWT_CTRL_ENABLE_LIBSSH2=1)
  target_include_directories(${OWT_CTRL_TARGET} PRIVATE ${LIBSSH2_INCLUDE_DIR})
  target_link_libraries(${OWT_CTRL_TARGET} PRIVATE ${LIBSSH2_LIBRARY})
else()
  target_compile_definitions(${OWT_CTRL_TARGET} PRIVATE OWT_CTRL_ENABLE_LIBSSH2=0)
endif()

target_link_libraries(${OWT_CTRL_TARGET}
  PRIVATE
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
)
