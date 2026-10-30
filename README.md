# PokerForge

Custom poker engine and analyzer (C++20, OpenMP, Dear ImGui). See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the design, milestones and task tracker.

## Build, test and run

```
cmake --preset linux-release          # macos-release / windows-release on those hosts
cmake --build --preset linux-release
ctest --preset linux-release
```

Run the app with `build/linux-release/pokerforge` (rules are copied next to it). The UI needs SDL2 and OpenGL: on Debian/Ubuntu `sudo apt install libsdl2-dev libgl-dev`; elsewhere SDL2 is fetched automatically. Configure with `-DPF_BUILD_UI=OFF` for the headless core only.

macOS: `brew install libomp` and add `-DOpenMP_ROOT=$(brew --prefix libomp)` when configuring.
