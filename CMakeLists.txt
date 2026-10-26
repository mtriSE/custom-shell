cmake_minimum_required(VERSION 3.13)

project(shell)

set(CMAKE_CXX_STANDARD 23) # Enable the C++23 standard

file(GLOB_RECURSE SOURCE_FILES src/*.cpp src/*.hpp)
# main() stays out of the library so the unit tests can link everything else.
list(FILTER SOURCE_FILES EXCLUDE REGEX "src/main\\.cpp$")

add_library(shell_core STATIC ${SOURCE_FILES})
target_include_directories(shell_core PUBLIC src)
target_link_libraries(shell_core PUBLIC readline)

add_executable(shell src/main.cpp)
target_link_libraries(shell PRIVATE shell_core)

# Unit tests (Catch2 v3, tests only). Nothing is downloaded unless asked to, so configuring and
# building the shell never needs network access or Catch2.
option(SHELL_UNIT_TESTS "Configure the Catch2 unit tests" ON)
option(SHELL_FETCH_CATCH2 "Download Catch2 when it is not installed" OFF)

if(SHELL_UNIT_TESTS)
  find_package(Catch2 3 QUIET)
  if(NOT Catch2_FOUND AND SHELL_FETCH_CATCH2)
    include(FetchContent)
    FetchContent_Declare(Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.7.1)
    FetchContent_MakeAvailable(Catch2)
    set(Catch2_FOUND TRUE)
  endif()

  if(Catch2_FOUND)
    file(GLOB UNIT_TEST_SOURCES tests/unit/*.cpp)
    # Built on request (`--target unit_tests`, done by tests/run.sh), not by a plain build.
    add_executable(unit_tests EXCLUDE_FROM_ALL ${UNIT_TEST_SOURCES})
    target_link_libraries(unit_tests PRIVATE shell_core Catch2::Catch2WithMain)
  else()
    message(STATUS "Catch2 not found: unit tests not configured "
                   "(install the catch2 package or pass -DSHELL_FETCH_CATCH2=ON)")
  endif()
endif()
