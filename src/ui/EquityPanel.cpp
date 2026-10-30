#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "core/Betting.hpp"
#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace {

bool haveSnapshot(const AppState& app) {
    return app.snap.error.empty() && app.snap.seats.size() == static_cast<std::size_t>(app.table.seatCount()) &&
           app.snap.runs > 0;
}

double bernoulliCI(double p, std::uint64_t n) {
    return n ? 1.96 * std::sqrt(std::max(0.0, p * (1.0 - p)) / static_cast<double>(n)) : 1.0;
}

void readout(const char* label, double p, std::uint64_t runs, ImU32 color) {
    ImGui::TextDisabled("%s", label);
    bigText(percent(p).c_str(), 1.9f, color);
    ImGui::TextDisabled("+/- %s", percent(bernoulliCI(p, runs), 2).c_str());
}

void drawReadouts(AppState& app) {
    const int hero = app.table.hero();
    const TableSeat& seat = app.table.seat(hero);
    if (seat.folded) {
        ImGui::TextDisabled("%s has folded.", seat.name.c_str());
        return;
    }
    if (!haveSnapshot(app)) {
        ImGui::TextDisabled(app.snap.error.empty() ? "Waiting for the first batch..." : "%s", app.snap.error.c_str());
        return;
    }
    const Snapshot& s = app.snap;
    const double win = s.win(hero), split = s.split(hero), lose = s.lose(hero);

    if (ImGui::BeginTable("##readouts", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); readout("Win", win, s.runs, Palette::Win);
        ImGui::TableNextColumn(); readout("Split", split, s.runs, Palette::Split);
        ImGui::TableNextColumn(); readout("Lose", lose, s.runs, Palette::Lose);
        ImGui::EndTable();
    }

    // Stacked bar: win | split | lose.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFontSize() * 0.8f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x1 = p.x + w * F(win), x2 = x1 + w * F(split);
    dl->AddRectFilled(p, {x1, p.y + h}, Palette::Win);
    dl->AddRectFilled({x1, p.y}, {x2, p.y + h}, Palette::Split);
    dl->AddRectFilled({x2, p.y}, {p.x + w, p.y + h}, Palette::Lose);
    dl->AddRect(p, {p.x + w, p.y + h}, IM_COL32(0, 0, 0, 120));
    ImGui::Dummy({w, h + 4.0f});

    ImGui::Text("Overall equity %s +/- %s   |   %s runs", percent(s.equity(hero)).c_str(),
                percent(s.equityCI95(hero), 2).c_str(), withCommas(s.runs).c_str());
}

