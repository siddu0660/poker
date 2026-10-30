include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json v3.11.3 (pinned release tarball)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# Dear ImGui (v1.91.9-docking), ImPlot and SDL2 are added in milestone M6.
