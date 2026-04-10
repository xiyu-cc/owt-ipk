cmake_minimum_required(VERSION 3.20)
project(owt_net VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

set(OWT_OAUTH2_PROXY_BINARY "" CACHE FILEPATH "Path to oauth2-proxy binary to bundle into the deb package")

enable_testing()

find_package(Threads REQUIRED)

set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)
add_subdirectory(third_party/nlohmann EXCLUDE_FROM_ALL)

set(OWT_NET_TARGET owt_net)

set(OWT_CTRL_SOURCES
  src/core/main.cpp
  src/core/log.cpp
  src/core/config.cpp
  src/core/utils.cpp
  src/core/rate_limiter.cpp
  src/core/observability.cpp
  src/core/sensitive_json.cpp
  src/core/command_retry_scheduler.cpp
  src/core/websocket_session_observer_factory.cpp
  src/core/control_ws_session_observer.cpp
  src/core/frontend_status_ws_session_observer.cpp
  src/core/frontend_status_stream.cpp
  src/core/control_hub.cpp
  src/core/command_store.cpp
  src/core/control/control_protocol.cpp
  src/core/control/control_json_codec.cpp
  third_party/sqlite3/sqlite3.c
  src/api/control_agent_get_api.cpp
  src/api/control_agents_get_api.cpp
  src/api/control_audit_get_api.cpp
  src/api/control_command_get_api.cpp
  src/api/control_command_push_api.cpp
  src/api/control_commands_get_api.cpp
)

add_executable(${OWT_NET_TARGET} ${OWT_CTRL_SOURCES})

set_target_properties(${OWT_NET_TARGET} PROPERTIES OUTPUT_NAME "owt-net")

target_include_directories(${OWT_NET_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
)

target_compile_definitions(${OWT_NET_TARGET}
  PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
)

target_link_libraries(${OWT_NET_TARGET}
  PRIVATE
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
)

set(OWT_NET_TEST_TARGET owt_net_tests)
set(OWT_NET_TEST_SOURCES
  tests/owt_net_tests.cpp
  src/core/log.cpp
  src/core/sensitive_json.cpp
  src/core/command_store.cpp
  src/core/control/control_protocol.cpp
  third_party/sqlite3/sqlite3.c
)

add_executable(${OWT_NET_TEST_TARGET} ${OWT_NET_TEST_SOURCES})
set_target_properties(${OWT_NET_TEST_TARGET} PROPERTIES OUTPUT_NAME "owt-net-tests")

target_include_directories(${OWT_NET_TEST_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
)

target_compile_definitions(${OWT_NET_TEST_TARGET}
  PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
)

target_link_libraries(${OWT_NET_TEST_TARGET}
  PRIVATE
    spdlog::spdlog_header_only
    nlohmann_json::nlohmann_json
    Threads::Threads
)

add_test(NAME owt-net-tests COMMAND ${OWT_NET_TEST_TARGET})

install(TARGETS ${OWT_NET_TARGET} RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/config.ini DESTINATION /etc/owt-net)
install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/deploy/nginx/oauth2-proxy-google.example.cfg
  DESTINATION /etc/owt-net
  RENAME oauth2-proxy.cfg
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/deploy/nginx/owt-net.conf DESTINATION /etc/nginx/sites-available)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/deploy/systemd/owt-net.service DESTINATION /lib/systemd/system)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/deploy/systemd/oauth2-proxy.service DESTINATION /lib/systemd/system)
if(OWT_OAUTH2_PROXY_BINARY)
  if(EXISTS "${OWT_OAUTH2_PROXY_BINARY}")
    install(PROGRAMS "${OWT_OAUTH2_PROXY_BINARY}" DESTINATION /usr/local/bin RENAME oauth2-proxy)
  else()
    message(FATAL_ERROR "OWT_OAUTH2_PROXY_BINARY does not exist: ${OWT_OAUTH2_PROXY_BINARY}")
  endif()
else()
  message(WARNING "OWT_OAUTH2_PROXY_BINARY is empty. deb package will not include oauth2-proxy binary.")
endif()
install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/../docs/build-and-release.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/owt-net
  RENAME README.md
)

set(OWT_NET_FRONTEND_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist")
if(EXISTS "${OWT_NET_FRONTEND_DIST_DIR}/index.html")
  install(DIRECTORY "${OWT_NET_FRONTEND_DIST_DIR}/" DESTINATION /var/www/owt)
else()
  message(WARNING "frontend dist is missing: ${OWT_NET_FRONTEND_DIST_DIR}. Run: cd frontend && npm install && npm run build")
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "owt-net")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "owt-ipk")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "owt-net control plane service")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "owt-ipk")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd, nginx")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/prerm;${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/postrm;${CMAKE_CURRENT_SOURCE_DIR}/deploy/deb/conffiles")

include(CPack)
