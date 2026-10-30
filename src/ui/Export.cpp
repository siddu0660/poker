#include "ui/Export.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace pf::ui {

std::string csvCell(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) return text;
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string equityCsv(const TableState& table) {
    std::string out = "hand,street";
    for (int i = 0; i < table.seatCount(); ++i) out += "," + csvCell(table.seat(i).name);
    out += "\n";
    for (const EquityPoint& p : table.equityHistory()) {
        out += std::to_string(table.handNumber()) + "," + std::string(streetName(p.street));
        for (int i = 0; i < table.seatCount(); ++i) {
            out += ",";
            const auto k = static_cast<std::size_t>(i);
            if (k < p.equity.size() && p.equity[k] >= 0) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.4f", p.equity[k] * 100.0);
                out += buf;
            }
        }
        out += "\n";
    }
    return out;
}

bool writeTextFile(const std::string& path, const std::string& text, std::string* error) {
    const auto p = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) { if (error) *error = "Cannot write " + path; return false; }
    out << text;
    if (!out) { if (error) *error = "Write failed for " + path; return false; }
    return true;
}

}  // namespace pf::ui
