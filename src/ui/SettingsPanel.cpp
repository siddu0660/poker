#include <algorithm>
#include <cfloat>
#include <cstdio>

#include "imgui.h"

#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace {

constexpr double kDefaultPrecisionPercent = 0.05;

ImU32 statusColor(const Snapshot& s) {
    if (!s.error.empty()) return Palette::Lose;
    if (s.exact || s.computingExact) return Palette::Win;
    if (s.paused) return Palette::Muted;
    return s.autoStopped ? Palette::Win : Palette::Split;
}

std::string pm(double fraction) {  // "+/- 0.05%"
    return "+/- " + percent(fraction, 2);
}

// One line that says what the numbers on screen are.
std::string headline(const Snapshot& s) {
    if (!s.error.empty()) return "Error: " + s.error;
    if (s.computingExact) return "Computing every deal...";
    if (s.exact) return "Exact: every one of " + withCommas(s.exactDeals) + " deals counted";
    if (s.paused) return "Paused";
    if (s.runs == 0) return "Starting...";
    if (s.autoStopped) return "Estimate settled, " + pm(s.worstCI95());
    return "Estimating, currently " + pm(s.worstCI95());
}

std::string explanation(const Snapshot& s) {
    if (!s.error.empty() || s.computingExact || s.runs == 0) return {};
    if (s.exact) return "Few enough possible deals to count them all, so these odds are exact.";
    return "Too many possible deals to count, so the odds are estimated from random deals. "
           "More deals make the estimate tighter.";
}

void controlsTab(AppState& app) {
    if (ImGui::Button("Restart")) app.restart();
    ImGui::SameLine();
    if (ImGui::Button(app.paused ? "Resume" : "Pause")) app.setPaused(!app.paused);
    if (ImGui::Button("Import Custom Rules (JSON)")) app.dialog = Dialog::ImportRules;

    // ---- what the numbers are
    ImGui::SeparatorText("Result");
    const Snapshot& s = app.snap;
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(statusColor(s)));
    ImGui::TextWrapped("%s", headline(s).c_str());
    ImGui::PopStyleColor();
    const std::string why = explanation(s);
    if (!why.empty()) hintText(why.c_str());

    // ---- accuracy
    ImGui::SeparatorText("Accuracy");
    bool exact = app.exactWhenPossible;
    if (ImGui::Checkbox("Count every deal when possible", &exact)) app.setExactWhenPossible(exact);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When there are at most %s possible deals, evaluate every one of them.\n"
                          "The result is exact and nothing more needs to run.",
                          withCommas(SimEngine::kDefaultExactDeals).c_str());

    ImGui::BeginDisabled(app.exactWhenPossible && s.exact);
    bool keepRefining = app.autoStopPercent <= 0.0;
    if (ImGui::Checkbox("Keep refining estimates", &keepRefining))
        app.setAutoStopPercent(keepRefining ? 0.0 : kDefaultPrecisionPercent);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Otherwise an estimate settles, and the CPU goes idle,\n"
                          "once every player's equity is within the precision below.");
    ImGui::BeginDisabled(keepRefining);
    ImGui::TextUnformatted("Settle when every equity is within");
    float pct = static_cast<float>(keepRefining ? kDefaultPrecisionPercent : app.autoStopPercent);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##precision", &pct, 0.01f, 2.0f, "+/- %.2f%%", ImGuiSliderFlags_Logarithmic))
        app.setAutoStopPercent(static_cast<double>(pct));
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    hintText("Speed, threads and the random seed are under Advanced.");
}

void advancedTab(AppState& app) {
    const Snapshot& s = app.snap;
    ImGui::TextWrapped("For spots with too many deals to count, the odds come from dealing random hands and "
                       "boards and counting who wins. These settings control that.");

    ImGui::SeparatorText("Speed");
    ImGui::TextUnformatted("Random deals per second");
    ImGui::BeginDisabled(app.unlimited);
    float rate = static_cast<float>(app.targetRate);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##speed", &rate, 1000.0f, 20'000'000.0f, "%.0f", ImGuiSliderFlags_Logarithmic))
        app.setSpeed(static_cast<double>(rate), app.unlimited);
    ImGui::EndDisabled();
    bool unlimited = app.unlimited;
    if (ImGui::Checkbox("Unlimited", &unlimited)) app.setSpeed(app.targetRate, unlimited);

    const int hw = hardwareThreads();
    int threads = app.threads <= 0 ? hw : std::min(app.threads, hw);
    ImGui::TextUnformatted("Threads");
    ImGui::SetNextItemWidth(-FLT_MIN);
    char threadFmt[32];
    std::snprintf(threadFmt, sizeof threadFmt, "%%d of %d", hw);
    if (ImGui::SliderInt("##threads", &threads, 1, hw, threadFmt)) app.setThreads(threads == hw ? 0 : threads);

    ImGui::SeparatorText("Reproducibility");
    bool fixed = app.fixedSeed;
    if (ImGui::Checkbox("Fixed seed", &fixed)) app.setSeed(fixed, app.seedValue);
    ImGui::BeginDisabled(!app.fixedSeed);
    ImGui::SetNextItemWidth(-FLT_MIN);
    unsigned long long seed = app.seedValue;
    if (ImGui::InputScalar("##seed", ImGuiDataType_U64, &seed)) app.setSeed(app.fixedSeed, seed);
    ImGui::EndDisabled();
    if (app.fixedSeed) hintText("Press Restart to replay the same deals.");

    ImGui::SeparatorText("Statistics");
    ImGui::Text("Deals evaluated: %s", withCommas(s.runs).c_str());
    ImGui::Text("Speed: %s deals/s", shortCount(s.runsPerSec).c_str());
    ImGui::Text("Last batch: %s deals in %.1f ms", shortCount(static_cast<double>(s.lastBatchRuns)).c_str(),
                static_cast<double>(s.lastBatchMs));
    ImGui::Text("Counted exactly up to: %s deals", withCommas(SimEngine::kDefaultExactDeals).c_str());
    hintText("The Console's Levels menu can show the per-second lines (level Sim).");

    ImGui::SeparatorText("Benchmark");
    ImGui::BeginDisabled(app.benchmarkRunning());
    if (ImGui::Button("Run Benchmark")) app.startBenchmark();
    ImGui::EndDisabled();
    if (app.benchmarkRunning()) {
        ImGui::SameLine();
        ImGui::TextDisabled("running...");
    } else if (app.haveBenchmark) {
        const BenchmarkResult& b = app.lastBenchmark;
        ImGui::Text("Evaluator: %.1f M hands/s/core", b.evalsPerSecSingle / 1e6);
        for (const ScalingPoint& p : b.scaling)
            ImGui::Text("%2d thread%s: %.2f M deals/s (%.1fx)", p.threads, p.threads == 1 ? " " : "s",
                        p.runsPerSec / 1e6, p.runsPerSec / b.scaling.front().runsPerSec);
    }
}

}  // namespace

void drawSettingsPanel(AppState& app) {
    if (!app.visible.settings) return;
    if (!ImGui::Begin(PanelTitles::Settings, &app.visible.settings)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##settingstabs")) {
        auto flags = [&](int index) { return app.forceSettingsTab == index ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Controls", nullptr, flags(0))) { controlsTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Advanced", nullptr, flags(1))) { advancedTab(app); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace pf::ui
