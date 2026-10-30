#include "test_main.hpp"
#include "core/Evaluator.hpp"
#include "engine/Benchmark.hpp"
#include "engine/Simulator.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>

using namespace pf;

namespace {

CardId c(const char* s) { return *parseCard(s); }

Scenario headsUp(const RuleSet& rules, std::array<CardId, 2> a, std::array<CardId, 2> b,
                 std::vector<CardId> board = {}) {
    Scenario s;
    s.evaluator = std::make_shared<const Evaluator>(rules);
    s.seats = {Seat{true, a}, Seat{true, b}};
    s.board = std::move(board);
    s.seed = 7;
    return s;
}

Snapshot toSnapshot(const Scenario& s, const Tally& t) {
    Snapshot snap;
    snap.runs = t.runs;
    for (std::size_t i = 0; i < s.seats.size(); ++i) {
        snap.seats.push_back(t.seats[i]);
        snap.active.push_back(s.seats[i].active);
    }
    return snap;
}

// Exact heads-up equity of seat 0 by enumerating every board (no board dealt yet).
double exactHeadsUp(const Scenario& s) {
    const Evaluator& ev = *s.evaluator;
    CardMask a = 0, b = 0;
    for (CardId x : s.seats[0].hole) a |= bit(x);
    for (CardId x : s.seats[1].hole) b |= bit(x);
    std::vector<CardId> rest;
    for (CardId x : deckFor(ev.rules())) if (!((a | b) & bit(x))) rest.push_back(x);
    const int n = static_cast<int>(rest.size());
    double eq = 0, total = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int k = j + 1; k < n; ++k)
                for (int l = k + 1; l < n; ++l)
                    for (int m = l + 1; m < n; ++m) {
                        const CardMask board = bit(rest[static_cast<std::size_t>(i)]) | bit(rest[static_cast<std::size_t>(j)]) |
                                               bit(rest[static_cast<std::size_t>(k)]) | bit(rest[static_cast<std::size_t>(l)]) |
                                               bit(rest[static_cast<std::size_t>(m)]);
                        const HandScore sa = ev.evaluate(board | a), sb = ev.evaluate(board | b);
                        eq += sa > sb ? 1.0 : sa == sb ? 0.5 : 0.0;
                        total += 1;
                    }
    return eq / total;
}

bool waitFor(const std::function<bool()>& cond, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond();
}

void checkEquitySums(const Scenario& s, const Tally& t) {
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < s.seats.size(); ++i) {
        std::uint64_t hands = 0;
        for (auto h : t.seats[i].hands) hands += h;
        CHECK_EQ(hands, s.seats[i].active ? t.runs : 0u);
        total += t.seats[i].equity;
    }
    CHECK_EQ(total, t.runs * kEquityUnits);  // equities of active seats sum to exactly 100%
}

}  // namespace

PF_TEST(sim_deck_sizes) {
    RuleSet r;
    CHECK_EQ(deckFor(r).size(), 52u);
    r.deckSize = 36;
    CHECK_EQ(deckFor(r).size(), 36u);
    r.jokers = 2;
    CHECK_EQ(deckFor(r).size(), 38u);
}

PF_TEST(sim_validation) {
    RuleSet r;
    CHECK(validateScenario(headsUp(r, {c("As"), c("Ah")}, {c("Kd"), c("Kc")})).empty());
    CHECK(!validateScenario(headsUp(r, {c("As"), c("Ah")}, {c("As"), c("Kc")})).empty());  // duplicate
    CHECK(!validateScenario(headsUp(r, {c("As"), c("Ah")}, {c("Kd"), c("Kc")}, {c("Ah")})).empty());
    CHECK(!validateScenario(headsUp(r, {c("As"), c("Ah")}, {c("Kd"), c("Kc")},
                                    {c("2c"), c("3c"), c("4c"), c("5c"), c("6c"), c("7c")})).empty());
    r.deckSize = 36;
    CHECK(!validateScenario(headsUp(r, {c("2s"), c("Ah")}, {c("Kd"), c("Kc")})).empty());  // no deuces
    Scenario none = headsUp(RuleSet{}, {kNoCard, kNoCard}, {kNoCard, kNoCard});
    none.seats[0].active = none.seats[1].active = false;
    CHECK(!validateScenario(none).empty());
    Scenario noRules;
    noRules.seats.resize(2);
    CHECK(!validateScenario(noRules).empty());
}

