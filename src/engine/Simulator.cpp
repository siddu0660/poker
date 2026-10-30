#include "engine/Simulator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "core/Rng.hpp"
#include "util/Log.hpp"

namespace pf {

using Clock = std::chrono::steady_clock;

std::string validateScenario(const Scenario& s) {
    if (!s.evaluator) return "No rules loaded";
    if (s.seats.empty() || s.seats.size() > kMaxSeats)
        return "Seat count must be 1 to " + std::to_string(kMaxSeats);
    if (s.board.size() > 5) return "Board has more than 5 cards";

    const auto deck = deckFor(s.evaluator->rules());
    CardMask deckMask = 0;
    for (CardId c : deck) deckMask |= bit(c);

    CardMask seen = 0;
    auto place = [&](CardId c, const std::string& where) -> std::string {
        if (c >= 64 || !(deckMask & bit(c))) return cardName(c) + " (" + where + ") is not in this deck";
        if (seen & bit(c)) return cardName(c) + " (" + where + ") is used twice";
        seen |= bit(c);
        return {};
    };
    for (CardId c : s.board)
        if (auto e = place(c, "board"); !e.empty()) return e;

    int active = 0, randomCards = 0;
    for (std::size_t i = 0; i < s.seats.size(); ++i) {
        const Seat& seat = s.seats[i];
        active += seat.active;
        for (CardId c : seat.hole) {
            if (c == kNoCard) { randomCards += seat.active; continue; }
            if (auto e = place(c, "seat " + std::to_string(i + 1)); !e.empty()) return e;
        }
    }
    if (active == 0) return "No active seats";
    const int needed = static_cast<int>(5 - s.board.size()) + randomCards;
    if (needed > popcount(deckMask & ~seen)) return "Not enough cards left in the deck to deal";
    return {};
}

void Tally::merge(const Tally& o) {
    runs += o.runs;
    for (std::size_t i = 0; i < seats.size(); ++i) {
        SeatTally& a = seats[i];
        const SeatTally& b = o.seats[i];
        a.wins += b.wins;
        a.ties += b.ties;
        a.equity += b.equity;
        a.equitySq += b.equitySq;
        for (std::size_t h = 0; h < a.hands.size(); ++h) a.hands[h] += b.hands[h];
    }
}

int hardwareThreads() {
#ifdef _OPENMP
    return std::max(1, omp_get_max_threads());
#else
    return 1;
#endif
}

namespace {

// The scenario flattened for the hot loop.
struct Prepared {
    const Evaluator* ev = nullptr;
    std::vector<CardId> pool;  // cards that can still be dealt
    CardMask board = 0;
    int boardMissing = 0;
    int seatCount = 0;                       // active seats
    std::array<int, kMaxSeats> seatIndex{};  // tally slot for each active seat
    std::array<CardMask, kMaxSeats> known{};
    std::array<int, kMaxSeats> unknown{};
    int deal = 0;  // cards drawn per run
    std::uint64_t seed = 0;
};

Prepared prepare(const Scenario& s) {
    Prepared p;
    p.ev = s.evaluator.get();
    p.seed = s.seed;
    CardMask used = 0;
    for (CardId c : s.board) { p.board |= bit(c); used |= bit(c); }
    p.boardMissing = 5 - static_cast<int>(s.board.size());
    for (std::size_t i = 0; i < s.seats.size(); ++i) {
        const Seat& seat = s.seats[i];
        CardMask known = 0;
        int unknown = 0;
        for (CardId c : seat.hole) {
            if (c == kNoCard) ++unknown;
            else { known |= bit(c); used |= bit(c); }  // folded known cards are dead too
        }
        if (!seat.active) continue;
        const auto k = static_cast<std::size_t>(p.seatCount++);
        p.seatIndex[k] = static_cast<int>(i);
        p.known[k] = known;
        p.unknown[k] = unknown;
        p.deal += unknown;
    }
    p.deal += p.boardMissing;
    for (CardId c : deckFor(s.evaluator->rules()))
        if (!(used & bit(c))) p.pool.push_back(c);
    return p;
}

// Scores one complete deal: `drawn[k]` are the random hole cards dealt to
// active seat k. Monte Carlo and exact enumeration both go through here, so
// they cannot disagree about who wins or how a split is counted.
inline void scoreDeal(const Prepared& p, CardMask board, const std::array<CardMask, kMaxSeats>& drawn, Tally& t) {
    std::array<HandScore, kMaxSeats> score{};
    HandScore best = 0;
    int winners = 0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(p.seatCount); ++k) {
        const HandScore s = p.ev->evaluate(board | p.known[k] | drawn[k]);
        score[k] = s;
        if (s > best) { best = s; winners = 1; }
        else if (s == best) ++winners;
    }
    const std::uint64_t share = static_cast<std::uint64_t>(kEquityUnits / winners);
    for (std::size_t k = 0; k < static_cast<std::size_t>(p.seatCount); ++k) {
        SeatTally& st = t.seats[static_cast<std::size_t>(p.seatIndex[k])];
        ++st.hands[static_cast<std::size_t>(scoreCategory(score[k]))];
        if (score[k] != best) continue;
        if (winners == 1) ++st.wins;
        else ++st.ties;
        st.equity += share;
        st.equitySq += share * share;
    }
    ++t.runs;
}

void runChunk(const Prepared& p, std::uint64_t chunk, std::vector<CardId>& pool, Tally& t) {
    // Each chunk starts from the same pool order and its own RNG stream, so
    // its result is independent of which thread runs it or what ran before.
    std::copy(p.pool.begin(), p.pool.end(), pool.begin());
    Rng rng(p.seed, chunk);
    const auto poolSize = static_cast<std::uint32_t>(pool.size());
    std::array<CardMask, kMaxSeats> drawn{};

    for (int run = 0; run < kChunkRuns; ++run) {
        for (int i = 0; i < p.deal; ++i) {  // partial Fisher-Yates
            const auto ui = static_cast<std::uint32_t>(i);
            std::swap(pool[ui], pool[ui + rng.below(poolSize - ui)]);
        }
        std::size_t d = 0;
        CardMask board = p.board;
        for (int i = 0; i < p.boardMissing; ++i) board |= bit(pool[d++]);
        for (std::size_t k = 0; k < static_cast<std::size_t>(p.seatCount); ++k) {
            drawn[k] = 0;
            for (int i = 0; i < p.unknown[k]; ++i) drawn[k] |= bit(pool[d++]);
        }
        scoreDeal(p, board, drawn, t);
    }
}

// ---- exact enumeration

// What is dealt, in order: each active seat's unknown hole cards, then the rest
// of the board. Seats are distinct, so a deal is one choice per stage.
struct Stage {
    int seat;   // active-seat slot, or -1 for the board
    int count;  // cards chosen at this stage
};

std::vector<Stage> stagesOf(const Prepared& p) {
    std::vector<Stage> out;
    for (int k = 0; k < p.seatCount; ++k)
        if (p.unknown[static_cast<std::size_t>(k)] > 0) out.push_back({k, p.unknown[static_cast<std::size_t>(k)]});
    if (p.boardMissing > 0) out.push_back({-1, p.boardMissing});
    return out;
}

// n choose k, or 0 when it exceeds `cap` (the caller only needs to know "too many").
std::uint64_t chooseCapped(std::uint64_t n, int k, std::uint64_t cap) {
    if (k < 0 || static_cast<std::uint64_t>(k) > n) return 0;
    std::uint64_t r = 1;
    for (int i = 1; i <= k; ++i) {
        r = r * (n - static_cast<std::uint64_t>(k) + static_cast<std::uint64_t>(i)) / static_cast<std::uint64_t>(i);
        if (r > cap) return 0;
    }
    return r;
}

// Calls f(mask) for every k-card subset of `cards`.
template <class F>
void forEachSubset(const std::vector<CardId>& cards, int k, F&& f) {
    const int n = static_cast<int>(cards.size());
    if (k == 0) { f(CardMask{0}); return; }
    if (k > n) return;
    std::array<int, 5> idx{};
    for (int i = 0; i < k; ++i) idx[static_cast<std::size_t>(i)] = i;
    while (true) {
        CardMask m = 0;
        for (int i = 0; i < k; ++i) m |= bit(cards[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])]);
        f(m);
        int i = k - 1;
        while (i >= 0 && idx[static_cast<std::size_t>(i)] == n - k + i) --i;
        if (i < 0) return;
        ++idx[static_cast<std::size_t>(i)];
        for (int j = i + 1; j < k; ++j) idx[static_cast<std::size_t>(j)] = idx[static_cast<std::size_t>(j - 1)] + 1;
    }
}

