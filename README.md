# PokerForge

Custom poker engine and analyzer (C++20, OpenMP, Dear ImGui). See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the design, milestones and task tracker.

## Build and test (headless core)

```
cmake --preset linux-release          # macos-release / windows-release on those hosts
cmake --build --preset linux-release
ctest --preset linux-release
```

macOS: `brew install libomp` and add `-DOpenMP_ROOT=$(brew --prefix libomp)` when configuring.
