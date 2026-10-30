#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace pf {

// Paths travel through the program as UTF-8 strings: SDL, Dear ImGui and the
// JSON files all use UTF-8. On Windows, std::filesystem::path(std::string) and
// path::string() use the ANSI code page instead, which breaks (or throws on)
// folders with other characters, such as a user name in Greek or Chinese.
// Every conversion between the two goes through these helpers.
inline std::filesystem::path pathFromUtf8(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace pf
