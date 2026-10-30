include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json v3.11.3 (pinned release tarball)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# UI dependencies. GitHub tarball downloads are not always reachable, so these
# are fetched with git at pinned tags.
if(PF_BUILD_UI)
  # SDL2: use an installed copy first so a normal Linux/macOS build stays fast;
  # otherwise build the pinned release (clean Windows needs only CMake + a compiler).
  find_package(SDL2 CONFIG QUIET)
  if(NOT TARGET SDL2::SDL2)
    message(STATUS "SDL2 not found: fetching release-2.30.9")
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.30.9
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL2)
  endif()
  if(NOT TARGET SDL2::SDL2 AND TARGET SDL2-static)
    add_library(SDL2::SDL2 ALIAS SDL2-static)
  endif()

  find_package(OpenGL REQUIRED)

  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9-docking
    GIT_SHALLOW TRUE)
  FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v1.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui implot)

  # Neither ships a CMake build, so compile them into small static libraries.
  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui_lib SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_compile_definitions(imgui_lib PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS GL_SILENCE_DEPRECATION)
  target_link_libraries(imgui_lib PUBLIC SDL2::SDL2 OpenGL::GL ${CMAKE_DL_LIBS})

  add_library(implot_lib STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)
  target_include_directories(implot_lib SYSTEM PUBLIC ${implot_SOURCE_DIR})
  target_link_libraries(implot_lib PUBLIC imgui_lib)
endif()
