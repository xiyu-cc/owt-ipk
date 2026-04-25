cmake_minimum_required(VERSION 3.20)
project(owt_agent VERSION 0.2.0 LANGUAGES CXX)

enable_testing()
include(CTest)

include("${CMAKE_CURRENT_SOURCE_DIR}/application/owt-agent/cmake/owt_agent_build.cmake")
