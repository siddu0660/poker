#include "ui/AppState.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <random>

#include "ui/Export.hpp"
#include "util/Paths.hpp"

namespace pf::ui {

namespace fs = std::filesystem;

namespace {

std::vector<HandType> referenceOrder(OrderPreset p) {
    auto order = p == OrderPreset::ShortDeck ? shortDeckOrder() : standardOrder();
    order.push_back(HandType::FiveOfAKind);
    return order;
}

}  // namespace

std::string levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Info: return "Info";
        case LogLevel::Warn: return "Warn";
        case LogLevel::Error: return "Error";
        case LogLevel::Sim: return "Sim";
        case LogLevel::Game: return "Game";
    }
    return "?";
}

AppState::AppState(std::string dir)
    : evaluator(std::make_shared<const Evaluator>(RuleSet{})),
      table(evaluator, 4, 20240601),
      rulesDir(std::move(dir)) {
    rescanPresets();

    RuleSet start;
    const fs::path holdem = pathFromUtf8(rulesDir) / "texas_holdem.json";
    if (fs::exists(holdem)) {
        auto res = loadRulesFile(pathToUtf8(holdem));
        if (res.rules) start = *res.rules;
    }
    applied = start;
    draft = start;
    evaluator = std::make_shared<const Evaluator>(applied);
    table.setRules(evaluator);
    logInfo("[Engine] Rules loaded: " + applied.name);
    engine.setThreads(threads);
    engine.setTargetRate(targetRate);
    pushScenario();
}

std::uint64_t AppState::pickSeed() const {
    if (fixedSeed) return seedValue;
    static std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

void AppState::pushScenario() {
    engine.setScenario(table.scenario(pickSeed(), table.street() == Street::River));
    engine.setAutoStop(autoStopPercent / 100.0);
    epoch_ = engine.snapshot().epoch;
    ++sceneVersion_;
}

void AppState::frame() {
    pollBenchmark();
    snap = engine.snapshot();

    // Feed the live equity of the street being played into the chart history.
    const auto n = static_cast<std::size_t>(table.seatCount());
    if (snap.epoch == epoch_ && snap.error.empty() && snap.runs >= 300 && snap.seats.size() == n) {
        std::vector<double> eq(n, kNoEquity);
        for (std::size_t i = 0; i < n; ++i)
            if (!table.seat(static_cast<int>(i)).folded) eq[i] = snap.equity(static_cast<int>(i));
        table.recordEquity(eq);
    }
    refreshAnalysis();
}

void AppState::refreshAnalysis() {
    if (analysedVersion_ == sceneVersion_) return;
    analysedVersion_ = sceneVersion_;
    const Evaluator& ev = *evaluator;
    const auto board = table.board();
    const bool showdown = table.street() == Street::River;

    seatViews.assign(static_cast<std::size_t>(table.seatCount()), SeatView{});
    for (int i = 0; i < table.seatCount(); ++i) {
        const TableSeat& s = table.seat(i);
        SeatView& v = seatViews[static_cast<std::size_t>(i)];
        v.visible = !s.folded && (s.known || showdown);
        if (v.visible) v.made = analyzeMadeHand(ev, s.hole, board);
    }
    heroAnalysis = analyzeHand(ev, table.seat(table.hero()).hole, board, table.deadForHero());
}

// ---------------------------------------------------------------- table actions

void AppState::restart() {
    logInfo("[Simulation] Restarted");
    pushScenario();
}

void AppState::newHand() {
    table.newHand();
    pushScenario();
}

void AppState::nextStreet() {
    // Save the equity of the street being left before the board changes.
    std::vector<double> eq(static_cast<std::size_t>(table.seatCount()), kNoEquity);
    if (snap.epoch == epoch_ && snap.seats.size() == eq.size() && snap.runs > 0)
        for (std::size_t i = 0; i < eq.size(); ++i)
            if (!table.seat(static_cast<int>(i)).folded) eq[i] = snap.equity(static_cast<int>(i));
    if (table.nextStreet(snap.runs > 0 ? eq : std::vector<double>{})) pushScenario();
}

void AppState::toggleFold(int seat) {
    if (table.toggleFold(seat)) {
        logInfo("[Game] " + table.seat(seat).name + (table.seat(seat).folded ? " folds" : " is back in"));
        pushScenario();
    } else {
        logWarn("[Game] The last player in the hand cannot fold");
    }
}

void AppState::setSeatCount(int n) {
    if (n == table.seatCount()) return;
    table.setSeatCount(n);
    pushScenario();
}

void AppState::setHero(int seat) {
    if (seat == table.hero()) return;
    table.setHero(seat);
    pushScenario();
}

void AppState::setSeatKnown(int seat, bool known) {
    table.setKnown(seat, known);
    pushScenario();
}

// ---------------------------------------------------------------- simulation settings

void AppState::setPaused(bool p) {
    paused = p;
    engine.setPaused(p);
}

void AppState::setSpeed(double runsPerSec, bool unl) {
    targetRate = runsPerSec;
    unlimited = unl;
    engine.setTargetRate(unl ? 0.0 : runsPerSec);
}

void AppState::setThreads(int t) {
    threads = t;
    engine.setThreads(t);
}

void AppState::setSeed(bool fixed, std::uint64_t seed) {
    fixedSeed = fixed;
    seedValue = seed;
}

void AppState::setAutoStopPercent(double percent) {
    autoStopPercent = percent;
    engine.setAutoStop(percent / 100.0);
}

void AppState::setExactWhenPossible(bool on) {
    exactWhenPossible = on;
    engine.setExactMode(on);
}

void AppState::startBenchmark() {
    if (benchmark_.valid()) return;
    pausedBeforeBenchmark_ = paused;
    engine.setPaused(true);  // the benchmark needs the cores to itself
    logInfo("[Engine] Benchmark started");
    const RuleSet rules = applied;
    benchmark_ = std::async(std::launch::async, [rules] {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));  // let an in-flight batch finish
        return runBenchmark(rules, 0.25);
    });
}

