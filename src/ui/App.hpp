#pragma once
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace pf::ui {

struct AppOptions {
    std::string rulesDir;         // empty: next to the executable
    int width = 1600;
    int height = 900;
    // Scripted runs for smoke tests and screenshots (0 = run until closed).
    int maxFrames = 0;
    int advanceStreets = 0;       // press Next Street this many times, 30 frames apart
    int players = 0;              // set the seat count (0 = leave)
    int tab = -1;                 // Equity panel tab to show: 0 Equity, 1 Hands, 2 Odds, 3 Simulation
    std::vector<int> foldSeats;   // seats to fold (1-based)
    bool fixedSeed = false;
    unsigned long long seed = 1;
    std::string screenshotPath;   // written on the last frame as a binary PPM
};

// Owns the SDL window, the GL context and the Dear ImGui context, and runs the main loop.
int runApp(const AppOptions& options);

}  // namespace pf::ui
