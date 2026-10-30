#include "test_main.hpp"
#include "ui/AppState.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

using namespace pf;
using namespace pf::ui;

namespace {

bool waitFor(const std::function<bool()>& cond, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond();
}

// Pumps frame() until the simulation has produced something for the current situation.
bool pump(AppState& app, std::uint64_t minRuns, double seconds = 20) {
    return waitFor([&] { app.frame(); return app.snap.error.empty() && app.snap.runs >= minRuns; }, seconds);
}

AppState makeApp() { return AppState(PF_RULES_DIR); }

}  // namespace

PF_TEST(app_starts_with_holdem_presets_and_a_running_simulation) {
    AppState app = makeApp();
    CHECK_EQ(app.applied.name, std::string("Texas Hold'em"));
    CHECK(!app.draftDirty());
    CHECK(app.presetFiles.size() >= 4);
    CHECK(std::find(app.presetFiles.begin(), app.presetFiles.end(), "short_deck_36.json") != app.presetFiles.end());
    CHECK(pump(app, 2000));
    CHECK(app.heroAnalysis.valid);
    CHECK_EQ(app.seatViews.size(), static_cast<std::size_t>(app.table.seatCount()));
    CHECK(app.seatViews[static_cast<std::size_t>(app.heroSeat())].visible);
    CHECK(!app.seatViews[1].visible);  // opponents are random hands
}

PF_TEST(app_draft_is_separate_until_applied) {
    AppState app = makeApp();
    app.draft.bigBlind = 50;
    app.draft.smallBlind = 25;
    CHECK(app.draftDirty());
    CHECK_EQ(app.applied.bigBlind, 20);
    app.revertDraft();
    CHECK(!app.draftDirty());
    CHECK_EQ(app.draft.bigBlind, 20);

    app.draft.bigBlind = 50;
    app.draft.smallBlind = 25;
    const int handBefore = app.table.handNumber();
    app.applyDraft();
    CHECK(!app.draftDirty());
    CHECK_EQ(app.applied.bigBlind, 50);
    CHECK_EQ(app.table.handNumber(), handBefore + 1);  // applying deals a new hand
    CHECK_EQ(app.table.pot(), 75);
}

PF_TEST(app_apply_sanitizes_and_warns) {
    Log::instance().clear();
    AppState app = makeApp();
    app.draft.deckSize = 37;
    app.draft.jokers = 5;
    app.applyDraft();
    CHECK_EQ(app.applied.deckSize, 36);
    CHECK_EQ(app.applied.jokers, 2);
    bool warned = false;
    for (const auto& l : Log::instance().snapshot()) warned = warned || l.level == LogLevel::Warn;
    CHECK(warned);
    CHECK(!app.draftDirty());  // the draft shows what was actually applied
}

PF_TEST(app_hand_type_toggles_keep_rank_order) {
    AppState app = makeApp();
    app.setHandTypeEnabled(HandType::Flush, false);
    CHECK(!app.draft.isEnabled(HandType::Flush));
    app.setHandTypeEnabled(HandType::HighCard, false);  // locked on
    CHECK(app.draft.isEnabled(HandType::HighCard));
    app.setHandTypeEnabled(HandType::Flush, true);
    CHECK(app.draft == app.applied);  // back where it was, including position in the order

    app.setOrderPreset(OrderPreset::ShortDeck);
    CHECK(app.orderPreset() == OrderPreset::ShortDeck);
    CHECK(app.draft.order == shortDeckOrder());
    app.setHandTypeEnabled(HandType::TwoPair, false);
    app.setHandTypeEnabled(HandType::TwoPair, true);
    CHECK(app.draft.order == shortDeckOrder());  // re-enabled in the short-deck position

    app.setHandTypeEnabled(HandType::Flush, false);
    CHECK(app.orderPreset() == OrderPreset::Custom);
    app.setOrderPreset(OrderPreset::Standard);   // keeps the enabled set, restores the strength order
    CHECK(!app.draft.isEnabled(HandType::Flush));
    app.setOrderPreset(OrderPreset::Standard);
}

PF_TEST(app_wild_mode_edits_the_draft_consistently) {
    AppState app = makeApp();
    app.setWildMode(WildMode::Jokers);
    CHECK_EQ(app.draft.jokers, 1);  // jokers wild needs jokers in the deck
    app.setHandTypeEnabled(HandType::FiveOfAKind, true);
    CHECK(app.draft.isEnabled(HandType::FiveOfAKind));
    CHECK(app.draft.order.back() == HandType::FiveOfAKind);
    app.setWildMode(WildMode::None);
    CHECK(!app.draft.isEnabled(HandType::FiveOfAKind));
    app.setWildMode(WildMode::Deuces);
    app.applyDraft();
    CHECK(app.applied.wild == WildMode::Deuces);
    CHECK_EQ(popcount(app.evaluator->wildMask()), 4);
}