struct Enumerator {
    const Prepared& p;
    const std::vector<Stage>& stages;
    Tally& tally;
    std::array<CardMask, kMaxSeats> drawn{};
    CardMask board = p.board;  // the revealed cards; a board stage, if any, adds the rest

    void run(std::size_t stage, CardMask used) {
        if (stage == stages.size()) {
            scoreDeal(p, board, drawn, tally);
            return;
        }
        std::vector<CardId> left;
        left.reserve(p.pool.size());
        for (CardId c : p.pool)
            if (!(used & bit(c))) left.push_back(c);
        forEachSubset(left, stages[stage].count, [&](CardMask m) {
            assign(stage, m);
            run(stage + 1, used | m);
        });
    }

    void assign(std::size_t stage, CardMask m) {
        if (stages[stage].seat < 0) board = p.board | m;
        else drawn[static_cast<std::size_t>(stages[stage].seat)] = m;
    }
};

}  // namespace

std::optional<std::uint64_t> countDeals(const Scenario& s, std::uint64_t limit) {
    if (!validateScenario(s).empty()) return std::nullopt;
    const Prepared p = prepare(s);
    std::uint64_t total = 1;
    std::uint64_t left = p.pool.size();
    for (const Stage& st : stagesOf(p)) {
        const std::uint64_t ways = chooseCapped(left, st.count, limit);
        if (ways == 0 || total > limit / ways) return std::nullopt;
        total *= ways;
        left -= static_cast<std::uint64_t>(st.count);
    }
    return total;
}

