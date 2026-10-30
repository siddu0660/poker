#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

enum class HandType : std::uint8_t {
    HighCard, Pair, TwoPair, ThreeOfAKind, Straight, Flush,
    FullHouse, FourOfAKind, StraightFlush, FiveOfAKind,
    Count
};
constexpr int kHandTypeCount = static_cast<int>(HandType::Count);

enum class WildMode : std::uint8_t { None, Deuces, Jokers, OneEyedJacks };
enum class BettingStructure : std::uint8_t { NoLimit, PotLimit, FixedLimit };

// Single string table for every label (UI, JSON, console), so none drift apart.
struct HandTypeInfo {
    HandType type;
    std::string_view key;      // JSON identifier
    std::string_view display;  // UI label
};
const std::array<HandTypeInfo, kHandTypeCount>& handTypeTable();
std::string_view handTypeKey(HandType t);
std::string_view handTypeDisplay(HandType t);
std::optional<HandType> handTypeFromKey(std::string_view key);

std::string_view wildModeKey(WildMode w);
std::string_view wildModeDisplay(WildMode w);
std::string_view bettingKey(BettingStructure b);
std::string_view bettingDisplay(BettingStructure b);

struct RuleSet {
    std::string name = "Texas Hold'em";

    int deckSize = 52;  // 24..52, multiples of 4 (removes the lowest ranks)
    int jokers = 0;     // 0..2
    WildMode wild = WildMode::None;

    // Weakest -> strongest. Listed types are enabled; HighCard is always first.
    std::vector<HandType> order;
    bool wheelStraight = true;

    int smallBlind = 10;
    int bigBlind = 20;
    int ante = 0;
    int startingStack = 2000;

    BettingStructure betting = BettingStructure::NoLimit;
    int maxRaises = 4;

    RuleSet();

    int ranksInDeck() const { return deckSize / 4; }
    int minRank() const { return 13 - ranksInDeck(); }  // 0 = '2'
    int totalCards() const { return deckSize + jokers; }
    bool hasWilds() const { return wild != WildMode::None; }
    bool isEnabled(HandType t) const;

    bool operator==(const RuleSet&) const = default;
};

std::vector<HandType> standardOrder();   // 52-card hold'em ranking
std::vector<HandType> shortDeckOrder();  // flush > full house, trips > straight

// Fixes invalid values in place; returns human-readable warnings.
std::vector<std::string> sanitize(RuleSet& rules);

struct LoadResult {
    std::optional<RuleSet> rules;      // empty on error
    std::string error;                 // set when rules is empty
    std::vector<std::string> warnings; // unknown keys, values fixed by sanitize()
};

LoadResult rulesFromJsonText(std::string_view text);
LoadResult loadRulesFile(const std::string& path);
std::string rulesToJsonText(const RuleSet& rules);
bool saveRulesFile(const RuleSet& rules, const std::string& path, std::string* error = nullptr);

}  // namespace pf
