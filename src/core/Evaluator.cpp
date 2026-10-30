#include "core/Evaluator.hpp"

#include <algorithm>
#include <bit>
#include <initializer_list>

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

    switch (rules_.wild) {
        case WildMode::None: break;
        case WildMode::Deuces:
            if (minRank == 0)
                for (int suit = 0; suit < kSuits; ++suit) wildMask_ |= bit(makeCard(suit, 0));
            break;
        case WildMode::Jokers: wildMask_ = bit(kJokerA) | bit(kJokerB); break;
        case WildMode::OneEyedJacks: wildMask_ = bit(makeCard(3, 9)) | bit(makeCard(2, 9)); break;
    }

    if (rules_.order == standardOrder()) mode_ = Mode::Standard;
    else if (monotone) mode_ = Mode::Monotone;
    else mode_ = Mode::General;
}

HandScore Evaluator::evaluate(CardMask cards) const noexcept {
    if (const CardMask wild = cards & wildMask_) [[unlikely]]
        return evalWild(cards & ~wildMask_, popcount(wild));
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

// Analytic wild-card evaluation: for each enabled type, build its best hand
// directly. Defining ranks are chosen highest first, naturals are used before
// wilds, and any wild left over plays as an ace kicker.
HandScore Evaluator::evalWild(CardMask natural, int wilds) const noexcept {
    const RankSets r = rankSets(natural);
    const int lo = std::clamp(rules_.minRank(), 0, 8);
    const std::uint32_t deckRanks = (0x1FFFu >> lo) << lo;
    // Ranks held at least n times naturally; with w wilds, rank x reaches n copies
    // exactly when it is in atLeast(n - w).
    auto atLeast = [&](int n) -> std::uint32_t {
        switch (n) {
            case 1: return r.any;
            case 2: return r.two;
            case 3: return r.three;
            case 4: return r.four;
            default: return n <= 0 ? deckRanks : 0;
        }
    };
    auto cnt = [&](int rank) {
        const std::uint32_t b = rankBit(rank);
        return int{(r.any & b) != 0} + int{(r.two & b) != 0} + int{(r.three & b) != 0} + int{(r.four & b) != 0};
    };
    auto need = [&](int rank, int n) { return std::max(0, n - cnt(rank)); };

    using Counts = std::array<int, kRanks>;
    // Natural cards left after the defining ranks take up to n copies each.
    auto leftAfter = [&](std::initializer_list<std::pair<int, int>> used) {
        Counts left{};
        for (int k = 0; k < kRanks; ++k) left[static_cast<std::size_t>(k)] = cnt(k);
        for (auto [rank, n] : used) {
            auto& c = left[static_cast<std::size_t>(rank)];
            c -= std::min(c, n);
        }
        return left;
    };
    // Leftover wilds play as aces, then the highest natural cards.
    auto kickers = [&](const Counts& left, int spare, int slots) {
        std::uint32_t out = 0;
        for (; spare > 0 && slots > 0; --spare, --slots) out |= 13u << (4 * (slots - 1));
        for (int rank = 12; rank >= 0 && slots > 0; --rank)
            for (int n = left[static_cast<std::size_t>(rank)]; n > 0 && slots > 0; --n, --slots)
                out |= static_cast<std::uint32_t>(rank + 1) << (4 * (slots - 1));
        return out;
    };
    // Highest straight reachable with the wilds, as high rank + 1 (0 = none).
    auto straight = [&](std::uint32_t mask) -> std::uint32_t {
        for (int hi = 12; hi >= lo + 4; --hi)
            if (std::popcount((0x1Fu << (hi - 4)) & ~mask) <= wilds) return static_cast<std::uint32_t>(hi + 1);
        if (rules_.wheelStraight && std::popcount((rankBit(12) | 0xFu << lo) & ~mask) <= wilds)
            return static_cast<std::uint32_t>(lo + 4);
        return 0;
    };
    auto rankNib = [](int rank, int pos) { return static_cast<std::uint32_t>(rank + 1) << pos; };

    // Strength outranks any tiebreak, so the first type (strongest first) that
    // the hand can make is the answer. Defining ranks are chosen highest first,
    // naturals are used before wilds, and spare wilds play as ace kickers.
    for (auto it = rules_.order.rbegin(); it != rules_.order.rend(); ++it) {
        const HandType t = *it;
        switch (t) {
            case HandType::FiveOfAKind:
                if (const std::uint32_t m = atLeast(5 - wilds)) return make(t, rankNib(topRank(m), 16));
                break;
            case HandType::FourOfAKind:
                if (const std::uint32_t m = atLeast(4 - wilds)) {
                    const int q = topRank(m);
                    return make(t, rankNib(q, 16) | kickers(leftAfter({{q, 4}}), wilds - need(q, 4), 1) << 12);
                }
                break;
            case HandType::FullHouse:
                for (std::uint32_t m = atLeast(3 - wilds); m; m &= ~rankBit(topRank(m))) {
                    const int trips = topRank(m);
                    const std::uint32_t pairs = atLeast(2 - (wilds - need(trips, 3))) & ~rankBit(trips);
                    if (pairs) return make(t, rankNib(trips, 16) | rankNib(topRank(pairs), 12));
                }
                break;
            case HandType::ThreeOfAKind:
                if (const std::uint32_t m = atLeast(3 - wilds)) {
                    const int k = topRank(m);
                    return make(t, rankNib(k, 16) | kickers(leftAfter({{k, 3}}), wilds - need(k, 3), 2) << 8);
                }
                break;
            case HandType::TwoPair:
                for (std::uint32_t m = atLeast(2 - wilds); m; m &= ~rankBit(topRank(m))) {
                    const int p1 = topRank(m);
                    const int spare = wilds - need(p1, 2);
                    if (const std::uint32_t lower = atLeast(2 - spare) & (rankBit(p1) - 1)) {
                        const int p2 = topRank(lower);
                        return make(t, rankNib(p1, 16) | rankNib(p2, 12) |
                                           kickers(leftAfter({{p1, 2}, {p2, 2}}), spare - need(p2, 2), 1) << 8);
                    }
                }
                break;
            case HandType::Pair:
                if (const std::uint32_t m = atLeast(2 - wilds)) {
                    const int p = topRank(m);
                    return make(t, rankNib(p, 16) | kickers(leftAfter({{p, 2}}), wilds - need(p, 2), 3) << 4);
                }
                break;
            case HandType::Straight:
                if (const std::uint32_t sh = straight(r.any)) return make(t, sh << 16);
                break;
            case HandType::StraightFlush: {
                std::uint32_t sh = 0;
                for (std::uint32_t m : r.suit)
                    if (std::popcount(m) + wilds >= 5) sh = std::max(sh, straight(m));
                if (sh) return make(t, sh << 16);
                break;
            }
            case HandType::Flush: {
                std::uint32_t tb = 0;
                for (std::uint32_t m : r.suit) {
                    if (std::popcount(m) + wilds < 5) continue;
                    std::uint32_t filled = m;
                    for (int rank = 12, left = wilds; rank >= lo && left > 0; --rank)
                        if (!(filled & rankBit(rank))) { filled |= rankBit(rank); --left; }
                    tb = std::max(tb, topPacked_[filled]);
                }
                if (tb) return make(t, tb);
                break;
            }
            case HandType::HighCard:
            case HandType::Count: break;
        }
    }
    return make(HandType::HighCard, kickers(leftAfter({}), wilds, 5));
}

}  // namespace pf
