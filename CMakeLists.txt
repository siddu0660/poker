cmake_minimum_required(VERSION 3.24)
project(PokerForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PF_BUILD_UI "Build the Dear ImGui front end" OFF)
option(PF_BUILD_TESTS "Build unit tests" ON)
option(PF_NATIVE_ARCH "Compile with -march=native" OFF)
option(PF_ENABLE_LTO "Enable LTO in Release" ON)
option(PF_WARNINGS_AS_ERRORS "Treat warnings as errors (CI)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/Dependencies.cmake)

find_package(OpenMP COMPONENTS CXX)
if(NOT OpenMP_CXX_FOUND)
  message(WARNING "OpenMP not found: simulation will run single-threaded")
endif()

add_library(pf_options INTERFACE)
if(MSVC)
  target_compile_options(pf_options INTERFACE /W4 /utf-8)
  if(PF_WARNINGS_AS_ERRORS)
    target_compile_options(pf_options INTERFACE /WX)
  endif()
else()
  target_compile_options(pf_options INTERFACE -Wall -Wextra -Wconversion)
  if(PF_WARNINGS_AS_ERRORS)
    target_compile_options(pf_options INTERFACE -Werror)
  endif()
  if(PF_NATIVE_ARCH)
    target_compile_options(pf_options INTERFACE -march=native)
  endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT PF_IPO_OK OUTPUT PF_IPO_MSG)

add_library(pokercore STATIC
  src/core/Rules.cpp
  src/core/Evaluator.cpp
  src/util/Log.cpp
)
target_include_directories(pokercore PUBLIC src)
target_link_libraries(pokercore PUBLIC nlohmann_json::nlohmann_json PRIVATE pf_options)
if(OpenMP_CXX_FOUND)
  target_link_libraries(pokercore PUBLIC OpenMP::OpenMP_CXX)
endif()
# Non-Apple Clang emits bitcode objects that the stock ar/ld cannot read, so
# LTO is limited to GCC, MSVC and Apple Clang.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(PF_LTO_COMPILER_OK OFF)
else()
  set(PF_LTO_COMPILER_OK ON)
endif()
if(PF_ENABLE_LTO AND PF_IPO_OK AND PF_LTO_COMPILER_OK)
  set_property(TARGET pokercore PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Rules JSON is found relative to the executable, not the working directory.
file(GLOB PF_RULE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/rules/*.json)

if(PF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PF_BUILD_UI)
  message(STATUS "PF_BUILD_UI requested: UI target is added in milestone M6")
endif()
