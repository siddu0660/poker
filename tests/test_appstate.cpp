#include "test_main.hpp"
#include "ui/AppState.hpp"
#include "ui/Export.hpp"
#include "util/Paths.hpp"

#include <algorithm>
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

// Test clean-up must never fail a test, so use the overload that reports instead of throwing.
void removeQuietly(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

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
    removeQuietly(out);

    const auto bad = std::filesystem::temp_directory_path() / "pokerforge_test_bad.json";
    { std::ofstream(bad) << "{ not json"; }
    CHECK(!other.importRules(bad.string()));
    removeQuietly(bad);
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

PF_TEST(app_equity_csv_export) {
    AppState app = makeApp();
    app.setSeatCount(3);
    app.toggleFold(2);
    CHECK(pump(app, 1000));
    app.nextStreet();  // files the preflop equity
    CHECK(pump(app, 1000));

    const std::string csv = equityCsv(app.table);
    CHECK(csv.rfind("hand,street,Player 1,Player 2,Player 3\n", 0) == 0);
    CHECK(csv.find(",Preflop,") != std::string::npos);
    CHECK(csv.find(",Flop,") != std::string::npos);
    // One header and one row per street; the folded seat's cell is empty.
    std::size_t lines = 0;
    for (char c : csv) lines += c == '\n';
    CHECK_EQ(lines, 1 + app.table.equityHistory().size());
    CHECK(csv.find("Preflop,") != std::string::npos);
    const auto row = csv.substr(csv.find("Preflop"));
    CHECK(row.substr(0, row.find('\n')).back() == ',');  // trailing empty cell for Player 3
    // Equities in a row add up to 100.
    double sum = 0;
    std::size_t pos = row.find(',');
    for (int cell = 0; cell < 2; ++cell) {
        sum += std::stod(row.substr(pos + 1));
        pos = row.find(',', pos + 1);
    }
    CHECK(std::abs(sum - 100.0) < 0.001);

    const auto path = std::filesystem::temp_directory_path() / "pokerforge_test_csv" / "equity.csv";
    CHECK(app.exportEquityCsv(path.string()));
    {
        std::ifstream in(path);  // closed before cleanup: Windows cannot delete an open file
        std::string firstLine;
        std::getline(in, firstLine);
        CHECK_EQ(firstLine, std::string("hand,street,Player 1,Player 2,Player 3"));
    }
    removeQuietly(path.parent_path());

    AppState empty = makeApp();
    empty.newHand();  // history is cleared
    CHECK(!empty.exportEquityCsv((std::filesystem::temp_directory_path() / "should_not_exist.csv").string()));
    CHECK(!std::filesystem::exists(std::filesystem::temp_directory_path() / "should_not_exist.csv"));
}

PF_TEST(app_csv_cells_are_escaped) {
    CHECK_EQ(csvCell("Player 1"), std::string("Player 1"));
    CHECK_EQ(csvCell("Smith, John"), std::string("\"Smith, John\""));
    CHECK_EQ(csvCell("say \"hi\""), std::string("\"say \"\"hi\"\"\""));
    CHECK_EQ(csvCell("two\nlines"), std::string("\"two\nlines\""));
}

PF_TEST(app_dropped_files) {
    AppState app = makeApp();
    CHECK(!app.handleDroppedFile((std::filesystem::path(PF_RULES_DIR) / "texas_holdem.txt").string()));
    CHECK(!app.draftDirty());
    app.visible.rules = false;
    CHECK(app.handleDroppedFile(std::string(PF_RULES_DIR) + "/short_deck_36.json"));
    CHECK_EQ(app.draft.deckSize, 36);
    CHECK(app.visible.rules);
    CHECK(app.draftDirty());  // imported into the draft, not applied
    CHECK(!app.handleDroppedFile(std::string(PF_RULES_DIR) + "/missing.json"));

    const auto upper = std::filesystem::temp_directory_path() / "DROPPED.JSON";
    CHECK(app.exportDraft(upper.string()));
    AppState other = makeApp();
    CHECK(other.handleDroppedFile(upper.string()));  // the extension check ignores case
    removeQuietly(upper);
}

PF_TEST(app_default_export_paths) {
    AppState app = makeApp();
    app.draft.name = "Short Deck (6+)";
    CHECK(std::filesystem::path(app.defaultRulesExportPath()).filename() == "short_deck_6.json");
    app.draft.name = "!!!";
    CHECK(std::filesystem::path(app.defaultRulesExportPath()).filename() == "rules.json");
    CHECK(std::filesystem::path(app.defaultCsvPath()).filename().string().rfind("equity_hand_", 0) == 0);
    app.exportDir = "somewhere";
    CHECK(std::filesystem::path(app.defaultCsvPath()).parent_path() == "somewhere");
}

PF_TEST(settings_round_trip_and_defaults) {
    UserSettings s;
    s.theme = Theme::FlatModern;
    s.runsPerSec = 250000;
    s.unlimited = true;
    s.threads = 3;
    s.fixedSeed = true;
    s.seed = 18446744073709551615ull;
    s.autoStopPercent = 0.5;
    CHECK(settingsFromJson(settingsToJson(s)) == s);
    CHECK(settingsFromJson("{}") == UserSettings{});

    std::vector<std::string> warnings;
    CHECK(settingsFromJson("not json", &warnings) == UserSettings{});
    CHECK(!warnings.empty());
    CHECK(loadSettings("/definitely/not/a/real/path.json") == UserSettings{});  // first run
}

PF_TEST(settings_bad_values_are_clamped_or_ignored) {
    std::vector<std::string> warnings;
    const auto s = settingsFromJson(R"({"theme":"neon","speed":{"runs_per_sec":5,"unlimited":"yes"},
                                        "threads":-4,"seed":{"fixed":true,"value":-1},"auto_stop_percent":99})",
                                    &warnings);
    CHECK(s.theme == Theme::ClassicFelt);
    CHECK_EQ(s.runsPerSec, 1000.0);        // clamped to the slider's range
    CHECK(!s.unlimited);                   // wrong type: default kept
    CHECK_EQ(s.threads, 0);
    CHECK(s.fixedSeed);
    CHECK_EQ(s.seed, 12345u);              // negative seed ignored
    CHECK_EQ(s.autoStopPercent, 5.0);
    CHECK(warnings.size() >= 3);
}

