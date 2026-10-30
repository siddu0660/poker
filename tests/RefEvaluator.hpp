#pragma once
// Brute-force reference evaluator, written directly from the semantics in
// Evaluator.hpp and deliberately sharing no code with it: score every 5-card
// subset category by category and keep the best. Wild cards are tried as every
// rank, with all wilds in one suit (so flushes are reachable) or in a suit of
// their own (never a flush).
#include <algorithm>
#include <array>
#include <bit>
#include <initializer_list>
#include <vector>

#include "core/Cards.hpp"
#include "core/Evaluator.hpp"
#include "core/Rules.hpp"

namespace pftest {

struct RCard { int rank; int suit; };

inline pf::HandScore refScore5(const pf::RuleSet& rules, const RCard* hand, int n) {
    using pf::HandType;
    std::array<int, 13> count{};
    for (int i = 0; i < n; ++i) ++count[static_cast<std::size_t>(hand[i].rank)];

    // A flush is five distinct cards of one suit; a straight is five distinct ranks in a row.
    int distinctRanks = 0;
    for (int c : count) distinctRanks += c > 0;
    bool flush = n == 5 && distinctRanks == 5;
    for (int i = 0; i < n; ++i) flush = flush && hand[i].suit == hand[0].suit;

    int straightHigh = -1;
    if (n == 5 && distinctRanks == 5) {
        int hi = 12, lo = 0;
        while (count[static_cast<std::size_t>(hi)] == 0) --hi;
        while (count[static_cast<std::size_t>(lo)] == 0) ++lo;
        const int m = rules.minRank();
        if (hi - lo == 4 && lo >= m) straightHigh = hi;
        else if (rules.wheelStraight && hi == 12 && count[static_cast<std::size_t>(m)] &&
                 count[static_cast<std::size_t>(m + 1)] && count[static_cast<std::size_t>(m + 2)] &&
                 count[static_cast<std::size_t>(m + 3)])
            straightHigh = m + 3;
    }

    // Ranks held at least k times, highest first (fixed arrays: this runs millions of times).
    struct Ranks { int r[13]; int n = 0; bool empty() const { return n == 0; } };
    auto ranksWithAtLeast = [&](int k) {
        Ranks out;
        for (int r = 12; r >= 0; --r) if (count[static_cast<std::size_t>(r)] >= k) out.r[out.n++] = r;
        return out;
    };
    const Ranks pairs = ranksWithAtLeast(2);
    const Ranks trips = ranksWithAtLeast(3);
    const Ranks quads = ranksWithAtLeast(4);
    const Ranks fives = ranksWithAtLeast(5);

    // defining ranks (with how many cards each uses), then every other card high to low
    auto encode = [&](std::initializer_list<std::pair<int, int>> defining) {
        std::array<int, 13> left = count;
        std::uint32_t tb = 0;
        int pos = 0;
        for (auto [r, k] : defining) {
            left[static_cast<std::size_t>(r)] -= k;
            tb |= static_cast<std::uint32_t>(r + 1) << (16 - 4 * pos++);
        }
        for (int r = 12; r >= 0; --r)
            for (int k = 0; k < left[static_cast<std::size_t>(r)] && pos < 5; ++k)
                tb |= static_cast<std::uint32_t>(r + 1) << (16 - 4 * pos++);
        return tb;
    };

    pf::HandScore best = 0;
    for (std::size_t s = 0; s < rules.order.size(); ++s) {
        const HandType t = rules.order[s];
        bool ok = false;
        std::uint32_t tb = 0;
        switch (t) {
            case HandType::HighCard: ok = true; tb = encode({}); break;
            case HandType::Pair:
                if ((ok = !pairs.empty())) tb = encode({{pairs.r[0], 2}});
                break;
            case HandType::TwoPair:
                if ((ok = pairs.n >= 2)) tb = encode({{pairs.r[0], 2}, {pairs.r[1], 2}});
                break;
            case HandType::ThreeOfAKind:
                if ((ok = !trips.empty())) tb = encode({{trips.r[0], 3}});
                break;
            case HandType::FourOfAKind:
                if ((ok = !quads.empty())) tb = encode({{quads.r[0], 4}});
                break;
            case HandType::FiveOfAKind:
                if ((ok = !fives.empty())) tb = encode({{fives.r[0], 5}});
                break;
            case HandType::FullHouse:
                if (!trips.empty()) {
                    for (int i = 0; i < pairs.n; ++i) {
                        const int p = pairs.r[i];
                        if (p == trips.r[0]) continue;
                        ok = true;
                        tb = encode({{trips.r[0], 3}, {p, 2}});
                        break;
                    }
                }
                break;
            case HandType::Straight:
                if ((ok = straightHigh >= 0)) tb = static_cast<std::uint32_t>(straightHigh + 1) << 16;
                break;
            case HandType::Flush:
                if ((ok = flush)) tb = encode({});
                break;
            case HandType::StraightFlush:
                if ((ok = flush && straightHigh >= 0)) tb = static_cast<std::uint32_t>(straightHigh + 1) << 16;
                break;
            default: break;
        }
        if (ok)
            best = std::max(best, static_cast<pf::HandScore>(s) << 28 | static_cast<pf::HandScore>(t) << 24 | tb);
    }
    return best;
}

inline bool refIsWild(const pf::RuleSet& r, pf::CardId c) {
    switch (r.wild) {
        case pf::WildMode::Deuces: return !pf::isJoker(c) && pf::rankOf(c) == 0;
        case pf::WildMode::Jokers: return pf::isJoker(c);
        case pf::WildMode::OneEyedJacks: return c == pf::makeCard(3, 9) || c == pf::makeCard(2, 9);
        case pf::WildMode::None: break;
    }
    return false;
}

// Best score over every 5-card subset and every wild substitution.
inline pf::HandScore refScore(const pf::RuleSet& rules, pf::CardMask mask) {
    std::vector<pf::CardId> cards;
    for (int i = 0; i < 64; ++i) {
        if (!(mask >> i & 1)) continue;
        const auto c = static_cast<pf::CardId>(i);
        if (pf::isJoker(c) && !refIsWild(rules, c)) continue;  // dead joker
        cards.push_back(c);
    }
    const int n = static_cast<int>(cards.size());
    const int take = std::min(n, 5);
    pf::HandScore best = 0;
    for (unsigned sub = 0; sub < (1u << n); ++sub) {
        if (std::popcount(sub) != take) continue;
        RCard hand[5];
        int naturals = 0, wilds = 0;
        for (int i = 0; i < n; ++i) {
            if (!(sub >> i & 1)) continue;
            const auto c = cards[static_cast<std::size_t>(i)];
            if (refIsWild(rules, c)) ++wilds;
            else hand[naturals++] = {pf::rankOf(c), pf::suitOf(c)};
        }
        // odometer over wild ranks, then each suit choice
        std::vector<int> ranks(static_cast<std::size_t>(wilds), rules.minRank());
        while (true) {
            for (int suit = 0; suit <= (wilds ? 4 : 0); ++suit) {
                for (int w = 0; w < wilds; ++w)
                    hand[naturals + w] = {ranks[static_cast<std::size_t>(w)], suit == 4 ? -1 : suit};
                best = std::max(best, refScore5(rules, hand, take));
            }
            int w = 0;
            while (w < wilds && ++ranks[static_cast<std::size_t>(w)] > 12)
                ranks[static_cast<std::size_t>(w++)] = rules.minRank();
            if (w == wilds) break;
        }
    }
    return best;
}

}  // namespace pftest
