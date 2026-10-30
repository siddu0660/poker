#pragma once
#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "core/HandAnalysis.hpp"
#include "core/Rules.hpp"
#include "engine/Benchmark.hpp"
#include "engine/Simulator.hpp"
#include "engine/TableState.hpp"
#include "util/Log.hpp"

namespace pf::ui {

// Every window title comes from this one table, so none can drift or repeat.
struct PanelTitles {
    static constexpr const char* Settings = "Settings & Controls";
    static constexpr const char* Rules = "Custom Rules";
    static constexpr const char* Table = "Table & Players";
    static constexpr const char* Equity = "Odds & Equity";
    static constexpr const char* Console = "Console";
};

struct PanelVisibility {
    bool settings = true, rules = true, table = true, equity = true, console = true;
};

enum class OrderPreset { Standard, ShortDeck, Custom };

struct SeatView {
    bool visible = false;  // the viewer may see this hand (known, or the cards are turned over)
    MadeHand made;
};

struct ConsoleState {
    bool autoScroll = true;
    std::array<bool, 5> levelOn{true, true, true, true, true};  // Info, Warn, Error, Sim, Game
    char filter[128] = {};
};

// Everything the panels share. The UI thread owns it; the simulation worker
// only ever sees an immutable Scenario and hands back Snapshot copies.
class AppState {
public:
    explicit AppState(std::string rulesDir);

    // Once per frame, before any panel is drawn.
    void frame();

    // ---- table actions (each one restarts the simulation on the new situation)
    void restart();
    void newHand();
    void nextStreet();
    void toggleFold(int seat);
    void setSeatCount(int n);
    void setHero(int seat);
    void setSeatKnown(int seat, bool known);

    // ---- simulation settings
    void setPaused(bool paused);
    void setSpeed(double runsPerSec, bool unlimited);
    void setThreads(int threads);
    void setSeed(bool fixed, std::uint64_t seed);
    void setAutoStopPercent(double percent);
    void startBenchmark();
    bool benchmarkRunning() const { return benchmark_.valid(); }

    // ---- rules
    bool draftDirty() const { return !(draft == applied); }
    void applyDraft();
    void revertDraft();
    bool importRules(const std::string& path);  // loads into the draft
    bool exportDraft(const std::string& path);
    void rescanPresets();
    OrderPreset orderPreset() const;
    void setOrderPreset(OrderPreset p);
    void setHandTypeEnabled(HandType t, bool on);
    void setWildMode(WildMode w);

    // ---- read by the panels
    RuleSet draft;  // edited in the Rules panel; Apply commits it
    RuleSet applied;
    std::shared_ptr<const Evaluator> evaluator;
    TableState table;
    SimEngine engine;
    Snapshot snap;

    std::vector<SeatView> seatViews;
    HandAnalysis heroAnalysis;

    std::string rulesDir;
    std::vector<std::string> presetFiles;  // file names in rulesDir

    double targetRate = 1'000'000;
    bool unlimited = false;
    int threads = 0;  // 0 = every core
    bool fixedSeed = false;
    std::uint64_t seedValue = 12345;
    double autoStopPercent = 0.0;
    bool paused = false;

    BenchmarkResult lastBenchmark;
    bool haveBenchmark = false;

    // pot-odds helper
    bool potOddsFromTable = true;
    int potInput = 0;
    int callInput = 0;

    PanelVisibility visible;
    ConsoleState console;
    int forceTab = -1;  // scripted runs only: keep this Equity tab selected
    bool layoutResetRequested = false;
    bool quitRequested = false;

    int heroSeat() const { return table.hero(); }

private:
    void pushScenario();
    void refreshAnalysis();
    void pollBenchmark();
    std::uint64_t pickSeed() const;

    std::uint64_t epoch_ = 0;
    std::uint64_t sceneVersion_ = 1;
    std::uint64_t analysedVersion_ = 0;
    bool pausedBeforeBenchmark_ = false;
    std::future<BenchmarkResult> benchmark_;
};

// "Deck Size (36/52)"-style labels all come from one place.
std::string levelName(LogLevel l);

}  // namespace pf::ui