PF_TEST(sim_all_cards_known_is_exact) {
    RuleSet r;
    auto s = headsUp(r, {c("As"), c("Ah")}, {c("Kd"), c("Kc")}, {c("2c"), c("7d"), c("9h"), c("Js"), c("3s")});
    auto t = runChunks(s, 0, 8, 2);
    auto snap = toSnapshot(s, t);
    CHECK_EQ(snap.win(0), 1.0);
    CHECK_EQ(snap.lose(1), 1.0);
    CHECK_EQ(snap.equityCI95(0), 0.0);

    auto split = headsUp(r, {c("2s"), c("3h")}, {c("4d"), c("5c")}, {c("As"), c("Ks"), c("Qs"), c("Js"), c("Ts")});
    auto ts = runChunks(split, 0, 8, 2);
    auto ss = toSnapshot(split, ts);
    CHECK_EQ(ss.split(0), 1.0);
    CHECK_EQ(ss.equity(0), 0.5);
    CHECK_EQ(ss.equity(1), 0.5);
    checkEquitySums(split, ts);
}

PF_TEST(sim_deterministic_across_threads_and_batches) {
    Scenario s;
    s.evaluator = std::make_shared<const Evaluator>(RuleSet{});
    s.seats.resize(4);
    s.seats[0].hole = {c("Ah"), c("Kh")};
    s.seats[2].active = false;
    s.board = {c("Qh"), c("7c"), c("2h")};
    s.seed = 99;
    const Tally one = runChunks(s, 0, 40, 1);
    CHECK(one == runChunks(s, 0, 40, 4));
    CHECK(one == runChunks(s, 0, 40, 3));
    Tally parts = runChunks(s, 0, 13, 2);
    parts.merge(runChunks(s, 13, 27, 4));
    CHECK(parts == one);
    s.seed = 100;
    CHECK(!(runChunks(s, 0, 40, 4) == one));
    checkEquitySums(s, one);
}

PF_TEST(sim_matches_exact_enumeration) {
    RuleSet r;
    auto s = headsUp(r, {c("As"), c("Ah")}, {c("Kd"), c("Kc")});
    const double exact = exactHeadsUp(s);
    CHECK(std::abs(exact - 0.812555) < 0.00001);  // AsAh vs KdKc: 81.06% win + 0.38% tie / 2
    auto t = runChunks(s, 0, 800, 0);         // 204,800 runs
    auto snap = toSnapshot(s, t);
    CHECK(std::abs(snap.equity(0) - exact) < 4 * snap.equityCI95(0) / 1.96);
    CHECK(std::abs(snap.equity(0) + snap.equity(1) - 1.0) < 1e-12);

    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    auto ss = headsUp(sd, {c("As"), c("Ks")}, {c("Qd"), c("Qc")});
    const double exactSd = exactHeadsUp(ss);
    auto ts = runChunks(ss, 0, 800, 0);
    auto snapSd = toSnapshot(ss, ts);
    CHECK(std::abs(snapSd.equity(0) - exactSd) < 4 * snapSd.equityCI95(0) / 1.96);
}

PF_TEST(sim_random_opponent) {
    auto s = headsUp(RuleSet{}, {c("As"), c("Ah")}, {kNoCard, kNoCard});
    auto snap = toSnapshot(s, runChunks(s, 0, 600, 0));
    CHECK(std::abs(snap.equity(0) - 0.852) < 0.006);  // AA vs a random hand: 85.2%
}

