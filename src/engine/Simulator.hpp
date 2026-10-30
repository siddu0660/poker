#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/Cards.hpp"
#include "core/Evaluator.hpp"

namespace pf {

constexpr int kMaxSeats = 6;
constexpr int kChunkRuns = 256;    // runs per RNG stream; the unit of determinism
constexpr int kEquityUnits = 60;   // LCM(1..6): a split between m winners is 60/m units each

struct Seat {
    bool active = true;                                // false = folded
    std::array<CardId, 2> hole{kNoCard, kNoCard};      // kNoCard = dealt at random
};

// Everything the simulation needs, immutable once handed to the engine.
struct Scenario {
    std::shared_ptr<const Evaluator> evaluator;
    std::vector<CardId> board;  // revealed cards, 0..5
    std::vector<Seat> seats;    // 1..kMaxSeats
    std::uint64_t seed = 1;
};

// Empty string when the scenario can be simulated, otherwise the reason it can't.
std::string validateScenario(const Scenario& s);

struct SeatTally {
    std::uint64_t wins = 0;      // runs won outright
    std::uint64_t ties = 0;      // runs where the pot was split with this seat
    std::uint64_t equity = 0;    // sum of per-run equity in kEquityUnits
    std::uint64_t equitySq = 0;  // sum of squared per-run equity (for the variance)
    std::array<std::uint64_t, kHandTypeCount> hands{};  // final hand type histogram

    bool operator==(const SeatTally&) const = default;
};

struct alignas(64) Tally {
    std::uint64_t runs = 0;
    std::array<SeatTally, kMaxSeats> seats{};

    void merge(const Tally& o);
    bool operator==(const Tally&) const = default;
};

// Runs chunks [firstChunk, firstChunk + chunks) of the scenario. The result
// depends only on the scenario and the chunk range, never on `threads`.
// threads <= 0 uses every core. The scenario must be valid.
Tally runChunks(const Scenario& s, std::uint64_t firstChunk, int chunks, int threads);

// Exact enumeration: every distinct deal of the unknown hole cards and the rest
// of the board, each counted once. countDeals returns that number, or nullopt
// when it is above `limit` (or the scenario is invalid). enumerateExact gives a
// Tally whose `runs` is that count; it is the same for any thread count.
// `cancel`, if given, is polled while working; once it reads true the result
// is incomplete and should be thrown away.
std::optional<std::uint64_t> countDeals(const Scenario& s, std::uint64_t limit);
Tally enumerateExact(const Scenario& s, int threads, const std::atomic<bool>* cancel = nullptr);

int hardwareThreads();

// What the UI reads: a copy, safe to keep.
struct Snapshot {
    std::uint64_t epoch = 0;
    std::uint64_t runs = 0;
    std::vector<SeatTally> seats;
    std::vector<bool> active;
    double runsPerSec = 0;
    double lastBatchMs = 0;
    std::uint64_t lastBatchRuns = 0;
    bool paused = false;
    bool autoStopped = false;
    bool exact = false;           // every deal was enumerated: the numbers are exact, not estimates
    bool computingExact = false;  // an exact enumeration is in progress
    std::uint64_t exactDeals = 0; // how many deals it covered
    std::string error;

    double win(int seat) const;
    double split(int seat) const;
    double lose(int seat) const;
    double equity(int seat) const;
    double winCI95(int seat) const;     // half-width, Bernoulli (0 when exact)
    double equityCI95(int seat) const;  // half-width, from the sample variance (0 when exact)
    double handShare(int seat, HandType t) const;
};

// Background worker: runs batches on its own thread (OpenMP inside), merges
// them, and publishes snapshots. Batches started before the latest
// setScenario() are dropped via the epoch counter.
class SimEngine {
public:
    SimEngine();
    ~SimEngine();
    SimEngine(const SimEngine&) = delete;
    SimEngine& operator=(const SimEngine&) = delete;

    void setScenario(Scenario s);         // restarts accumulation
    void setTargetRate(double runsPerSec);  // <= 0: unlimited
    void setThreads(int n);                // <= 0: all cores
    void setPaused(bool paused);
    // Pause once the hero's equity CI half-width is at or below `halfWidth`
    // (a fraction, 0.005 = +-0.5%). halfWidth <= 0 turns it off.
    void setAutoStop(int heroSeat, double halfWidth);
    // When a scenario has at most `maxDeals` distinct deals, enumerate them all
    // once (exact) instead of sampling. Restarts the current scenario.
    void setExactMode(bool allowed, std::uint64_t maxDeals = kDefaultExactDeals);
    static constexpr std::uint64_t kDefaultExactDeals = 10'000'000;

    Snapshot snapshot() const;

private:
    void loop();
    int chooseChunks() const;
    void resetLocked();  // new epoch, empty totals

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_ = false;
    std::uint64_t wake_ = 0;  // bumped by every setter to cut throttling sleeps short

    std::shared_ptr<const Scenario> scenario_;
    std::string error_;
    std::uint64_t epoch_ = 0;
    std::uint64_t nextChunk_ = 0;
    Tally totals_;

    double targetRate_ = 0;
    int threads_ = 0;
    bool paused_ = false;
    bool autoStopped_ = false;
    int heroSeat_ = 0;
    double autoStopHalfWidth_ = 0;

    bool exactAllowed_ = true;
    std::uint64_t exactLimit_ = kDefaultExactDeals;
    bool exactChecked_ = false;  // this epoch's scenario has been counted
    bool exactDone_ = false;     // totals_ hold the exact result
    bool computingExact_ = false;
    std::atomic<bool> cancelExact_{false};  // set when the scenario changes under a running enumeration
    std::uint64_t exactDeals_ = 0;

    double computeRate_ = 0;  // runs per second of compute, for batch sizing
    double runsPerSec_ = 0;
    double lastBatchMs_ = 0;
    std::uint64_t lastBatchRuns_ = 0;
    std::chrono::steady_clock::time_point epochStart_{};
    std::chrono::steady_clock::time_point windowStart_{};
    std::uint64_t windowRuns_ = 0;
    double windowComputeMs_ = 0;
};

std::string withCommas(std::uint64_t n);
std::string shortCount(double n);  // 8.3M, 120k, 950

}  // namespace pf
