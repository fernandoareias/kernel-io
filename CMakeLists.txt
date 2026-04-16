cmake_minimum_required(VERSION 3.21)

include(cmake/PreventInSourceBuilds.cmake)

project(kernel-io
  VERSION 0.1.0
  DESCRIPTION "TCP network stack experiments with multiple I/O backends"
  LANGUAGES CXX
)

include(cmake/StandardProjectSettings.cmake)
include(cmake/ProjectOptions.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/StaticAnalysers.cmake)

find_package(spdlog REQUIRED)

if(KIO_ENABLE_TESTS)
  find_package(GTest REQUIRED)
endif()

add_subdirectory(src)

if(KIO_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
