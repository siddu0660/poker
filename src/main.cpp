#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "ui/App.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

// On Windows pokerforge is a GUI program with no console of its own. When it
// is started from a terminal, print usage and errors there instead.
void attachParentConsole() {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

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
        "  --tab N             Equity tab: 0 Equity, 1 Hands, 2 Odds, 3 Details\n"
        "  --settings-tab N    Settings tab: 0 Controls, 1 Advanced\n"
        "  --seed N            fixed simulation seed\n"
        "  --theme T           classic or flat\n"
        "  --drop FILE         inject a file drop onto the window\n"
        "  --press K,K         inject key presses: f1-f12, ctrl+o, ctrl+e ...\n"
        "  --settings FILE     load and save settings here\n"
        "  --toggle-theme-at N flip the theme on frame N\n"
        "  --dialog D          open a dialog: import, export, csv, about, shortcuts\n"
        "  --screenshot FILE   write the last frame as a binary PPM");
}

// Whole-string number parsing that needs no CRT functions MSVC calls unsafe.
template <class T>
bool parseNumber(std::string_view s, T& out) {
    const char* end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc{} && r.ptr == end;
}

bool parseSize(std::string_view s, int& w, int& h) {
    const auto x = s.find('x');
    return x != std::string_view::npos && parseNumber(s.substr(0, x), w) && parseNumber(s.substr(x + 1), h) &&
           w > 0 && h > 0;
}

bool parseSeatList(std::string_view s, std::vector<int>& out) {
    while (!s.empty()) {
        const auto comma = s.find(',');
        int seat = 0;
        if (!parseNumber(s.substr(0, comma), seat)) return false;
        out.push_back(seat);
        if (comma == std::string_view::npos) break;
        s.remove_prefix(comma + 1);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    attachParentConsole();
    pf::ui::AppOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (i + 1 >= argc) { usage(); return 2; }
        const std::string_view value = argv[++i];
        bool ok = true;
        if (a == "--rules-dir") opt.rulesDir = std::string(value);
        else if (a == "--screenshot") opt.screenshotPath = std::string(value);
        else if (a == "--dialog") opt.dialog = std::string(value);
        else if (a == "--settings") opt.settingsPath = std::string(value);
        else if (a == "--toggle-theme-at") ok = parseNumber(value, opt.toggleThemeAtFrame);
        else if (a == "--drop") opt.dropFile = std::string(value);
        else if (a == "--press") {
            std::string_view rest = value;
            while (!rest.empty()) {
                const auto comma = rest.find(',');
                opt.keys.emplace_back(rest.substr(0, comma));
                if (comma == std::string_view::npos) break;
                rest.remove_prefix(comma + 1);
            }
        }
        else if (a == "--theme") {
            ok = value == "classic" || value == "flat";
            opt.theme = value == "flat" ? 1 : 0;
        }
        else if (a == "--frames") ok = parseNumber(value, opt.maxFrames);
        else if (a == "--advance") ok = parseNumber(value, opt.advanceStreets);
        else if (a == "--players") ok = parseNumber(value, opt.players);
        else if (a == "--tab") ok = parseNumber(value, opt.tab);
        else if (a == "--settings-tab") ok = parseNumber(value, opt.settingsTab);
        else if (a == "--seed") { ok = parseNumber(value, opt.seed); opt.fixedSeed = ok; }
        else if (a == "--size") ok = parseSize(value, opt.width, opt.height);
        else if (a == "--fold") ok = parseSeatList(value, opt.foldSeats);
        else ok = false;
        if (!ok) { usage(); return 2; }
    }
    return pf::ui::runApp(opt);
}