Tally enumerateExact(const Scenario& s, int threads, const std::atomic<bool>* cancel) {
    const Prepared p = prepare(s);
    const std::vector<Stage> stages = stagesOf(p);
    if (stages.empty()) {  // everything is known: a single deal
        Tally t;
        scoreDeal(p, p.board, std::array<CardMask, kMaxSeats>{}, t);
        return t;
    }

    // Split the work on the first stage's choices; each thread walks the rest.
    std::vector<CardMask> first;
    forEachSubset(p.pool, stages[0].count, [&](CardMask m) { first.push_back(m); });
    if (threads <= 0) threads = hardwareThreads();
    threads = std::clamp(threads, 1, std::max(1, static_cast<int>(first.size())));
    std::vector<Tally> local(static_cast<std::size_t>(threads));
    const int n = static_cast<int>(first.size());

    // OpenMP loops cannot break early, so a cancelled run skips its remaining items instead.
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
#ifdef _OPENMP
#pragma omp parallel for num_threads(threads) schedule(dynamic, 1)
    for (int i = 0; i < n; ++i) {
        if (cancelled()) continue;
        Enumerator e{p, stages, local[static_cast<std::size_t>(omp_get_thread_num())]};
        e.assign(0, first[static_cast<std::size_t>(i)]);
        e.run(1, first[static_cast<std::size_t>(i)]);
    }
#else
    for (int i = 0; i < n && !cancelled(); ++i) {
        Enumerator e{p, stages, local[0]};
        e.assign(0, first[static_cast<std::size_t>(i)]);
        e.run(1, first[static_cast<std::size_t>(i)]);
    }
#endif

    Tally total;
    for (const Tally& t : local) total.merge(t);
    return total;
}

