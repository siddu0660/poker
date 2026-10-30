#pragma once
#include <array>
#include <cstdint>

#include "core/Cards.hpp"
#include "core/Rules.hpp"

namespace pf {

// Score layout: strength<<28 | category<<24 | tiebreak (20 bits).
// strength is the category's index in the rule's ranking order. tiebreak is up to
// five rank nibbles, highest first, each stored as rank+1 so 0 means "no card".
// Higher score = better hand; compare scores as plain integers.
using HandScore = std::uint32_t;

constexpr HandType scoreCategory(HandScore s) noexcept { return static_cast<HandType>((s >> 24) & 0xF); }
constexpr int scoreStrength(HandScore s) noexcept { return static_cast<int>(s >> 28); }
constexpr std::uint32_t scoreTiebreak(HandScore s) noexcept { return s & 0xFFFFF; }

// Hand semantics (all paths agree; tests check them against brute force):
// A 5-card hand satisfies every category its rank counts and suits allow
// (e.g. AAAKK is a full house, three of a kind, two pair and a pair). Its value
// is the best *enabled* category it satisfies, tie-broken by that category's
// defining ranks followed by the remaining cards, highest first. A larger hand
// is worth its best 5-card subset. Disabled categories therefore fall through.
//
// Wild cards are evaluated in milestone M3; until then wild cards count as
// ordinary cards and jokers are ignored.
class Evaluator {
public:
    explicit Evaluator(const RuleSet& rules);

    HandScore evaluate(CardMask cards) const noexcept;

    const RuleSet& rules() const noexcept { return rules_; }

    // Highest rank of a straight in a 13-bit rank mask, or -1 if none.
    int straightHigh(std::uint16_t rankMask) const noexcept { return straightHigh_[rankMask] - 1; }

    enum class Mode : std::uint8_t {
        Standard,  // standard order, all types on: return at the first hit
        Monotone,  // every containing type ranks higher: max over table candidates
        General,   // anything else: multiset kickers per candidate
    };
    Mode mode() const noexcept { return mode_; }

private:
    HandScore make(HandType t, std::uint32_t tiebreak) const noexcept {
        const auto i = static_cast<std::size_t>(t);
        return static_cast<HandScore>(strength_[i]) << 28 | static_cast<HandScore>(i) << 24 | tiebreak;
    }
    // Top k ranks of a rank mask as k nibbles (highest first) in the low bits.
    std::uint32_t pack(std::uint32_t rankMask, int k) const noexcept {
        return topPacked_[rankMask] >> (4 * (5 - k));
    }

    HandScore evalStandard(CardMask cards) const noexcept;
    HandScore evalMonotone(CardMask cards) const noexcept;
    HandScore evalGeneral(CardMask cards) const noexcept;

    RuleSet rules_;
    Mode mode_ = Mode::Standard;
    std::array<std::uint8_t, kHandTypeCount> strength_{};
    std::array<bool, kHandTypeCount> enabled_{};
    std::array<std::uint8_t, 8192> straightHigh_{};  // high rank + 1, 0 = none
    std::array<std::uint32_t, 8192> topPacked_{};     // top 5 ranks as rank+1 nibbles
};

}  // namespace pf
