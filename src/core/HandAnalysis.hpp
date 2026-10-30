#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Cards.hpp"
#include "core/Evaluator.hpp"

namespace pf {

using HoleCards = std::array<CardId, 2>;

// "Ten" / "Tens" for rank 0..12 (0 = deuce).
std::string_view rankName(int rank, bool plural);

// Readable text for a score, e.g. "Two Pair, Kings and Tens, Queen kicker".
// Works with custom rankings and wild cards (the score carries the ranks).
std::string describeScore(HandScore score);
// The same without the kicker, for small labels: "Two Pair, Kings and Tens".
std::string describeScoreShort(HandScore score);
// Before the flop: "Pocket Tens", "Queen-Ten, offsuit", "Ace-King, suited".
std::string describeStartingHand(const HoleCards& hole);

struct MadeHand {
    bool valid = false;
    HandScore score = 0;
    HandType type = HandType::HighCard;
    std::string description;  // full text, or the starting-hand text before the flop
    std::string label;        // short text for the table seat
    CardMask bestMask = 0;    // the (at most) five cards that make the hand
};

// Hero's best hand from the hole cards plus the revealed board.
MadeHand analyzeMadeHand(const Evaluator& ev, const HoleCards& hole, const std::vector<CardId>& board);

// Exact distribution of the hero's final hand type over every way of
// completing the board to five cards. Only built when the number of
// completions is small (flop onwards); preflop is left to the simulator.
struct ExactDistribution {
    bool available = false;
    std::uint64_t total = 0;
    std::array<std::uint64_t, kHandTypeCount> counts{};
    double share(HandType t) const {
        return total ? static_cast<double>(counts[static_cast<std::size_t>(t)]) / static_cast<double>(total) : 0.0;
    }
};

// `dead` are cards the hero knows are gone (known opponent cards, folded cards).
ExactDistribution exactFinalDistribution(const Evaluator& ev, const HoleCards& hole,
                                         const std::vector<CardId>& board, CardMask dead = 0,
                                         std::uint64_t maxCompletions = 20000);

// One row of the "hands you can get" table.
struct HandTypeRow {
    HandType type = HandType::HighCard;
    bool have = false;      // already holds this type or better
    int outs = -1;          // unseen cards that bring the hand to this type or better; -1 = n/a
    bool possible = true;   // false: can no longer finish as exactly this type
    double exactShare = -1; // exact chance of finishing as exactly this type, -1 = not computed
    std::string needs;      // "any Ten or Queen", "any heart", "As, Kd"
};

struct HandAnalysis {
    bool valid = false;
    MadeHand made;
    std::vector<HandTypeRow> rows;  // enabled types, weakest to strongest
    std::vector<std::string> draws; // "Flush draw (9 outs)", "Two overcards (6 outs to top pair)", ...
    std::string label;              // "Pair of Tens", "Flush draw"
    int unseen = 0;                 // cards the hero cannot see
};

// Needs hole cards (neither kNoCard). The board has 0..5 revealed cards.
HandAnalysis analyzeHand(const Evaluator& ev, const HoleCards& hole, const std::vector<CardId>& board,
                         CardMask dead = 0);

// "By river %" -> "Reach %": cumulative share at or above each type's strength.
// Disabled types get 0.
std::array<double, kHandTypeCount> reachShares(const RuleSet& rules,
                                               const std::array<double, kHandTypeCount>& share);

}  // namespace pf
