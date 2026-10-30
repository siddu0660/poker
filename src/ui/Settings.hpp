#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pf::ui {

enum class Theme : std::uint8_t { ClassicFelt, FlatModern };

std::string_view themeKey(Theme t);      // "classic_felt", "flat_modern"
std::string_view themeDisplay(Theme t);  // "Classic felt", "Flat modern"

// What the app remembers between runs. Rules and layout are kept elsewhere.
struct UserSettings {
    Theme theme = Theme::ClassicFelt;
    double runsPerSec = 1'000'000;
    bool unlimited = false;
    int threads = 0;  // 0 = every core
    bool fixedSeed = false;
    std::uint64_t seed = 12345;
    double autoStopPercent = 0.0;
    bool exactWhenPossible = true;  // enumerate every deal when there are few enough

    bool operator==(const UserSettings&) const = default;
};

std::string settingsToJson(const UserSettings& s);

// Lenient: a missing or wrongly typed field keeps its default and adds a
// warning; out-of-range numbers are clamped. Never fails.
UserSettings settingsFromJson(std::string_view text, std::vector<std::string>* warnings = nullptr);

UserSettings loadSettings(const std::string& path, std::vector<std::string>* warnings = nullptr);
bool saveSettings(const UserSettings& s, const std::string& path, std::string* error = nullptr);

}  // namespace pf::ui
