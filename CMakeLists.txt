cmake_minimum_required(VERSION 3.24)
project(PokerForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PF_BUILD_UI "Build the Dear ImGui front end" ON)
option(PF_BUILD_TESTS "Build unit tests" ON)
option(PF_NATIVE_ARCH "Compile with -march=native" OFF)
option(PF_ENABLE_LTO "Enable LTO in Release" ON)
option(PF_WARNINGS_AS_ERRORS "Treat warnings as errors (CI)" OFF)
option(PF_USE_OPENMP "Parallelise simulation batches with OpenMP" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/Dependencies.cmake)

if(PF_USE_OPENMP)
  find_package(OpenMP COMPONENTS CXX)
endif()
if(PF_USE_OPENMP AND NOT OpenMP_CXX_FOUND)
  message(WARNING "OpenMP not found: simulation will run single-threaded")
endif()

add_library(pf_options INTERFACE)
if(MSVC)
  # C4324 "structure was padded due to alignment specifier" is the point of
  # alignas(64) on per-thread tallies (no false sharing), so it is not a warning here.
  target_compile_options(pf_options INTERFACE /W4 /utf-8 /wd4324)
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
  src/core/HandAnalysis.cpp
  src/util/Log.cpp
  src/engine/Simulator.cpp
  src/engine/TableState.cpp
  src/engine/Benchmark.cpp
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

# The state behind the UI panels: no ImGui or SDL, so it is built (and tested) everywhere.
add_library(pokerapp STATIC
  src/ui/AppState.cpp
  src/ui/Settings.cpp
  src/ui/Export.cpp)
target_include_directories(pokerapp PUBLIC src)
target_link_libraries(pokerapp PUBLIC pokercore PRIVATE pf_options)

# Rules JSON is found relative to the executable, not the working directory.
file(GLOB PF_RULE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/rules/*.json)

if(PF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PF_BUILD_UI)
  add_executable(pokerforge
    src/main.cpp
    src/ui/App.cpp
    src/ui/DrawUtil.cpp
    src/ui/Dialogs.cpp
    src/ui/SettingsPanel.cpp
    src/ui/RulesPanel.cpp
    src/ui/TablePanel.cpp
    src/ui/EquityPanel.cpp
    src/ui/ConsolePanel.cpp)
  target_link_libraries(pokerforge PRIVATE pokerapp implot_lib pf_options)
  if(WIN32)
    # A desktop program: no console window on start. MSVC still enters through main().
    set_target_properties(pokerforge PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
      target_link_options(pokerforge PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()
  target_compile_definitions(pokerforge PRIVATE PF_SOURCE_RULES_DIR="${CMAKE_SOURCE_DIR}/rules"
                                              PF_VERSION="${PROJECT_VERSION}")
  # Rules are found next to the executable, so ship them beside it.
  add_custom_command(TARGET pokerforge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/rules $<TARGET_FILE_DIR:pokerforge>/rules
    COMMENT "Copying rules next to pokerforge")

  # Packaging: `cmake --build <dir> --target package` makes a zip (Windows) or tar.gz holding the
  # executable, the rules folder and the README, unpacked into one folder that can be moved anywhere.
  install(TARGETS pokerforge RUNTIME DESTINATION .)
  install(DIRECTORY rules DESTINATION . FILES_MATCHING PATTERN "*.json")
  install(FILES README.md DESTINATION .)
  set(CPACK_PACKAGE_NAME "PokerForge")
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Real-time poker odds with custom rules")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
  set(CPACK_PACKAGE_FILE_NAME "PokerForge-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  if(WIN32)
    set(CPACK_GENERATOR ZIP)
  else()
    set(CPACK_GENERATOR TGZ)
  endif()
  include(CPack)
endif()
