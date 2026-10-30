#include <algorithm>
#include <cstdint>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace {

ImU32 levelColor(LogLevel l) {
    switch (l) {
        case LogLevel::Info: return IM_COL32(205, 208, 215, 255);
        case LogLevel::Warn: return Palette::Split;
        case LogLevel::Error: return Palette::Lose;
        case LogLevel::Sim: return IM_COL32(110, 205, 230, 255);
        case LogLevel::Game: return Palette::Win;
    }
    return IM_COL32_WHITE;
}

std::string timestamp(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &tm);
    return buf;
}

bool containsNoCase(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    const std::string n(needle);
    return std::search(hay.begin(), hay.end(), n.begin(), n.end(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           }) != hay.end();
}

bool passes(const AppState& app, const LogLine& l) {
    return app.console.levelOn[static_cast<std::size_t>(l.level)] && containsNoCase(l.text, app.console.filter);
}

}  // namespace

void drawConsolePanel(AppState& app) {
    if (!app.visible.console) return;
    if (!ImGui::Begin(PanelTitles::Console, &app.visible.console)) {
        ImGui::End();
        return;
    }
    ConsoleState& c = app.console;

    // The log is copied only when something new arrived.
    static std::vector<LogLine> lines;
    static std::uint64_t seen = ~0ull;
    const std::uint64_t total = Log::instance().totalAdded();
    const bool grew = total != seen;
    if (grew) {
        lines = Log::instance().snapshot();
        seen = total;
    }

    // ---- toolbar
    if (ImGui::Button("Clear")) {
        Log::instance().clear();
        lines.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        std::string all;
        for (const LogLine& l : lines)
            if (passes(app, l)) all += "[" + timestamp(l.time) + "] " + l.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &c.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Levels")) ImGui::OpenPopup("##levels");
    if (ImGui::BeginPopup("##levels")) {
        for (std::size_t i = 0; i < c.levelOn.size(); ++i) {
            const LogLevel l = static_cast<LogLevel>(i);
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(levelColor(l)));
            ImGui::Checkbox(levelName(l).c_str(), &c.levelOn[i]);
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter text", c.filter, sizeof c.filter);

    // ---- lines (clipped: only the visible rows are drawn)
    std::vector<const LogLine*> shown;
    shown.reserve(lines.size());
    for (const LogLine& l : lines)
        if (passes(app, l)) shown.push_back(&l);

    ImGui::Separator();
    if (ImGui::BeginChild("##logscroll", ImVec2{0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const LogLine& l = *shown[static_cast<std::size_t>(i)];
                ImGui::TextDisabled("%s", timestamp(l.time).c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, toVec4(levelColor(l.level)));
                ImGui::TextUnformatted(l.text.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (c.autoScroll && grew) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace pf::ui