PF_TEST(settings_save_load_and_apply_to_the_app) {
    const auto path = std::filesystem::temp_directory_path() / "pokerforge_test_settings" / "settings.json";
    UserSettings s;
    s.theme = Theme::FlatModern;
    s.runsPerSec = 40000;
    s.fixedSeed = true;
    s.seed = 99;
    s.threads = 2;
    CHECK(saveSettings(s, path.string()));
    CHECK(loadSettings(path.string()) == s);

    AppState app = makeApp();
    app.applySettings(loadSettings(path.string()));
    CHECK(app.currentSettings() == s);
    CHECK(app.theme == Theme::FlatModern);
    CHECK_EQ(app.targetRate, 40000.0);
    CHECK(app.fixedSeed);
    removeQuietly(path.parent_path());
}

// Every file path goes through UTF-8. On Windows the old narrow conversions used
// the ANSI code page and threw on characters like these; elsewhere this guards the round trip.
PF_TEST(app_paths_with_non_ascii_folders) {
    const std::string dirUtf8 = pathToUtf8(std::filesystem::temp_directory_path() / "pokerforge_\xE6\xB5\x8B\xE8\xAF\x95_\xCE\xA9");
    const auto dir = pathFromUtf8(dirUtf8);
    CHECK(pathToUtf8(dir) == dirUtf8);
    removeQuietly(dir);
    std::filesystem::create_directories(dir);
    for (const char* f : {"texas_holdem.json", "short_deck_36.json"})
        std::filesystem::copy_file(pathFromUtf8(std::string(PF_RULES_DIR) + "/" + f), dir / f);

    AppState app(dirUtf8);
    CHECK_EQ(app.applied.name, std::string("Texas Hold'em"));  // loaded from the non-ASCII folder
    CHECK(std::find(app.presetFiles.begin(), app.presetFiles.end(), "short_deck_36.json") != app.presetFiles.end());
    CHECK(app.importRules(pathToUtf8(dir / "short_deck_36.json")));
    CHECK_EQ(app.draft.deckSize, 36);
    CHECK(app.handleDroppedFile(pathToUtf8(dir / "texas_holdem.json")));

    app.draft.name = "My rules";
    const std::string rulesOut = app.defaultRulesExportPath();
    CHECK(rulesOut.rfind(dirUtf8, 0) == 0);  // still under the same folder, byte for byte
    CHECK(app.exportDraft(rulesOut));
    CHECK(std::filesystem::exists(pathFromUtf8(rulesOut)));

    app.exportDir = pathToUtf8(dir / "exports");
    CHECK(pump(app, 500));
    const std::string csvOut = app.defaultCsvPath();
    CHECK(app.exportEquityCsv(csvOut));
    CHECK(std::filesystem::exists(pathFromUtf8(csvOut)));

    UserSettings s;
    s.theme = Theme::FlatModern;
    CHECK(saveSettings(s, pathToUtf8(dir / "settings.json")));
    CHECK(loadSettings(pathToUtf8(dir / "settings.json")) == s);
    removeQuietly(dir);
}
