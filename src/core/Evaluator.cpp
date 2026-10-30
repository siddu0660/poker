#include "core/Evaluator.hpp"

#include <algorithm>
#include <bit>

namespace pf {

namespace {

constexpr std::uint32_t rankBit(int r) noexcept { return 1u << r; }
inline int topRank(std::uint32_t m) noexcept { return std::bit_width(m) - 1; }

struct RankSets {
    std::array<std::uint16_t, kSuits> suit;
    std::uint32_t any, two, three, four;  // ranks held at least 1/2/3/4 times
    std::uint32_t flush;                  // rank mask of a suit with 5+ cards, else 0
};

inline RankSets rankSets(CardMask cards) noexcept {
    RankSets r;
    r.suit = suitMasks(cards);
    const std::uint32_t c = r.suit[0], d = r.suit[1], h = r.suit[2], s = r.suit[3];
    r.any = c | d | h | s;
    r.two = (c & d) | (h & s) | ((c | d) & (h | s));
    r.three = (c & d & (h | s)) | (h & s & (c | d));
    r.four = c & d & h & s;
    r.flush = 0;
    for (std::uint32_t m : r.suit)
        if (std::popcount(m) >= 5 && (r.flush == 0 || m > r.flush)) r.flush = m;
    return r;
}

// Containment edges (sup, sub): any hand satisfying sub with a duplicate among
// its kickers also satisfies sup. If every enabled sub has an enabled, stronger
// sup, kickers can be read from distinct-rank masks (Monotone mode).
constexpr std::array<std::pair<HandType, HandType>, 7> kContainment{{
    {HandType::ThreeOfAKind, HandType::Pair},
    {HandType::TwoPair, HandType::Pair},
    {HandType::FullHouse, HandType::TwoPair},
    {HandType::FullHouse, HandType::ThreeOfAKind},
    {HandType::FourOfAKind, HandType::ThreeOfAKind},
    {HandType::StraightFlush, HandType::Straight},
    {HandType::StraightFlush, HandType::Flush},
}};

}  // namespace

Evaluator::Evaluator(const RuleSet& rules) : rules_(rules) {
    for (std::size_t i = 0; i < rules_.order.size(); ++i) {
        const auto t = static_cast<std::size_t>(rules_.order[i]);
        enabled_[t] = true;
        strength_[t] = static_cast<std::uint8_t>(i);
    }
    enabled_[static_cast<std::size_t>(HandType::HighCard)] = true;

    const int minRank = std::clamp(rules_.minRank(), 0, 8);
    for (std::uint32_t mask = 0; mask < 8192; ++mask) {
        std::uint8_t high = 0;
        for (int r = 12; r >= minRank + 4; --r) {
            const std::uint32_t window = 0x1Fu << (r - 4);
            if ((mask & window) == window) { high = static_cast<std::uint8_t>(r + 1); break; }
        }
        if (high == 0 && rules_.wheelStraight) {
            const std::uint32_t wheel = rankBit(12) | (0xFu << minRank);
            if ((mask & wheel) == wheel) high = static_cast<std::uint8_t>(minRank + 3 + 1);
        }
        straightHigh_[mask] = high;

        std::uint32_t packed = 0;
        int n = 0;
        for (int r = 12; r >= 0 && n < 5; --r) {
            if (mask & rankBit(r)) {
                packed |= static_cast<std::uint32_t>(r + 1) << (16 - 4 * n);
                ++n;
            }
        }
        topPacked_[mask] = packed;
    }

    auto on = [&](HandType t) { return enabled_[static_cast<std::size_t>(t)]; };
    auto str = [&](HandType t) { return strength_[static_cast<std::size_t>(t)]; };
    bool monotone = true;
    for (auto [sup, sub] : kContainment)
        if (on(sub) && !(on(sup) && str(sup) > str(sub))) monotone = false;

    if (rules_.order == standardOrder()) mode_ = Mode::Standard;
    else if (monotone) mode_ = Mode::Monotone;
    else mode_ = Mode::General;
}

HandScore Evaluator::evaluate(CardMask cards) const noexcept {
    switch (mode_) {
        case Mode::Standard: return evalStandard(cards);
        case Mode::Monotone: return evalMonotone(cards);
        case Mode::General: break;
    }
    return evalGeneral(cards);
}

HandScore Evaluator::evalStandard(CardMask cards) const noexcept {
    const RankSets r = rankSets(cards);
    if (r.flush) {
        if (const std::uint32_t sh = straightHigh_[r.flush]) return make(HandType::StraightFlush, sh << 16);
    }
    if (r.four) {
        const int q = topRank(r.four);
        return make(HandType::FourOfAKind,
                    static_cast<std::uint32_t>(q + 1) << 16 | pack(r.any & ~rankBit(q), 1) << 12);
    }
    if (r.three) {
        const int t = topRank(r.three);
        if (const std::uint32_t pairs = r.two & ~rankBit(t))
            return make(HandType::FullHouse,
                        static_cast<std::uint32_t>(t + 1) << 16 | static_cast<std::uint32_t>(topRank(pairs) + 1) << 12);
    }
    if (r.flush) return make(HandType::Flush, topPacked_[r.flush]);
    if (const std::uint32_t sh = straightHigh_[r.any]) return make(HandType::Straight, sh << 16);
    if (r.three) {
        const int t = topRank(r.three);
        return make(HandType::ThreeOfAKind,
                    static_cast<std::uint32_t>(t + 1) << 16 | pack(r.any & ~rankBit(t), 2) << 8);
    }
    if (r.two) {
        const int p1 = topRank(r.two);
        if (const std::uint32_t rest = r.two & ~rankBit(p1)) {
            const int p2 = topRank(rest);
            return make(HandType::TwoPair, static_cast<std::uint32_t>(p1 + 1) << 16 |
                                               static_cast<std::uint32_t>(p2 + 1) << 12 |
                                               pack(r.any & ~rankBit(p1) & ~rankBit(p2), 1) << 8);
        }
        return make(HandType::Pair, static_cast<std::uint32_t>(p1 + 1) << 16 | pack(r.any & ~rankBit(p1), 3) << 4);
    }
    return make(HandType::HighCard, topPacked_[r.any]);
}

HandScore Evaluator::evalMonotone(CardMask cards) const noexcept {
    const RankSets r = rankSets(cards);
    HandScore best = make(HandType::HighCard, topPacked_[r.any]);
    auto consider = [&](HandType t, std::uint32_t tiebreak) {
        if (enabled_[static_cast<std::size_t>(t)]) best = std::max(best, make(t, tiebreak));
    };

    if (r.flush) {
        if (const std::uint32_t sh = straightHigh_[r.flush]) consider(HandType::StraightFlush, sh << 16);
        consider(HandType::Flush, topPacked_[r.flush]);
    }
    if (r.four) {
        const int q = topRank(r.four);
        consider(HandType::FourOfAKind, static_cast<std::uint32_t>(q + 1) << 16 | pack(r.any & ~rankBit(q), 1) << 12);
    }
    if (r.three) {
        const int t = topRank(r.three);
        if (const std::uint32_t pairs = r.two & ~rankBit(t))
            consider(HandType::FullHouse,
                     static_cast<std::uint32_t>(t + 1) << 16 | static_cast<std::uint32_t>(topRank(pairs) + 1) << 12);
        consider(HandType::ThreeOfAKind, static_cast<std::uint32_t>(t + 1) << 16 | pack(r.any & ~rankBit(t), 2) << 8);
    }
    if (const std::uint32_t sh = straightHigh_[r.any]) consider(HandType::Straight, sh << 16);
    if (r.two) {
        const int p1 = topRank(r.two);
        if (const std::uint32_t rest = r.two & ~rankBit(p1)) {
            const int p2 = topRank(rest);
            consider(HandType::TwoPair, static_cast<std::uint32_t>(p1 + 1) << 16 |
                                            static_cast<std::uint32_t>(p2 + 1) << 12 |
                                            pack(r.any & ~rankBit(p1) & ~rankBit(p2), 1) << 8);
        }
        consider(HandType::Pair, static_cast<std::uint32_t>(p1 + 1) << 16 | pack(r.any & ~rankBit(p1), 3) << 4);
    }
    return best;
}

HandScore Evaluator::evalGeneral(CardMask cards) const noexcept {
    const RankSets r = rankSets(cards);
    std::array<int, kRanks> count{};
    for (int k = 0; k < kRanks; ++k)
        for (std::uint16_t m : r.suit) count[static_cast<std::size_t>(k)] += (m >> k) & 1;

    // Highest remaining cards (with repeats) after removing `used`, as k nibbles.
    auto kickers = [&](std::array<int, kRanks> left, int k) {
        std::uint32_t out = 0;
        for (int rank = 12; rank >= 0 && k > 0; --rank) {
            for (int n = left[static_cast<std::size_t>(rank)]; n > 0 && k > 0; --n, --k)
                out |= static_cast<std::uint32_t>(rank + 1) << (4 * (k - 1));
        }
        return out;
    };
    auto without = [&](int rank, int n) {
        auto left = count;
        left[static_cast<std::size_t>(rank)] -= n;
        return left;
    };

    HandScore best = make(HandType::HighCard, kickers(count, 5));
    auto consider = [&](HandType t, std::uint32_t tiebreak) {
        if (enabled_[static_cast<std::size_t>(t)]) best = std::max(best, make(t, tiebreak));
    };

    if (r.flush) {
        if (const std::uint32_t sh = straightHigh_[r.flush]) consider(HandType::StraightFlush, sh << 16);
        consider(HandType::Flush, topPacked_[r.flush]);
    }
    if (const std::uint32_t sh = straightHigh_[r.any]) consider(HandType::Straight, sh << 16);
    if (r.four) {
        const int q = topRank(r.four);
        consider(HandType::FourOfAKind, static_cast<std::uint32_t>(q + 1) << 16 | kickers(without(q, 4), 1) << 12);
    }
    if (r.three) {
        const int t = topRank(r.three);
        if (const std::uint32_t pairs = r.two & ~rankBit(t))
            consider(HandType::FullHouse,
                     static_cast<std::uint32_t>(t + 1) << 16 | static_cast<std::uint32_t>(topRank(pairs) + 1) << 12);
        consider(HandType::ThreeOfAKind, static_cast<std::uint32_t>(t + 1) << 16 | kickers(without(t, 3), 2) << 8);
    }
    if (r.two) {
        const int p1 = topRank(r.two);
        if (const std::uint32_t rest = r.two & ~rankBit(p1)) {
            const int p2 = topRank(rest);
            auto left = without(p1, 2);
            left[static_cast<std::size_t>(p2)] -= 2;
            consider(HandType::TwoPair, static_cast<std::uint32_t>(p1 + 1) << 16 |
                                            static_cast<std::uint32_t>(p2 + 1) << 12 | kickers(left, 1) << 8);
        }
        consider(HandType::Pair, static_cast<std::uint32_t>(p1 + 1) << 16 | kickers(without(p1, 2), 3) << 4);
    }
    return best;
}

}  // namespace pf