Tally runChunks(const Scenario& s, std::uint64_t firstChunk, int chunks, int threads) {
    const Prepared p = prepare(s);
    if (threads <= 0) threads = hardwareThreads();
    threads = std::clamp(threads, 1, std::max(1, chunks));
    std::vector<Tally> local(static_cast<std::size_t>(threads));

#ifdef _OPENMP
#pragma omp parallel num_threads(threads)
    {
        Tally& mine = local[static_cast<std::size_t>(omp_get_thread_num())];
        std::vector<CardId> pool(p.pool.size());
#pragma omp for schedule(dynamic, 1)
        for (int c = 0; c < chunks; ++c) runChunk(p, firstChunk + static_cast<std::uint64_t>(c), pool, mine);
    }
#else
    std::vector<CardId> pool(p.pool.size());
    for (int c = 0; c < chunks; ++c) runChunk(p, firstChunk + static_cast<std::uint64_t>(c), pool, local[0]);
#endif

    Tally total;
    for (const Tally& t : local) total.merge(t);
    return total;
}

// ---------------------------------------------------------------- Snapshot

namespace {
double ratio(std::uint64_t a, std::uint64_t b) { return b ? static_cast<double>(a) / static_cast<double>(b) : 0.0; }
}  // namespace

double Snapshot::win(int s) const { return ratio(seats[static_cast<std::size_t>(s)].wins, runs); }
double Snapshot::split(int s) const { return ratio(seats[static_cast<std::size_t>(s)].ties, runs); }
double Snapshot::lose(int s) const {
    if (!runs || !active[static_cast<std::size_t>(s)]) return 0.0;
    return 1.0 - win(s) - split(s);
}
double Snapshot::equity(int s) const {
    return ratio(seats[static_cast<std::size_t>(s)].equity, runs) / kEquityUnits;
}
double Snapshot::winCI95(int s) const {
    if (exact) return 0.0;
    if (!runs) return 1.0;
    const double p = win(s);
    return 1.96 * std::sqrt(p * (1.0 - p) / static_cast<double>(runs));
}
double Snapshot::equityCI95(int s) const {
    if (exact) return 0.0;
    if (runs < 2) return 1.0;
    const auto n = static_cast<double>(runs);
    const auto& st = seats[static_cast<std::size_t>(s)];
    const double mean = static_cast<double>(st.equity) / n / kEquityUnits;
    const double meanSq = static_cast<double>(st.equitySq) / n / (kEquityUnits * kEquityUnits);
    const double var = std::max(0.0, meanSq - mean * mean) * n / (n - 1.0);
    return 1.96 * std::sqrt(var / n);
}
double Snapshot::handShare(int s, HandType t) const {
    return ratio(seats[static_cast<std::size_t>(s)].hands[static_cast<std::size_t>(t)], runs);
}

// ---------------------------------------------------------------- SimEngine

SimEngine::SimEngine() { worker_ = std::thread([this] { loop(); }); }

SimEngine::~SimEngine() {
    {
        std::lock_guard lock(mu_);
        stop_ = true;
        cancelExact_.store(true, std::memory_order_relaxed);
        ++wake_;
    }
    cv_.notify_all();
    worker_.join();
}

void SimEngine::setScenario(Scenario s) {
    std::string err = validateScenario(s);
    {
        std::lock_guard lock(mu_);
        resetLocked();
        error_ = err;
        scenario_ = err.empty() ? std::make_shared<const Scenario>(std::move(s)) : nullptr;
    }
    if (!err.empty()) logError("[Simulation] " + err);
    cv_.notify_all();
}

void SimEngine::resetLocked() {
    ++epoch_;
    ++wake_;
    totals_ = Tally{};
    nextChunk_ = 0;
    autoStopped_ = false;
    exactChecked_ = exactDone_ = computingExact_ = false;
    cancelExact_.store(true, std::memory_order_relaxed);  // stop an enumeration for the old scenario
    exactDeals_ = 0;
    lastBatchMs_ = 0;
    lastBatchRuns_ = 0;
    runsPerSec_ = 0;
    epochStart_ = windowStart_ = Clock::now();
    windowRuns_ = 0;
    windowComputeMs_ = 0;
}