PF_TEST(app_import_and_export_rules) {
    AppState app = makeApp();
    CHECK(app.importRules(std::string(PF_RULES_DIR) + "/short_deck_36.json"));
    CHECK_EQ(app.draft.deckSize, 36);
    CHECK(app.draftDirty());
    CHECK_EQ(app.applied.deckSize, 52);  // not applied until asked
    CHECK(!app.importRules(std::string(PF_RULES_DIR) + "/does_not_exist.json"));
    CHECK_EQ(app.draft.deckSize, 36);    // a failed import leaves the draft alone

    const auto out = std::filesystem::temp_directory_path() / "pokerforge_test_export.json";
    CHECK(app.exportDraft(out.string()));
    AppState other = makeApp();
    CHECK(other.importRules(out.string()));
    CHECK(other.draft == app.draft);
    std::filesystem::remove(out);

    const auto bad = std::filesystem::temp_directory_path() / "pokerforge_test_bad.json";
    { std::ofstream(bad) << "{ not json"; }
    CHECK(!other.importRules(bad.string()));
    std::filesystem::remove(bad);
}

PF_TEST(app_table_changes_restart_the_simulation) {
    AppState app = makeApp();
    CHECK(pump(app, 1000));
    const auto epoch = app.snap.epoch;

    app.toggleFold(1);
    app.frame();
    CHECK(app.snap.epoch > epoch);
    CHECK(app.table.seat(1).folded);
    CHECK(pump(app, 1000));
    CHECK(app.snap.lose(1) == 0.0);  // a folded seat has no equity

    const auto e2 = app.snap.epoch;
    app.nextStreet();
    app.frame();
    CHECK(app.snap.epoch > e2);
    CHECK_EQ(app.table.board().size(), 3u);
    CHECK(pump(app, 1000));
    CHECK_EQ(app.table.equityHistory().front().street == Street::Preflop, true);

    app.setSeatCount(2);
    CHECK_EQ(app.table.seatCount(), 2);
    CHECK(pump(app, 1000));
    CHECK_EQ(app.snap.seats.size(), 2u);
}

PF_TEST(app_equities_always_sum_to_one) {
    AppState app = makeApp();
    app.setSeatCount(5);
    app.toggleFold(3);
    for (int street = 0; street < 4; ++street) {
        CHECK(pump(app, 3000));
        double sum = 0;
        for (int i = 0; i < app.table.seatCount(); ++i)
            if (!app.table.seat(i).folded) sum += app.snap.equity(i);
        CHECK(std::abs(sum - 1.0) < 1e-9);
        for (int i = 0; i < app.table.seatCount(); ++i)
            if (app.table.seat(i).folded) CHECK_EQ(app.snap.equity(i), 0.0);
        if (street < 3) app.nextStreet();
    }
    // The river turns every hand over, so the result is certain: 100% split among the winners.
    CHECK(app.table.street() == Street::River);
    CHECK(!app.table.showdownWinners().empty());
    for (int w : app.table.showdownWinners()) CHECK(app.snap.equity(w) > 0.0);
}

PF_TEST(app_hero_known_seats_and_analysis) {
    AppState app = makeApp();
    app.setHero(2);
    CHECK_EQ(app.heroSeat(), 2);
    CHECK(app.table.seat(2).known);
    app.setSeatKnown(1, true);
    app.frame();
    CHECK(app.seatViews[1].visible);
    app.nextStreet();
    app.frame();
    CHECK(app.heroAnalysis.valid);
    CHECK(!app.heroAnalysis.rows.empty());
    app.setSeatKnown(1, false);
    app.frame();
    CHECK(!app.seatViews[1].visible);
}

PF_TEST(app_settings_reach_the_engine) {
    AppState app = makeApp();
    app.setPaused(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    app.frame();
    CHECK(app.snap.paused);
    const auto runs = app.snap.runs;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    app.frame();
    CHECK_EQ(app.snap.runs, runs);
    app.setPaused(false);
    CHECK(pump(app, runs + 1000));

    app.setSeed(true, 77);  // a fixed seed is only used for the next scenario; exact replay is tested in test_simulator
    CHECK(app.fixedSeed);
    CHECK_EQ(app.seedValue, 77u);
    app.restart();
    CHECK(pump(app, 5000));

    app.setAutoStopPercent(1.0);
    app.restart();
    CHECK(waitFor([&] { app.frame(); return app.snap.autoStopped; }, 30));
    CHECK(app.snap.equityCI95(app.heroSeat()) <= 0.01);
}

PF_TEST(app_benchmark_runs_and_restores_pause) {
    AppState app = makeApp();
    app.setPaused(true);
    app.startBenchmark();
    CHECK(app.benchmarkRunning());
    CHECK(waitFor([&] { app.frame(); return app.haveBenchmark; }, 60));
    CHECK(!app.benchmarkRunning());
    CHECK(app.lastBenchmark.evalsPerSecSingle > 0);
    CHECK(!app.lastBenchmark.scaling.empty());
    CHECK(app.paused);  // the state from before the benchmark is back
}
