#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ui/App.hpp"

namespace {

void usage() {
    std::puts(
        "pokerforge [options]\n"
        "  --rules-dir DIR     folder holding the rules JSON files\n"
        "  --size WxH          window size (default 1600x900)\n"
        "Scripted runs (smoke tests, screenshots):\n"
        "  --frames N          run N frames, then exit\n"
        "  --advance K         press Next Street K times\n"
        "  --players N         seat count\n"
        "  --fold A,B          fold these seats (1-based)\n"
        "  --tab N             Equity tab: 0 Equity, 1 Hands, 2 Odds, 3 Simulation\n"
        "  --seed N            fixed simulation seed\n"
        "  --screenshot FILE   write the last frame as a binary PPM");
}

}  // namespace

int main(int argc, char** argv) {
    pf::ui::AppOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--rules-dir" && hasValue) opt.rulesDir = argv[++i];
        else if (a == "--frames" && hasValue) opt.maxFrames = std::atoi(argv[++i]);
        else if (a == "--advance" && hasValue) opt.advanceStreets = std::atoi(argv[++i]);
        else if (a == "--players" && hasValue) opt.players = std::atoi(argv[++i]);
        else if (a == "--fold" && hasValue) {
            for (char* tok = std::strtok(argv[++i], ","); tok; tok = std::strtok(nullptr, ",")) opt.foldSeats.push_back(std::atoi(tok));
        }
        else if (a == "--tab" && hasValue) opt.tab = std::atoi(argv[++i]);
        else if (a == "--seed" && hasValue) { opt.fixedSeed = true; opt.seed = std::strtoull(argv[++i], nullptr, 10); }
        else if (a == "--screenshot" && hasValue) opt.screenshotPath = argv[++i];
        else if (a == "--size" && hasValue) {
            if (std::sscanf(argv[++i], "%dx%d", &opt.width, &opt.height) != 2) { usage(); return 2; }
        } else { usage(); return 2; }
    }
    return pf::ui::runApp(opt);
}
