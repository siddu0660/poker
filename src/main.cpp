#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

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
    pf::ui::AppOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (i + 1 >= argc) { usage(); return 2; }
        const std::string_view value = argv[++i];
        bool ok = true;
        if (a == "--rules-dir") opt.rulesDir = std::string(value);
        else if (a == "--screenshot") opt.screenshotPath = std::string(value);
        else if (a == "--frames") ok = parseNumber(value, opt.maxFrames);
        else if (a == "--advance") ok = parseNumber(value, opt.advanceStreets);
        else if (a == "--players") ok = parseNumber(value, opt.players);
        else if (a == "--tab") ok = parseNumber(value, opt.tab);
        else if (a == "--seed") { ok = parseNumber(value, opt.seed); opt.fixedSeed = ok; }
        else if (a == "--size") ok = parseSize(value, opt.width, opt.height);
        else if (a == "--fold") ok = parseSeatList(value, opt.foldSeats);
        else ok = false;
        if (!ok) { usage(); return 2; }
    }
    return pf::ui::runApp(opt);
}