void drawEquityChart(AppState& app, float height) {
    const auto& history = app.table.equityHistory();
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2{6.0f, 6.0f});
    if (ImPlot::BeginPlot("##equitychart", ImVec2{-1.0f, height}, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("Street", "Equity %", ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus,
                          ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus);
        // Equity is 0-100%, so the axis never wanders off that range.
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.3, 3.3, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImPlotCond_Always);
        static const double ticks[] = {0, 1, 2, 3};
        static const char* const names[] = {"Preflop", "Flop", "Turn", "River"};
        ImPlot::SetupAxisTicks(ImAxis_X1, ticks, 4, names);
        ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);

        for (int seat = 0; seat < app.table.seatCount(); ++seat) {
            std::vector<double> xs, ys;
            for (const EquityPoint& pt : history) {
                if (seat >= static_cast<int>(pt.equity.size()) || pt.equity[static_cast<std::size_t>(seat)] < 0) continue;
                xs.push_back(static_cast<double>(static_cast<int>(pt.street)));
                ys.push_back(pt.equity[static_cast<std::size_t>(seat)] * 100.0);
            }
            if (xs.empty()) continue;
            ImPlotSpec spec;
            spec.LineColor = toVec4(seatColor(seat));
            spec.LineWeight = seat == app.table.hero() ? 3.0f : 1.8f;
            spec.Marker = ImPlotMarker_Circle;
            spec.MarkerSize = 4.0f;
            ImPlot::PlotLine(app.table.seat(seat).name.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
}

// "Your hand now": the best made hand and the five cards that make it.
void drawHandCard(AppState& app) {
    const HandAnalysis& a = app.heroAnalysis;
    ImGui::SeparatorText("Your hand now");
    if (!a.valid) {
        ImGui::TextDisabled("Hero has no cards.");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(Palette::Gold));
    ImGui::TextWrapped("%s", a.made.description.c_str());
    ImGui::PopStyleColor();

    const float cw = ImGui::GetFontSize() * 2.3f, ch = cw * 1.4f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int k = 0;
    for (int id = 0; id < 64; ++id) {
        if (!(a.made.bestMask >> id & 1)) continue;
        drawCard(dl, {origin.x + F(k) * (cw * 1.15f), origin.y + ch * 0.1f}, {cw, ch}, static_cast<CardId>(id), false);
        ++k;
    }
    ImGui::Dummy({1.0f, ch * 1.2f});
}

void drawEquityTab(AppState& app) {
    drawReadouts(app);
    const float h = std::max(ImGui::GetFontSize() * 11.0f, ImGui::GetContentRegionAvail().y * 0.45f);
    ImGui::Spacing();
    drawEquityChart(app, h);
    drawHandCard(app);
}

struct RowShares {
    std::vector<double> byRiver, reach;
    bool exact = false;
};

RowShares computeShares(const AppState& app) {
    RowShares out;
    const HandAnalysis& a = app.heroAnalysis;
    std::array<double, kHandTypeCount> share{};
    out.exact = !a.rows.empty() && a.rows.front().exactShare >= 0;
    for (const HandTypeRow& r : a.rows) {
        const auto i = static_cast<std::size_t>(r.type);
        if (r.exactShare >= 0) share[i] = r.exactShare;
        else if (haveSnapshot(app)) share[i] = app.snap.handShare(app.table.hero(), r.type);
    }
    const auto reach = reachShares(app.applied, share);
    for (const HandTypeRow& r : a.rows) {
        out.byRiver.push_back(share[static_cast<std::size_t>(r.type)]);
        out.reach.push_back(reach[static_cast<std::size_t>(r.type)]);
    }
    return out;
}

void drawHandsTab(AppState& app) {
    drawHandCard(app);
    const HandAnalysis& a = app.heroAnalysis;
    if (!a.valid) return;
    const RowShares shares = computeShares(app);

    ImGui::SeparatorText("Hands you can get");
    if (!a.draws.empty()) {
        for (const std::string& d : a.draws) ImGui::BulletText("%s", d.c_str());
        ImGui::Spacing();
    }
    const float pad = ImGui::GetStyle().CellPadding.x * 2.0f + 4.0f;
    auto width = [&](const char* text) { return ImGui::CalcTextSize(text).x + pad; };
    // "Needs" is too wide for a column in a side panel, so it goes on a line of its own under each row.
    if (ImGui::BeginTable("##hands", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoClip)) {
        ImGui::TableSetupColumn("Hand", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Have", ImGuiTableColumnFlags_WidthFixed, width("Have"));
        ImGui::TableSetupColumn("By river", ImGuiTableColumnFlags_WidthFixed, width("By river"));
        ImGui::TableSetupColumn("Reach", ImGuiTableColumnFlags_WidthFixed, width("100.0%"));
        ImGui::TableSetupColumn("Outs", ImGuiTableColumnFlags_WidthFixed, width("Outs"));
        ImGui::TableHeadersRow();
        for (std::size_t i = a.rows.size(); i-- > 0;) {  // strongest first
            const HandTypeRow& r = a.rows[i];
            ImGui::TableNextRow();
            if (r.have) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(40, 110, 70, 90));
            const bool grey = !r.possible && !r.have;
            if (grey) ImGui::PushStyleColor(ImGuiCol_Text, toVec4(Palette::Muted));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(std::string(handTypeDisplay(r.type)).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.have ? "yes" : "");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(shares.byRiver[i]).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(shares.reach[i]).c_str());
            ImGui::TableNextColumn();
            if (r.outs >= 0) ImGui::Text("%d", r.outs); else ImGui::TextDisabled("-");
            if (grey) ImGui::PopStyleColor();
            if (r.outs > 0 && !r.needs.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("   needs %s", r.needs.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(shares.exact ? "By river and Reach are exact (every remaining board)."
                                     : "By river and Reach come from the simulation.");

    // Bar chart of the same data, in the rules' ranking order.
    std::vector<double> values;
    std::vector<std::string> names;
    std::vector<const char*> labels;
    std::vector<double> positions;
    double top = 5.0;
    int currentIndex = -1;
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        values.push_back(shares.byRiver[i] * 100.0);
        names.emplace_back(handTypeDisplay(a.rows[i].type));
        positions.push_back(static_cast<double>(i));
        top = std::max(top, values.back() * 1.15);
        if (a.rows[i].type == a.made.type) currentIndex = static_cast<int>(i);
    }
    for (const std::string& n : names) labels.push_back(n.c_str());
    if (ImPlot::BeginPlot("##handbars", ImVec2{-1.0f, ImGui::GetFontSize() * (4.0f + 1.7f * static_cast<float>(values.size()))},
                          ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("Chance by the river (%)", nullptr, ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus,
                          ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_Invert);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, top, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.6, static_cast<double>(values.size()) - 0.4, ImPlotCond_Always);
        ImPlot::SetupAxisTicks(ImAxis_Y1, positions.data(), static_cast<int>(positions.size()), labels.data());
        ImPlotSpec bars;
        bars.FillColor = toVec4(Palette::Win);
        bars.Flags = ImPlotBarsFlags_Horizontal;
        ImPlot::PlotBars("By river %", values.data(), static_cast<int>(values.size()), 0.67, 0.0, bars);
        if (currentIndex >= 0) {
            const double y = static_cast<double>(currentIndex);
            ImPlotSpec marker;
            marker.LineColor = toVec4(Palette::Gold);
            marker.LineWeight = 2.0f;
            marker.Flags = ImPlotInfLinesFlags_Horizontal;
            ImPlot::PlotInfLines("Your hand", &y, 1, marker);
        }
        ImPlot::EndPlot();
    }
}

void drawOddsTab(AppState& app) {
    ImGui::SeparatorText("Pot odds");
    ImGui::Checkbox("Use the table's pot and the hero's call", &app.potOddsFromTable);
    int pot = app.potInput, call = app.callInput;
    if (app.potOddsFromTable) {
        const BettingContext ctx = app.table.heroBettingContext();
        pot = ctx.pot;
        call = toCall(ctx);
    } else {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputInt("Pot", &app.potInput);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        ImGui::InputInt("Amount to call", &app.callInput);
        app.potInput = std::max(0, app.potInput);
        app.callInput = std::max(0, app.callInput);
        pot = app.potInput;
        call = app.callInput;
    }
    ImGui::Text("Pot: %d    To call: %d", pot, call);
    const double required = (pot + call) > 0 ? static_cast<double>(call) / static_cast<double>(pot + call) : 0.0;
    ImGui::Text("Equity needed to break even: %s", percent(required).c_str());

    if (!haveSnapshot(app) || app.table.seat(app.table.hero()).folded) {
        ImGui::TextDisabled("No equity yet.");
        return;
    }
    const double eq = app.snap.equity(app.table.hero());
    const double ev = eq * pot - (1.0 - eq) * call;
    ImGui::Text("Your equity: %s", percent(eq).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(ev >= 0 ? Palette::Win : Palette::Lose));
    ImGui::Text("EV of calling: %+.1f chips (%s)", ev, ev >= 0 ? "profitable" : "losing");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("EV = equity x pot - (1 - equity) x call");
}

void drawSimulationTab(AppState& app) {
    const Snapshot& s = app.snap;
    ImGui::Text("Runs: %s    Speed: %s runs/s    Epoch: %llu", withCommas(s.runs).c_str(),
                shortCount(s.runsPerSec).c_str(), static_cast<unsigned long long>(s.epoch));
    ImGui::Text("Last batch: %s runs in %.1f ms", shortCount(F(s.lastBatchRuns)).c_str(), static_cast<double>(s.lastBatchMs));
    if (!haveSnapshot(app)) {
        ImGui::TextDisabled(s.error.empty() ? "Waiting for the first batch..." : "%s", s.error.c_str());
        return;
    }
    if (ImGui::BeginTable("##seats", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const char* h : {"Player", "Win", "Split", "Lose", "Equity", "+/-"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int i = 0; i < app.table.seatCount(); ++i) {
            const bool folded = app.table.seat(i).folded;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(toVec4(seatColor(i)), "%s", app.table.seat(i).name.c_str());
            if (folded) {
                for (int c = 0; c < 5; ++c) { ImGui::TableNextColumn(); ImGui::TextDisabled("-"); }
                continue;
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(s.win(i)).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(s.split(i)).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(s.lose(i)).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(s.equity(i)).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(s.equityCI95(i), 2).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Equities of the players still in add up to 100%%.");
}

}  // namespace

void drawEquityPanel(AppState& app) {
    if (!app.visible.equity) return;
    if (!ImGui::Begin(PanelTitles::Equity, &app.visible.equity)) {
        ImGui::End();
        return;
    }
    // Hero selector.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Player");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    const int hero = app.table.hero();
    if (ImGui::BeginCombo("##hero", app.table.seat(hero).name.c_str())) {
        for (int i = 0; i < app.table.seatCount(); ++i)
            if (ImGui::Selectable(app.table.seat(i).name.c_str(), i == hero)) app.setHero(i);
        ImGui::EndCombo();
    }

    if (ImGui::BeginTabBar("##equitytabs")) {
        auto tabFlags = [&](int index) { return app.forceTab == index ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Equity", nullptr, tabFlags(0))) { drawEquityTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Hands", nullptr, tabFlags(1))) { drawHandsTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Odds", nullptr, tabFlags(2))) { drawOddsTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Simulation", nullptr, tabFlags(3))) { drawSimulationTab(app); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace pf::ui
