#include "ui/Settings.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "ui/Export.hpp"

namespace pf::ui {

namespace {

std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

void warn(std::vector<std::string>* w, std::string text) {
    if (w) w->push_back(std::move(text));
}

}  // namespace

std::string_view themeKey(Theme t) { return t == Theme::FlatModern ? "flat_modern" : "classic_felt"; }
std::string_view themeDisplay(Theme t) { return t == Theme::FlatModern ? "Flat modern" : "Classic felt"; }

std::string settingsToJson(const UserSettings& s) {
    nlohmann::json doc = {
        {"theme", std::string(themeKey(s.theme))},
        {"speed", {{"runs_per_sec", s.runsPerSec}, {"unlimited", s.unlimited}}},
        {"threads", s.threads},
        {"seed", {{"fixed", s.fixedSeed}, {"value", s.seed}}},
        {"auto_stop_percent", s.autoStopPercent},
    };
    return doc.dump(2) + "\n";
}

UserSettings settingsFromJson(std::string_view text, std::vector<std::string>* warnings) {
    UserSettings out;
    const auto doc = nlohmann::json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        warn(warnings, "Settings file is not a JSON object; using defaults");
        return out;
    }

    auto number = [&](const nlohmann::json& obj, const char* key, double& dst, double lo, double hi) {
        auto it = obj.find(key);
        if (it == obj.end()) return;
        if (!it->is_number()) { warn(warnings, std::string("Setting \"") + key + "\" must be a number; ignored"); return; }
        dst = std::clamp(it->get<double>(), lo, hi);
    };
    auto flag = [&](const nlohmann::json& obj, const char* key, bool& dst) {
        auto it = obj.find(key);
        if (it == obj.end()) return;
        if (!it->is_boolean()) { warn(warnings, std::string("Setting \"") + key + "\" must be true or false; ignored"); return; }
        dst = it->get<bool>();
    };

    if (auto it = doc.find("theme"); it != doc.end()) {
        if (it->is_string() && it->get<std::string>() == themeKey(Theme::FlatModern)) out.theme = Theme::FlatModern;
        else if (it->is_string() && it->get<std::string>() == themeKey(Theme::ClassicFelt)) out.theme = Theme::ClassicFelt;
        else warn(warnings, "Unknown theme; using Classic felt");
    }
    if (auto it = doc.find("speed"); it != doc.end() && it->is_object()) {
        number(*it, "runs_per_sec", out.runsPerSec, 1000.0, 20'000'000.0);
        flag(*it, "unlimited", out.unlimited);
    }
    if (auto it = doc.find("threads"); it != doc.end()) {
        if (it->is_number_integer()) out.threads = static_cast<int>(std::clamp<std::int64_t>(it->get<std::int64_t>(), 0, 1024));
        else warn(warnings, "Setting \"threads\" must be a whole number; ignored");
    }
    if (auto it = doc.find("seed"); it != doc.end() && it->is_object()) {
        flag(*it, "fixed", out.fixedSeed);
        if (auto v = it->find("value"); v != it->end()) {
            if (v->is_number_unsigned()) out.seed = v->get<std::uint64_t>();
            else warn(warnings, "Setting \"value\" must be a non-negative whole number; ignored");
        }
    }
    number(doc, "auto_stop_percent", out.autoStopPercent, 0.0, 5.0);
    return out;
}

UserSettings loadSettings(const std::string& path, std::vector<std::string>* warnings) {
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) return {};  // first run: nothing saved yet
    std::stringstream ss;
    ss << in.rdbuf();
    return settingsFromJson(ss.str(), warnings);
}

bool saveSettings(const UserSettings& s, const std::string& path, std::string* error) {
    return writeTextFile(path, settingsToJson(s), error);  // creates the folder on a first run
}

}  // namespace pf::ui
