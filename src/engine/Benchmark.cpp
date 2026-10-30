#include "engine/Benchmark.hpp"

#include <chrono>
#include <cstdio>
#include <memory>

#include "core/Evaluator.hpp"
#include "core/Rng.hpp"
#include "engine/Simulator.hpp"

namespace pf {

using Clock = std::chrono::steady_clock;

namespace {

double seconds(Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count(); }

}  // namespace

BenchmarkResult runBenchmark(const RuleSet& rules, double secondsPerPoint) {
    BenchmarkResult out;
    auto ev = std::make_shared<const Evaluator>(rules);

    // Evaluator alone on pre-generated random hands.
    const auto deck = deckFor(rules);
    Rng rng(12345);
    std::vector<CardMask> hands(1 << 16);
    for (auto& h : hands) {
        h = 0;
        while (popcount(h) < 7) h |= bit(deck[rng.below(static_cast<std::uint32_t>(deck.size()))]);
    }
    std::uint64_t sink = 0, evals = 0;
    const auto t0 = Clock::now();
    do {
        for (CardMask h : hands) sink += ev->evaluate(h);
        evals += hands.size();
    } while (seconds(t0) < secondsPerPoint);
    out.evalsPerSecSingle = static_cast<double>(evals) / seconds(t0);
    if (sink == 1) out.summary = " ";  // keep the loop from being optimised away

    // Full simulation: six random hands, no board.
    Scenario s;
    s.evaluator = ev;
    s.seats.resize(6);
    const int maxThreads = hardwareThreads();
    std::vector<int> counts;
    for (int t = 1; t < maxThreads; t *= 2) counts.push_back(t);
    counts.push_back(maxThreads);
    for (int threads : counts) {
        std::uint64_t runs = 0, chunk = 0;
        const int chunksPerCall = 16 * threads;
        const auto start = Clock::now();
        do {
            runs += runChunks(s, chunk, chunksPerCall, threads).runs;
            chunk += static_cast<std::uint64_t>(chunksPerCall);
        } while (seconds(start) < secondsPerPoint);
        const double rate = static_cast<double>(runs) / seconds(start);
        out.scaling.push_back({threads, rate, rate * 6});
    }

    char line[200];
    const ScalingPoint& top = out.scaling.back();
    std::snprintf(line, sizeof line,
                  "[Engine] Benchmark: evaluator %.1fM evals/s/core; simulation %s runs/s on %d threads (%.1fx vs 1)",
                  out.evalsPerSecSingle / 1e6, shortCount(top.runsPerSec).c_str(), top.threads,
                  top.runsPerSec / out.scaling.front().runsPerSec);
    out.summary = line;
    return out;
}

}  // namespace pf