void SimEngine::setExactMode(bool allowed, std::uint64_t maxDeals) {
    {
        std::lock_guard lock(mu_);
        if (allowed == exactAllowed_ && maxDeals == exactLimit_) return;
        exactAllowed_ = allowed;
        exactLimit_ = maxDeals;
        resetLocked();  // the same scenario, started again under the new mode
    }
    cv_.notify_all();
}

void SimEngine::setTargetRate(double r) {
    { std::lock_guard lock(mu_); targetRate_ = r; ++wake_; }
    cv_.notify_all();
}

void SimEngine::setThreads(int n) {
    { std::lock_guard lock(mu_); threads_ = n; ++wake_; }
    cv_.notify_all();
}

void SimEngine::setPaused(bool p) {
    {
        std::lock_guard lock(mu_);
        paused_ = p;
        if (!p) autoStopped_ = false;
        ++wake_;
    }
    cv_.notify_all();
}

void SimEngine::setAutoStop(int heroSeat, double halfWidth) {
    {
        std::lock_guard lock(mu_);
        heroSeat_ = heroSeat;
        autoStopHalfWidth_ = halfWidth;
        if (halfWidth <= 0) autoStopped_ = false;
        ++wake_;
    }
    cv_.notify_all();
}

Snapshot SimEngine::snapshot() const {
    std::lock_guard lock(mu_);
    Snapshot s;
    s.epoch = epoch_;
    s.runs = totals_.runs;
    const std::size_t n = scenario_ ? scenario_->seats.size() : 0;
    s.seats.assign(totals_.seats.begin(), totals_.seats.begin() + static_cast<std::ptrdiff_t>(n));
    for (std::size_t i = 0; i < n; ++i) s.active.push_back(scenario_->seats[i].active);
    s.runsPerSec = runsPerSec_;
    s.lastBatchMs = lastBatchMs_;
    s.lastBatchRuns = lastBatchRuns_;
    s.paused = paused_;
    s.autoStopped = autoStopped_;
    s.exact = exactDone_;
    s.computingExact = computingExact_;
    s.exactDeals = exactDeals_;
    s.error = error_;
    return s;
}

int SimEngine::chooseChunks() const {
    // Aim for ~50 ms batches so the UI sees steady updates and settings react quickly.
    const double rate = targetRate_ > 0 ? targetRate_ : computeRate_;
    if (rate <= 0) return 4;
    const double chunks = std::round(rate * 0.05 / kChunkRuns);
    return static_cast<int>(std::clamp(chunks, 1.0, 65536.0));
}

