#pragma once
// Brute-force reference evaluator, written directly from the semantics in
// Evaluator.hpp and deliberately sharing no code with it: score every 5-card
// subset category by category and keep the best.
#include <algorithm>
#include <array>
#include <vector>

#include "core/Cards.hpp"
#include "core/Evaluator.hpp"
#include "core/Rules.hpp"

namespace pftest {

inline pf::HandScore refScore5(const pf::RuleSet& rules, const std::vector<pf::CardId>& hand) {
    using pf::HandType;
    std::array<int, 13> count{};
    for (auto c : hand) ++count[static_cast<std::size_t>(pf::rankOf(c))];

    std::vector<int> ranksDesc;
    for (auto c : hand) ranksDesc.push_back(pf::rankOf(c));
    std::sort(ranksDesc.rbegin(), ranksDesc.rend());

    const bool five = hand.size() == 5;
    bool flush = five;
    for (auto c : hand) flush = flush && pf::suitOf(c) == pf::suitOf(hand[0]);

    int straightHigh = -1;
    if (five) {
        std::vector<int> distinct = ranksDesc;
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        const int m = rules.minRank();
        if (distinct.size() == 5) {
            if (distinct[0] - distinct[4] == 4 && distinct[4] >= m) straightHigh = distinct[0];
            else if (rules.wheelStraight && distinct[0] == 12 && distinct[1] == m + 3 && distinct[4] == m)
                straightHigh = m + 3;
        }
    }

    auto ranksWithAtLeast = [&](int n) {
        std::vector<int> out;
        for (int r = 12; r >= 0; --r) if (count[static_cast<std::size_t>(r)] >= n) out.push_back(r);
        return out;
    };
    // defining ranks, then every other card high to low, first 5 nibbles
    auto encode = [&](std::vector<int> defining, std::vector<std::pair<int, int>> used) {
        std::array<int, 13> left = count;
        for (auto [r, n] : used) left[static_cast<std::size_t>(r)] -= n;
        std::vector<int> nib = defining;
        for (int r = 12; r >= 0; --r)
            for (int k = 0; k < left[static_cast<std::size_t>(r)]; ++k) nib.push_back(r);
        std::uint32_t tb = 0;
        for (std::size_t i = 0; i < nib.size() && i < 5; ++i)
            tb |= static_cast<std::uint32_t>(nib[i] + 1) << (16 - 4 * i);
        return tb;
    };

    pf::HandScore best = 0;
    for (std::size_t s = 0; s < rules.order.size(); ++s) {
        const HandType t = rules.order[s];
        bool ok = false;
        std::uint32_t tb = 0;
        const auto pairs = ranksWithAtLeast(2);
        const auto trips = ranksWithAtLeast(3);
        const auto quads = ranksWithAtLeast(4);
        switch (t) {
            case HandType::HighCard: ok = true; tb = encode({}, {}); break;
            case HandType::Pair:
                if ((ok = !pairs.empty())) tb = encode({pairs[0]}, {{pairs[0], 2}});
                break;
            case HandType::TwoPair:
                if ((ok = pairs.size() >= 2)) tb = encode({pairs[0], pairs[1]}, {{pairs[0], 2}, {pairs[1], 2}});
                break;
            case HandType::ThreeOfAKind:
                if ((ok = !trips.empty())) tb = encode({trips[0]}, {{trips[0], 3}});
                break;
            case HandType::FourOfAKind:
                if ((ok = !quads.empty())) tb = encode({quads[0]}, {{quads[0], 4}});
                break;
            case HandType::FullHouse:
                if (!trips.empty()) {
                    for (int p : pairs) {
                        if (p == trips[0]) continue;
                        ok = true;
                        tb = encode({trips[0], p}, {{trips[0], 3}, {p, 2}});
                        break;
                    }
                }
                break;
            case HandType::Straight:
                if ((ok = straightHigh >= 0)) tb = static_cast<std::uint32_t>(straightHigh + 1) << 16;
                break;
            case HandType::Flush:
                if ((ok = flush)) tb = encode({}, {});
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

inline pf::HandScore refScore(const pf::RuleSet& rules, const std::vector<pf::CardId>& cards) {
    if (cards.size() <= 5) return refScore5(rules, cards);
    pf::HandScore best = 0;
    const std::size_t n = cards.size();
    std::vector<pf::CardId> sub(5);
    for (std::size_t a = 0; a < n; ++a)
        for (std::size_t b = a + 1; b < n; ++b)
            for (std::size_t c = b + 1; c < n; ++c)
                for (std::size_t d = c + 1; d < n; ++d)
                    for (std::size_t e = d + 1; e < n; ++e) {
                        sub = {cards[a], cards[b], cards[c], cards[d], cards[e]};
                        best = std::max(best, refScore5(rules, sub));
                    }
    return best;
}

}  // namespace pftest
