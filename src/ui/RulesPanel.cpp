#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>

#include "imgui.h"

#include "core/Betting.hpp"
#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace {

// The deck slider moves in steps of four: 24, 28, ... 52.
int snapDeckSize(int v) { return std::clamp(24 + ((v - 24 + 2) / 4) * 4, 24, 52); }

struct ExportState {
    char path[512] = {};
    bool initialised = false;
};

}  // namespace

void drawRulesPanel(AppState& app) {
    if (!app.visible.rules) return;
    if (!ImGui::Begin(PanelTitles::Rules, &app.visible.rules)) {
        ImGui::End();
        return;
    }
    RuleSet& d = app.draft;
    const bool dirty = app.draftDirty();

    // ---- presets
    ImGui::TextUnformatted("Preset");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##preset", d.name.c_str())) {
        if (ImGui::IsWindowAppearing()) app.rescanPresets();
        for (const std::string& file : app.presetFiles) {
            if (ImGui::Selectable(file.c_str(), false))
                app.importRules((std::filesystem::path(app.rulesDir) / file).string());
        }
        ImGui::EndCombo();
    }

    // ---- apply / revert, always visible
    {
        if (dirty) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 130, 80, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(55, 160, 100, 255));
        }
        if (ImGui::Button("Apply Rules")) app.applyDraft();
        if (dirty) ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button("Revert")) app.revertDraft();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(dirty ? "unapplied changes" : "up to date");
    }

    // ---- variables
    ImGui::SeparatorText("Variables");
    ImGui::SetNextItemWidth(-FLT_MIN);
    int deck = d.deckSize;
    if (ImGui::SliderInt("##deck", &deck, 24, 52, "Deck Size (%d/52)")) d.deckSize = snapDeckSize(deck);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##sb", &d.smallBlind, 0, 500, "Small Blind: %d");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##bb", &d.bigBlind, 1, 1000, "Big Blind: %d");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##ante", &d.ante, 0, 200, "Ante: %d");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##stack", &d.startingStack, 100, 100000, "Starting Stack: %d", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##jokers", &d.jokers, 0, 2, "Jokers: %d");

    // ---- hand rankings
    ImGui::SeparatorText("Hand Rankings");
    for (const HandTypeInfo& info : handTypeTable()) {
        if (info.type == HandType::FiveOfAKind && !d.hasWilds()) continue;  // needs wild cards
        const bool locked = info.type == HandType::HighCard;
        bool on = d.isEnabled(info.type);
        ImGui::BeginDisabled(locked);
        if (ImGui::Checkbox(std::string(info.display).c_str(), &on)) app.setHandTypeEnabled(info.type, on);
        ImGui::EndDisabled();
    }
    ImGui::Checkbox("Ace plays low (wheel straight)", &d.wheelStraight);

    ImGui::TextUnformatted("Ranking order");
    const OrderPreset preset = app.orderPreset();
    const char* presetName = preset == OrderPreset::Standard   ? "Standard"
                             : preset == OrderPreset::ShortDeck ? "Short Deck"
                                                                : "Custom";
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##order", presetName)) {
        if (ImGui::Selectable("Standard", preset == OrderPreset::Standard)) app.setOrderPreset(OrderPreset::Standard);
        if (ImGui::Selectable("Short Deck (flush > full house, trips > straight)", preset == OrderPreset::ShortDeck))
            app.setOrderPreset(OrderPreset::ShortDeck);
        if (preset == OrderPreset::Custom) ImGui::Selectable("Custom", true);
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Weakest to strongest:");
    for (std::size_t i = 0; i < d.order.size(); ++i)
        ImGui::TextDisabled("%zu. %s", i + 1, std::string(handTypeDisplay(d.order[i])).c_str());

    // ---- wild cards
    ImGui::SeparatorText("Wild Cards");
    for (WildMode w : {WildMode::None, WildMode::Deuces, WildMode::Jokers, WildMode::OneEyedJacks}) {
        if (ImGui::RadioButton(std::string(wildModeDisplay(w)).c_str(), d.wild == w)) app.setWildMode(w);
    }

    // ---- betting
    ImGui::SeparatorText("Betting Structure");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##betting", std::string(bettingDisplay(d.betting)).c_str())) {
        for (BettingStructure b : {BettingStructure::NoLimit, BettingStructure::PotLimit, BettingStructure::FixedLimit})
            if (ImGui::Selectable(std::string(bettingDisplay(b)).c_str(), d.betting == b)) d.betting = b;
        ImGui::EndCombo();
    }
    if (d.betting == BettingStructure::FixedLimit) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##raises", &d.maxRaises, 1, 10, "Max raises per round: %d");
    }
    {
        const BettingContext ctx = app.table.heroBettingContext();
        const RaiseRange range = raiseRange(d, ctx);
        if (range.canRaise)
            ImGui::TextDisabled("At the current pot (%d): raise to %d..%d", ctx.pot, range.minTo, range.maxTo);
        else
            ImGui::TextDisabled("At the current pot (%d): no raise possible", ctx.pot);
    }

    // ---- export
    ImGui::SeparatorText("Export");
    static ExportState ex;
    if (!ex.initialised) {
        std::snprintf(ex.path, sizeof ex.path, "%s", (std::filesystem::path(app.rulesDir) / "my_rules.json").string().c_str());
        ex.initialised = true;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##exportpath", ex.path, sizeof ex.path);
    if (ImGui::Button("Export Draft Rules")) {
        if (app.exportDraft(ex.path)) app.rescanPresets();
    }
    ImGui::End();
}

}  // namespace pf::ui