void AppState::pollBenchmark() {
    if (!benchmark_.valid()) return;
    if (benchmark_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    lastBenchmark = benchmark_.get();
    haveBenchmark = true;
    logInfo(lastBenchmark.summary);
    engine.setPaused(pausedBeforeBenchmark_);
}

// ---------------------------------------------------------------- rules

void AppState::rescanPresets() {
    presetFiles.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(pathFromUtf8(rulesDir), ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            presetFiles.push_back(pathToUtf8(entry.path().filename()));
    std::sort(presetFiles.begin(), presetFiles.end());
}

void AppState::applyDraft() {
    const auto warnings = sanitize(draft);
    for (const auto& w : warnings) logWarn("[Engine] " + w);
    applied = draft;
    evaluator = std::make_shared<const Evaluator>(applied);
    table.setRules(evaluator);  // deals a new hand with fresh stacks
    logInfo("[Engine] Rules loaded: " + applied.name);
    pushScenario();
}

void AppState::revertDraft() { draft = applied; }

bool AppState::importRules(const std::string& path) {
    const auto res = loadRulesFile(path);
    if (!res.rules) {
        logError("[Engine] Could not load rules: " + res.error);
        return false;
    }
    for (const auto& w : res.warnings) logWarn("[Engine] " + w);
    draft = *res.rules;
    logInfo("[Engine] Imported \"" + res.rules->name + "\" into the draft; press Apply Rules to use it");
    return true;
}

bool AppState::exportDraft(const std::string& path) {
    std::string err;
    if (!saveRulesFile(draft, path, &err)) {
        logError("[Engine] " + err);
        return false;
    }
    logInfo("[Engine] Exported rules to " + path);
    return true;
}

OrderPreset AppState::orderPreset() const {
    auto strip = [](std::vector<HandType> o) {
        o.erase(std::remove(o.begin(), o.end(), HandType::FiveOfAKind), o.end());
        return o;
    };
    const auto order = strip(draft.order);
    if (order == standardOrder()) return OrderPreset::Standard;
    if (order == shortDeckOrder()) return OrderPreset::ShortDeck;
    return OrderPreset::Custom;
}

void AppState::setOrderPreset(OrderPreset p) {
    if (p == OrderPreset::Custom) return;
    // Keep the enabled set, change only the strength order.
    std::vector<HandType> next;
    for (HandType t : referenceOrder(p))
        if (draft.isEnabled(t)) next.push_back(t);
    draft.order = std::move(next);
}

void AppState::setHandTypeEnabled(HandType t, bool on) {
    if (t == HandType::HighCard) return;
    auto& order = draft.order;
    const bool present = draft.isEnabled(t);
    if (on == present) return;
    if (!on) {
        order.erase(std::remove(order.begin(), order.end(), t), order.end());
        return;
    }
    // Insert where the current ranking style would put it.
    const OrderPreset p = orderPreset() == OrderPreset::ShortDeck ? OrderPreset::ShortDeck : OrderPreset::Standard;
    const auto ref = referenceOrder(p);
    const auto rank = [&](HandType x) { return std::find(ref.begin(), ref.end(), x) - ref.begin(); };
    auto pos = order.begin();
    while (pos != order.end() && rank(*pos) < rank(t)) ++pos;
    order.insert(pos, t);
}

void AppState::setWildMode(WildMode w) {
    RuleSet& r = draft;
    r.wild = w;
    if (w == WildMode::Jokers && r.jokers == 0) r.jokers = 1;
    if (w == WildMode::None)
        r.order.erase(std::remove(r.order.begin(), r.order.end(), HandType::FiveOfAKind), r.order.end());
}

}  // namespace pf::ui

namespace pf::ui {

namespace {

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "Short Deck (6+)" -> "short_deck_6"
std::string fileSafe(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "rules" : out;
}

}  // namespace

UserSettings AppState::currentSettings() const {
    UserSettings s;
    s.theme = theme;
    s.runsPerSec = targetRate;
    s.unlimited = unlimited;
    s.threads = threads;
    s.fixedSeed = fixedSeed;
    s.seed = seedValue;
    s.autoStopPercent = autoStopPercent;
    s.exactWhenPossible = exactWhenPossible;
    return s;
}

void AppState::applySettings(const UserSettings& s) {
    theme = s.theme;
    setSpeed(s.runsPerSec, s.unlimited);
    setThreads(s.threads);
    setSeed(s.fixedSeed, s.seed);
    setAutoStopPercent(s.autoStopPercent);
    setExactWhenPossible(s.exactWhenPossible);
}

bool AppState::exportEquityCsv(const std::string& path) {
    if (table.equityHistory().empty()) {
        logWarn("[Engine] Nothing to export yet: no equity has been recorded for this hand");
        return false;
    }
    std::string err;
    if (!writeTextFile(path, equityCsv(table), &err)) {
        logError("[Engine] " + err);
        return false;
    }
    logInfo("[Engine] Exported the equity history to " + path);
    return true;
}

bool AppState::handleDroppedFile(const std::string& path) {
    const fs::path p = pathFromUtf8(path);
    if (lowerCase(pathToUtf8(p.extension())) != ".json") {
        logWarn("[Engine] Ignored the dropped file \"" + pathToUtf8(p.filename()) +
                "\": only .json rules files can be imported");
        return false;
    }
    if (!importRules(path)) return false;
    visible.rules = true;  // so the imported draft is in front of the user
    return true;
}

std::string AppState::defaultRulesExportPath() const {
    const std::string file = fileSafe(draft.name) + ".json";
    return pathToUtf8(pathFromUtf8(rulesDir) / file);
}

std::string AppState::defaultCsvPath() const {
    const std::string file = "equity_hand_" + std::to_string(table.handNumber()) + ".csv";
    return exportDir.empty() ? file : pathToUtf8(pathFromUtf8(exportDir) / file);
}

}  // namespace pf::ui
