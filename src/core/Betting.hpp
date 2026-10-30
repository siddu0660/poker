#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>

#include "core/Rules.hpp"

namespace pf {

enum class Street : std::uint8_t { Preflop, Flop, Turn, River };

constexpr int boardCardsFor(Street s) noexcept {
    constexpr int n[] = {0, 3, 4, 5};
    return n[static_cast<int>(s)];
}
constexpr std::string_view streetName(Street s) noexcept {
    constexpr std::string_view n[] = {"Preflop", "Flop", "Turn", "River"};
    return n[static_cast<int>(s)];
}

// The betting round as seen by the player who is about to act.
struct BettingContext {
    int pot = 0;         // every chip in the middle: antes plus all bets this hand
    int currentBet = 0;  // highest bet this round
    int playerBet = 0;   // this player's bet so far this round
    int stack = 0;       // this player's chips behind
    int lastRaise = 0;   // size of the last bet or raise this round (0 = none yet)
    int raisesMade = 0;  // raises so far this round (fixed-limit cap)
    Street street = Street::Preflop;
};

// Legal raise sizes as "raise to" totals for this round.
struct RaiseRange {
    bool canRaise = false;
    int minTo = 0;
    int maxTo = 0;
};

inline int toCall(const BettingContext& c) noexcept {
    return std::max(0, std::min(c.currentBet - c.playerBet, c.stack));
}

inline int fixedLimitStep(const RuleSet& r, Street s) noexcept {
    return s >= Street::Turn ? 2 * r.bigBlind : r.bigBlind;
}

inline RaiseRange raiseRange(const RuleSet& r, const BettingContext& c) noexcept {
    RaiseRange out;
    const int allIn = c.playerBet + c.stack;
    if (allIn <= c.currentBet) return out;  // can only call, or fold

    int minTo = 0, maxTo = 0;
    switch (r.betting) {
        case BettingStructure::NoLimit:
            minTo = c.currentBet + std::max(c.lastRaise, r.bigBlind);
            maxTo = allIn;
            break;
        case BettingStructure::PotLimit:
            minTo = c.currentBet + std::max(c.lastRaise, r.bigBlind);
            // Call first, then raise by the size of the pot including that call.
            maxTo = c.currentBet + c.pot + toCall(c);
            break;
        case BettingStructure::FixedLimit:
            if (c.raisesMade >= r.maxRaises) return out;
            minTo = maxTo = c.currentBet + fixedLimitStep(r, c.street);
            break;
    }
    maxTo = std::min(maxTo, allIn);
    minTo = std::min(minTo, maxTo);
    out.canRaise = maxTo > c.currentBet;
    out.minTo = minTo;
    out.maxTo = maxTo;
    return out;
}

}  // namespace pf
