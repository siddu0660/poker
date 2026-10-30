#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <string>

#include <SDL.h>

#include "imgui.h"
#include "implot.h"

#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"
#include "util/Paths.hpp"

#ifndef PF_VERSION
#define PF_VERSION "dev"
#endif

namespace pf::ui {

namespace {

constexpr const char* kImportTitle = "Import Custom Rules";
constexpr const char* kExportRulesTitle = "Export Rules";
constexpr const char* kExportCsvTitle = "Export Equity CSV";
constexpr const char* kAboutTitle = "About PokerForge";
constexpr const char* kShortcutsTitle = "Keyboard Shortcuts";

struct DialogState {
    Dialog shown = Dialog::None;  // which dialog the popup was last opened for
    int selected = 0;
    char path[512] = {};
    std::string message;          // the last problem, shown in red
};

DialogState& state() {
    static DialogState s;
    return s;
}

const char* titleOf(Dialog d) {
    switch (d) {
        case Dialog::ImportRules: return kImportTitle;
        case Dialog::ExportRules: return kExportRulesTitle;
        case Dialog::ExportCsv: return kExportCsvTitle;
        case Dialog::About: return kAboutTitle;
        case Dialog::Shortcuts: return kShortcutsTitle;
        case Dialog::None: break;
    }
    return "";
}

void setPath(const std::string& p) { std::snprintf(state().path, sizeof state().path, "%s", p.c_str()); }

void showMessage() {
    if (state().message.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(Palette::Lose));
    ImGui::TextWrapped("%s", state().message.c_str());
    ImGui::PopStyleColor();
}

void importBody(AppState& app, bool& close) {
    DialogState& d = state();
    ImGui::TextUnformatted("Pick a rules file from the rules folder:");
    const char* preview = app.presetFiles.empty() ? "(none found)"
                                                  : app.presetFiles[static_cast<std::size_t>(d.selected)].c_str();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##presetfile", preview)) {
        for (std::size_t i = 0; i < app.presetFiles.size(); ++i)
            if (ImGui::Selectable(app.presetFiles[i].c_str(), static_cast<std::size_t>(d.selected) == i))
                d.selected = static_cast<int>(i);
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("...or type a path (it overrides the list):");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##rulespath", d.path, sizeof d.path);
    ImGui::TextDisabled("You can also drop a .json file onto the window.");
    showMessage();
    ImGui::Spacing();

    const bool typed = d.path[0] != '\0';
    ImGui::BeginDisabled(!typed && app.presetFiles.empty());
    if (ImGui::Button("Import")) {
        const std::string path = typed ? std::string(d.path)
                                       : pathToUtf8(pathFromUtf8(app.rulesDir) /
                                                    app.presetFiles[static_cast<std::size_t>(d.selected)]);
        if (app.importRules(path)) close = true;
        else d.message = "Could not import that file. The Console has the reason.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) close = true;
}

void exportBody(AppState& app, bool& close, const char* what, bool csv) {
    DialogState& d = state();
    ImGui::TextUnformatted(what);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##exportpath", d.path, sizeof d.path);
    showMessage();
    ImGui::Spacing();
    ImGui::BeginDisabled(d.path[0] == '\0');
    if (ImGui::Button("Export")) {
        const bool ok = csv ? app.exportEquityCsv(d.path) : app.exportDraft(d.path);
        if (ok) {
            if (!csv) app.rescanPresets();
            close = true;
        } else {
            d.message = "Nothing was written. The Console has the reason.";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) close = true;
}

void aboutBody(bool& close) {
    bigText("PokerForge", 1.8f, Palette::Gold);
    ImGui::Text("Version %s", PF_VERSION);
    ImGui::Spacing();
    ImGui::TextWrapped("Real-time poker odds with a parallel Monte Carlo engine, an exact hand evaluator and "
                       "rules you can change without recompiling.");
    ImGui::Spacing();
    SDL_version sdl{};
    SDL_GetVersion(&sdl);
    ImGui::TextDisabled("Dear ImGui %s", IMGUI_VERSION);
    ImGui::TextDisabled("ImPlot %s", IMPLOT_VERSION);
    ImGui::TextDisabled("SDL %d.%d.%d", sdl.major, sdl.minor, sdl.patch);
    ImGui::TextDisabled("%d simulation threads available", hardwareThreads());
    ImGui::Spacing();
    if (ImGui::Button("Close")) close = true;
}

void shortcutsBody(bool& close) {
    struct Row { const char* keys; const char* what; };
    static const Row rows[] = {
        {"F2", "New hand"},
        {"F3", "Next street"},
        {"F4", "Pause or resume the simulation"},
        {"F5", "Restart the simulation"},
        {"Ctrl+O", "Import rules"},
        {"Ctrl+S", "Export the draft rules"},
        {"Ctrl+E", "Export the equity history (CSV)"},
        {"F1", "This list"},
        {"Click a seat", "Fold or rejoin the hand"},
        {"Right-click a seat", "Fold, known or random cards, set hero"},
        {"Drop a .json file", "Import it into the rules draft"},
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const Row& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(toVec4(Palette::Gold), "%s", r.keys);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.what);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Close")) close = true;
}

}  // namespace

void drawDialogs(AppState& app) {
    DialogState& d = state();
    if (app.dialog != d.shown) {  // a new request: set up its fields and open the popup
        d.shown = app.dialog;
        d.message.clear();
        d.selected = 0;
        d.path[0] = '\0';
        switch (d.shown) {
            case Dialog::ImportRules: app.rescanPresets(); break;
            case Dialog::ExportRules: setPath(app.defaultRulesExportPath()); break;
            case Dialog::ExportCsv: setPath(app.defaultCsvPath()); break;
            default: break;
        }
        if (d.shown != Dialog::None) ImGui::OpenPopup(titleOf(d.shown));
    }
    if (d.shown == Dialog::None) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 34.0f, 0.0f}, ImGuiCond_Appearing);
    bool open = true;
    bool close = false;
    if (ImGui::BeginPopupModal(titleOf(d.shown), &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        switch (d.shown) {
            case Dialog::ImportRules: importBody(app, close); break;
            case Dialog::ExportRules: exportBody(app, close, "Write the draft rules from the Rules panel to:", false); break;
            case Dialog::ExportCsv: exportBody(app, close, "Write the equity history of this hand to:", true); break;
            case Dialog::About: aboutBody(close); break;
            case Dialog::Shortcuts: shortcutsBody(close); break;
            case Dialog::None: break;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
        if (close) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!open || close) {
        app.dialog = Dialog::None;
        d.shown = Dialog::None;
    }
}

}  // namespace pf::ui
