// On-demand checks that are too slow for every CI build:
//   pokercore_exhaustive          all C(52,7) hands vs the known category counts
//   pokercore_exhaustive --bench  single-thread throughput on random 7-card hands
//   pokercore_exhaustive --wild   every 5-card deuces-wild hand vs brute force
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Evaluator.hpp"
#include "core/Rng.hpp"
#include "RefEvaluator.hpp"

using namespace pf;
using Clock = std::chrono::steady_clock;

namespace {

int exhaustive() {
    Evaluator ev{RuleSet{}};
    std::array<std::uint64_t, kHandTypeCount> n{};
    std::uint64_t checksum = 0;
    const auto t0 = Clock::now();
    for (int a = 0; a < 52; ++a) {
        const CardMask ma = CardMask{1} << a;
        for (int b = a + 1; b < 52; ++b) {
            const CardMask mb = ma | CardMask{1} << b;
            for (int c = b + 1; c < 52; ++c) {
                const CardMask mc = mb | CardMask{1} << c;
                for (int d = c + 1; d < 52; ++d) {
                    const CardMask md = mc | CardMask{1} << d;
                    for (int e = d + 1; e < 52; ++e) {
                        const CardMask me = md | CardMask{1} << e;
                        for (int f = e + 1; f < 52; ++f) {
                            const CardMask mf = me | CardMask{1} << f;
                            for (int g = f + 1; g < 52; ++g) {
                                const HandScore s = ev.evaluate(mf | CardMask{1} << g);
                                ++n[static_cast<std::size_t>(scoreCategory(s))];
                                checksum += s;
                            }
                        }
                    }
                }
            }
        }
    }
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();

    constexpr std::array<std::uint64_t, 9> expected{
        23294460, 58627800, 31433400, 6461620, 6180020, 4047644, 3473184, 224848, 41584};
    bool ok = true;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const bool match = n[i] == expected[i];
        ok = ok && match;
        total += n[i];
        std::printf("%-16s %10llu  %s\n", std::string(handTypeDisplay(static_cast<HandType>(i))).c_str(),
                    static_cast<unsigned long long>(n[i]), match ? "ok" : "MISMATCH");
    }
    std::printf("total %llu hands in %.2f s (%.1f M/s), checksum %llu\n", static_cast<unsigned long long>(total),
                secs, static_cast<double>(total) / secs / 1e6, static_cast<unsigned long long>(checksum));
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

double benchRules(const char* label, const RuleSet& rules) {
    Evaluator ev(rules);
    Rng rng(99);
    std::vector<CardId> deck;
    for (int s = 0; s < kSuits; ++s)
        for (int k = rules.minRank(); k < kRanks; ++k) deck.push_back(makeCard(s, k));
    std::vector<CardMask> hands(1 << 20);
    for (auto& h : hands) {
        h = 0;
        while (popcount(h) < 7) h |= bit(deck[rng.below(static_cast<std::uint32_t>(deck.size()))]);
    }
    std::uint64_t sink = 0;
    for (CardMask h : hands) sink += ev.evaluate(h);  // warm up
    constexpr int kRounds = 20;
    const auto t0 = Clock::now();
    for (int r = 0; r < kRounds; ++r)
        for (CardMask h : hands) sink += ev.evaluate(h);
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    const double rate = static_cast<double>(hands.size()) * kRounds / secs / 1e6;
    std::printf("%-22s %7.1f M evals/s/core  (sink %llu)\n", label, rate, static_cast<unsigned long long>(sink & 0xFF));
    return rate;
}

int bench() {
    const double standard = benchRules("standard (52)", RuleSet{});
    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    benchRules("short deck (monotone)", sd);
    RuleSet gen;
    gen.order = {HandType::HighCard, HandType::Pair, HandType::ThreeOfAKind, HandType::Straight,
                 HandType::Flush, HandType::FullHouse, HandType::FourOfAKind, HandType::StraightFlush};
    benchRules("two pair off (general)", gen);
    RuleSet deuces;
    deuces.wild = WildMode::Deuces;
    deuces.order.push_back(HandType::FiveOfAKind);
    benchRules("deuces wild (mixed)", deuces);
    const bool ok = standard >= 30.0;
    std::printf("target 30 M/s/core on the standard path: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Every 5-card hand under the shipped Deuces Wild rules, compared with the
// brute-force reference, plus category counts against the published
// deuces-wild video poker frequencies.
int wildExhaustive() {
    RuleSet rules;
    rules.wild = WildMode::Deuces;
    rules.order.push_back(HandType::FiveOfAKind);
    sanitize(rules);
    Evaluator ev(rules);
    std::array<std::uint64_t, kHandTypeCount> n{};
    std::uint64_t mismatches = 0;
    const auto t0 = Clock::now();
    for (int a = 0; a < 52; ++a)
        for (int b = a + 1; b < 52; ++b)
            for (int c = b + 1; c < 52; ++c)
                for (int d = c + 1; d < 52; ++d)
                    for (int e = d + 1; e < 52; ++e) {
                        const CardMask m = CardMask{1} << a | CardMask{1} << b | CardMask{1} << c |
                                           CardMask{1} << d | CardMask{1} << e;
                        const HandScore s = ev.evaluate(m);
                        ++n[static_cast<std::size_t>(scoreCategory(s))];
                        if (s != pftest::refScore(rules, m) && ++mismatches <= 5)
                            std::printf("mismatch on mask %llx\n", static_cast<unsigned long long>(m));
                    }
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();

    // Natural + wild royals and straight flushes share one category here, and
    // four deuces count as five of a kind.
    constexpr std::array<std::uint64_t, kHandTypeCount> expected{
        0, 0, 95040, 355080, 62232, 14472, 12672, 31552, 4 + 480 + 2068, 624 + 48};
    bool ok = mismatches == 0;
    for (std::size_t i = 2; i < expected.size(); ++i) {
        const bool match = n[i] == expected[i];
        ok = ok && match;
        std::printf("%-16s %10llu  %s\n", std::string(handTypeDisplay(static_cast<HandType>(i))).c_str(),
                    static_cast<unsigned long long>(n[i]), match ? "ok" : "MISMATCH");
    }
    std::printf("pair + high card %llu (not checked)\n", static_cast<unsigned long long>(n[0] + n[1]));
    std::printf("%llu reference mismatches, %.1f s\n%s\n", static_cast<unsigned long long>(mismatches), secs,
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) return bench();
    if (argc > 1 && std::strcmp(argv[1], "--wild") == 0) return wildExhaustive();
    return exhaustive();
}