void SimEngine::loop() {
    std::unique_lock lock(mu_);
    while (!stop_) {
        if (!scenario_ || paused_ || autoStopped_ || exactDone_) {
            cv_.wait(lock);
            continue;
        }

        // Once per scenario: if every deal can be enumerated quickly, do that instead of sampling.
        if (exactAllowed_ && !exactChecked_) {
            exactChecked_ = true;
            const auto scenario = scenario_;
            const std::uint64_t epoch = epoch_;
            const std::uint64_t limit = exactLimit_;
            const int threads = threads_;
            lock.unlock();
            const auto deals = countDeals(*scenario, limit);
            lock.lock();
            if (!deals || epoch != epoch_) continue;  // too many deals: sample instead

            computingExact_ = true;
            cancelExact_.store(false, std::memory_order_relaxed);
            lock.unlock();
            const auto t0 = Clock::now();
            Tally exact = enumerateExact(*scenario, threads, &cancelExact_);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            lock.lock();
            if (epoch != epoch_) continue;  // the scenario changed meanwhile
            computingExact_ = false;
            totals_ = std::move(exact);
            exactDone_ = true;
            exactDeals_ = *deals;
            lastBatchMs_ = ms;
            lastBatchRuns_ = totals_.runs;
            runsPerSec_ = ms > 0 ? static_cast<double>(totals_.runs) / (ms / 1000.0) : 0.0;
            char line[160];
            std::snprintf(line, sizeof line, "[Simulation] Exact: all %s deals evaluated in %.0f ms",
                          withCommas(totals_.runs).c_str(), ms);
            Log::instance().add(LogLevel::Sim, line);
            continue;
        }

        const auto scenario = scenario_;
        const std::uint64_t epoch = epoch_;
        const std::uint64_t first = nextChunk_;
        const int chunks = chooseChunks();
        const int threads = threads_;
        lock.unlock();

        const auto t0 = Clock::now();
        const Tally batch = runChunks(*scenario, first, chunks, threads);
        const auto t1 = Clock::now();

        lock.lock();
        if (epoch != epoch_) continue;  // scenario changed mid-batch: drop it

        totals_.merge(batch);
        nextChunk_ += static_cast<std::uint64_t>(chunks);
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        lastBatchMs_ = ms;
        lastBatchRuns_ = batch.runs;
        if (ms > 0) computeRate_ = static_cast<double>(batch.runs) / (ms / 1000.0);
        windowRuns_ += batch.runs;
        windowComputeMs_ += ms;

        const double sinceWindow = std::chrono::duration<double>(t1 - windowStart_).count();
        if (sinceWindow >= 1.0) {
            runsPerSec_ = static_cast<double>(windowRuns_) / sinceWindow;
            char line[160];
            std::snprintf(line, sizeof line, "[Simulation] %s runs completed in %.0f ms (%s runs/s)",
                          withCommas(windowRuns_).c_str(), windowComputeMs_, shortCount(runsPerSec_).c_str());
            Log::instance().add(LogLevel::Sim, line);
            windowStart_ = t1;
            windowRuns_ = 0;
            windowComputeMs_ = 0;
        } else if (sinceWindow >= 0.1 || runsPerSec_ == 0) {  // a running estimate until the first full second is in
            runsPerSec_ = static_cast<double>(windowRuns_) / sinceWindow;
        }

        if (autoStopHalfWidth_ > 0 && heroSeat_ >= 0 &&
            heroSeat_ < static_cast<int>(scenario->seats.size()) &&
            scenario->seats[static_cast<std::size_t>(heroSeat_)].active && totals_.runs >= 1000) {
            Snapshot snap;
            snap.runs = totals_.runs;
            snap.seats.assign(totals_.seats.begin(), totals_.seats.end());
            const double hw = snap.equityCI95(heroSeat_);
            if (hw <= autoStopHalfWidth_) {
                autoStopped_ = true;
                char line[160];
                std::snprintf(line, sizeof line, "[Simulation] Auto-stopped at %s runs: equity +-%.2f%%",
                              withCommas(totals_.runs).c_str(), hw * 100.0);
                Log::instance().add(LogLevel::Sim, line);
                continue;
            }
        }

        if (targetRate_ > 0) {  // hold the target rate
            const auto deadline =
                t0 + std::chrono::duration_cast<Clock::duration>(
                         std::chrono::duration<double>(static_cast<double>(batch.runs) / targetRate_));
            const std::uint64_t wake = wake_;
            cv_.wait_until(lock, deadline, [&] { return stop_ || wake_ != wake; });
        }
    }
}

// ---------------------------------------------------------------- formatting

std::string withCommas(std::uint64_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

std::string shortCount(double n) {
    char buf[32];
    if (n >= 1e9) std::snprintf(buf, sizeof buf, "%.1fG", n / 1e9);
    else if (n >= 1e6) std::snprintf(buf, sizeof buf, "%.1fM", n / 1e6);
    else if (n >= 1e3) std::snprintf(buf, sizeof buf, "%.0fk", n / 1e3);
    else std::snprintf(buf, sizeof buf, "%.0f", n);
    return buf;
}

}  // namespace pf