PF_TEST(sim_folded_cards_are_dead) {
    // Seat 1 folded the Th: the hero's royal flush draw is dead.
    Scenario s;
    s.evaluator = std::make_shared<const Evaluator>(RuleSet{});
    s.seats = {Seat{true, {c("Ah"), c("Kh")}}, Seat{false, {c("Th"), c("2d")}}, Seat{true, {kNoCard, kNoCard}}};
    s.board = {c("Qh"), c("Jh"), c("3c"), c("4d")};
    const Tally dead = runChunks(s, 0, 40, 0);
    CHECK_EQ(dead.seats[0].hands[static_cast<std::size_t>(HandType::StraightFlush)], 0u);
    CHECK_EQ(dead.seats[1].equity, 0u);
    checkEquitySums(s, dead);

    s.seats[1].hole = {kNoCard, kNoCard};  // folded, cards unknown: Th can come
    const Tally live = runChunks(s, 0, 40, 0);
    CHECK(live.seats[0].hands[static_cast<std::size_t>(HandType::StraightFlush)] > 0);
}

PF_TEST(sim_wild_and_short_deck_rules) {
    RuleSet r;
    r.wild = WildMode::Jokers;
    r.jokers = 2;
    r.order.push_back(HandType::FiveOfAKind);
    sanitize(r);
    Scenario s;
    s.evaluator = std::make_shared<const Evaluator>(r);
    s.seats.resize(6);
    CHECK(validateScenario(s).empty());
    const Tally t = runChunks(s, 0, 400, 0);
    checkEquitySums(s, t);
    std::uint64_t fives = 0;
    for (const auto& st : t.seats) fives += st.hands[static_cast<std::size_t>(HandType::FiveOfAKind)];
    CHECK(fives > 0);
}

PF_TEST(sim_engine_lifecycle) {
    SimEngine engine;
    engine.setThreads(2);
    auto s = headsUp(RuleSet{}, {c("As"), c("Ah")}, {c("Kd"), c("Kc")});
    engine.setScenario(s);
    CHECK(waitFor([&] { return engine.snapshot().runs >= 50000; }, 20));
    Snapshot a = engine.snapshot();
    CHECK(a.error.empty());
    CHECK(std::abs(a.equity(0) - 0.8126) < 0.02);
    CHECK(a.runsPerSec > 0);

    // New scenario: new epoch, totals restart.
    auto b = headsUp(RuleSet{}, {c("2c"), c("7d")}, {c("Ad"), c("Ac")});
    engine.setScenario(b);
    Snapshot fresh = engine.snapshot();
    CHECK_EQ(fresh.epoch, a.epoch + 1);
    CHECK(fresh.runs < a.runs);
    CHECK(waitFor([&] { return engine.snapshot().runs >= 20000; }, 20));
    CHECK(engine.snapshot().equity(1) > 0.8);

    // Pause holds the totals still.
    engine.setPaused(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // let an in-flight batch land
    const auto held = engine.snapshot().runs;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK_EQ(engine.snapshot().runs, held);
    CHECK(engine.snapshot().paused);
    engine.setPaused(false);
    CHECK(waitFor([&] { return engine.snapshot().runs > held; }, 10));

    // Auto-stop once the hero's equity is known to +-1%.
    engine.setAutoStop(0, 0.01);
    CHECK(waitFor([&] { return engine.snapshot().autoStopped; }, 20));
    const Snapshot stopped = engine.snapshot();
    CHECK(stopped.equityCI95(0) <= 0.01);

    // Invalid scenario reports an error and does not run.
    auto bad = headsUp(RuleSet{}, {c("As"), c("Ah")}, {c("As"), c("Kc")});
    engine.setScenario(bad);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!engine.snapshot().error.empty());
    CHECK_EQ(engine.snapshot().runs, 0u);
}

PF_TEST(sim_engine_rate_limit) {
    SimEngine engine;
    engine.setTargetRate(20000);
    Scenario s;
    s.evaluator = std::make_shared<const Evaluator>(RuleSet{});
    s.seats.resize(3);
    engine.setScenario(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const auto runs = engine.snapshot().runs;
    // ~20k expected; allow one batch of slack either way and slow sanitizer builds
    CHECK(runs >= 4000);
    CHECK(runs <= 26000);
}

PF_TEST(sim_benchmark_runs) {
    auto b = runBenchmark(RuleSet{}, 0.05);
    CHECK(b.evalsPerSecSingle > 0);
    CHECK(!b.scaling.empty());
    CHECK(b.scaling.front().threads == 1);
    CHECK(!b.summary.empty());
}
