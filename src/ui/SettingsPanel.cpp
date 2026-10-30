#include <algorithm>
#include <cfloat>
#include <cstdio>

#include "imgui.h"

#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace {

ImU32 statusColor(const Snapshot& s) {
    if (!s.error.empty()) return Palette::Lose;
    if (s.autoStopped) return Palette::Split;
    if (s.paused) return Palette::Muted;
    return Palette::Win;
}

std::string statusText(const Snapshot& s) {
    if (!s.error.empty()) return "Error: " + s.error;
    if (s.autoStopped) return "Auto-stopped (precision reached)";
    if (s.paused) return "Paused";
    return "Running";
}

}  // namespace

void drawSettingsPanel(AppState& app) {
    if (!app.visible.settings) return;
    if (!ImGui::Begin(PanelTitles::Settings, &app.visible.settings)) {
        ImGui::End();
        return;
    }

    // ---- controls
    if (ImGui::Button("Restart")) app.restart();
    ImGui::SameLine();
    if (ImGui::Button(app.paused ? "Resume Simulation" : "Pause Simulation")) app.setPaused(!app.paused);
    if (ImGui::Button("Import Custom Rules (JSON)")) app.dialog = Dialog::ImportRules;

    ImGui::SeparatorText("Monte Carlo speed");
    ImGui::TextUnformatted("Runs per second");
    ImGui::BeginDisabled(app.unlimited);
    float rate = F(app.targetRate);
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
    if (app.fixedSeed) ImGui::TextDisabled("Press Restart to replay the same run.");

    ImGui::SeparatorText("Auto-stop");
    ImGui::TextUnformatted("Stop when hero equity is within");
    float pct = F(app.autoStopPercent);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##autostop", &pct, 0.0f, 5.0f, pct <= 0.0f ? "Off" : "+/- %.2f%%"))
        app.setAutoStopPercent(static_cast<double>(pct));

    // ---- status
    ImGui::SeparatorText("Status");
    const Snapshot& s = app.snap;
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(statusColor(s)));
    ImGui::TextUnformatted(statusText(s).c_str());
    ImGui::PopStyleColor();
    ImGui::Text("Runs: %s", withCommas(s.runs).c_str());
    ImGui::Text("Speed: %s runs/s", shortCount(s.runsPerSec).c_str());
    ImGui::Text("Last batch: %s runs in %.1f ms", shortCount(static_cast<double>(s.lastBatchRuns)).c_str(), static_cast<double>(s.lastBatchMs));

    // ---- benchmark
    ImGui::SeparatorText("Benchmark");
    ImGui::BeginDisabled(app.benchmarkRunning());
    if (ImGui::Button("Run Benchmark")) app.startBenchmark();
    ImGui::EndDisabled();
    if (app.benchmarkRunning()) {
        ImGui::SameLine();
        ImGui::TextDisabled("running...");
    } else if (app.haveBenchmark) {
        const BenchmarkResult& b = app.lastBenchmark;
        ImGui::Text("Evaluator: %.1f M evals/s/core", b.evalsPerSecSingle / 1e6);
        for (const ScalingPoint& p : b.scaling)
            ImGui::Text("%2d thread%s: %.2f M runs/s (%.1fx)", p.threads, p.threads == 1 ? " " : "s",
                        p.runsPerSec / 1e6, p.runsPerSec / b.scaling.front().runsPerSec);
    }
    ImGui::End();
}

}  // namespace pf::ui
