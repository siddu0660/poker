#include "core/Rules.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace pf {

using nlohmann::json;

namespace {

constexpr std::array<HandTypeInfo, kHandTypeCount> kHandTypes{{
    {HandType::HighCard, "high_card", "High Card"},
    {HandType::Pair, "pair", "Pair"},
    {HandType::TwoPair, "two_pair", "Two Pair"},
    {HandType::ThreeOfAKind, "three_of_a_kind", "Three of a Kind"},
    {HandType::Straight, "straight", "Straight"},
    {HandType::Flush, "flush", "Flush"},
    {HandType::FullHouse, "full_house", "Full House"},
    {HandType::FourOfAKind, "four_of_a_kind", "Four of a Kind"},
    {HandType::StraightFlush, "straight_flush", "Straight Flush"},
    {HandType::FiveOfAKind, "five_of_a_kind", "Five of a Kind"},
}};

struct WildInfo { WildMode mode; std::string_view key, display; };
constexpr std::array<WildInfo, 4> kWild{{
    {WildMode::None, "none", "None"},
    {WildMode::Deuces, "deuces", "Deuces Wild"},
    {WildMode::Jokers, "jokers", "Jokers Wild"},
    {WildMode::OneEyedJacks, "one_eyed_jacks", "One-Eyed Jacks"},
}};

struct BetInfo { BettingStructure mode; std::string_view key, display; };
constexpr std::array<BetInfo, 3> kBetting{{
    {BettingStructure::NoLimit, "no_limit", "No-Limit"},
    {BettingStructure::PotLimit, "pot_limit", "Pot-Limit"},
    {BettingStructure::FixedLimit, "fixed_limit", "Fixed-Limit"},
}};

template <class Table, class Mode>
std::string_view lookupKey(const Table& t, Mode m) {
    for (const auto& e : t) if (e.mode == m) return e.key;
    return {};
}
template <class Table, class Mode>
std::string_view lookupDisplay(const Table& t, Mode m) {
    for (const auto& e : t) if (e.mode == m) return e.display;
    return {};
}
template <class Table>
auto lookupFromKey(const Table& t, std::string_view key) {
    using Mode = decltype(t[0].mode);
    std::optional<std::decay_t<Mode>> out;
    for (const auto& e : t) if (e.key == key) out = e.mode;
    return out;
}

std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string quote(std::string_view s) { return "\"" + std::string(s) + "\""; }

// Typed access to a JSON object; records unknown keys as warnings.
class ObjectReader {
public:
    ObjectReader(const json& obj, std::string path, std::vector<std::string>& warnings)
        : obj_(obj), path_(std::move(path)), warnings_(warnings) {}

    const json* get(const char* key) {
        seen_.push_back(key);
        auto it = obj_.find(key);
        return it == obj_.end() ? nullptr : &*it;
    }

    void warnUnknown() {
        for (auto it = obj_.begin(); it != obj_.end(); ++it) {
            if (std::find(seen_.begin(), seen_.end(), it.key()) == seen_.end())
                warnings_.push_back("Unknown key " + quote(path_ + it.key()) + " ignored");
        }
    }

    std::string path(const char* key) const { return path_ + key; }

private:
    const json& obj_;
    std::string path_;
    std::vector<std::string>& warnings_;
    std::vector<std::string> seen_;
};

struct ParseError { std::string message; };

void readInt(ObjectReader& r, const char* key, int& out) {
    const json* v = r.get(key);
    if (!v) return;
    if (!v->is_number_integer()) throw ParseError{quote(r.path(key)) + " must be an integer"};
    const auto n = v->get<std::int64_t>();
    if (n < -1'000'000'000 || n > 1'000'000'000) throw ParseError{quote(r.path(key)) + " is out of range"};
    out = static_cast<int>(n);
}
void readBool(ObjectReader& r, const char* key, bool& out) {
    const json* v = r.get(key);
    if (!v) return;
    if (!v->is_boolean()) throw ParseError{quote(r.path(key)) + " must be true or false"};
    out = v->get<bool>();
}
void readString(ObjectReader& r, const char* key, std::string& out) {
    const json* v = r.get(key);
    if (!v) return;
    if (!v->is_string()) throw ParseError{quote(r.path(key)) + " must be a string"};
    out = v->get<std::string>();
}
template <class Table, class Mode>
void readEnum(ObjectReader& r, const char* key, const Table& table, Mode& out) {
    const json* v = r.get(key);
    if (!v) return;
    if (!v->is_string()) throw ParseError{quote(r.path(key)) + " must be a string"};
    auto m = lookupFromKey(table, v->get<std::string>());
    if (!m) throw ParseError{quote(r.path(key)) + " has unknown value " + quote(v->get<std::string>())};
    out = *m;
}
const json* readObject(ObjectReader& r, const char* key) {
    const json* v = r.get(key);
    if (!v) return nullptr;
    if (!v->is_object()) throw ParseError{quote(r.path(key)) + " must be an object"};
    return v;
}

}  // namespace

const std::array<HandTypeInfo, kHandTypeCount>& handTypeTable() { return kHandTypes; }
std::string_view handTypeKey(HandType t) { return kHandTypes[static_cast<std::size_t>(t)].key; }
std::string_view handTypeDisplay(HandType t) { return kHandTypes[static_cast<std::size_t>(t)].display; }
std::optional<HandType> handTypeFromKey(std::string_view key) {
    for (const auto& e : kHandTypes) if (e.key == key) return e.type;
    return std::nullopt;
}
std::string_view wildModeKey(WildMode w) { return lookupKey(kWild, w); }
std::string_view wildModeDisplay(WildMode w) { return lookupDisplay(kWild, w); }
std::string_view bettingKey(BettingStructure b) { return lookupKey(kBetting, b); }
std::string_view bettingDisplay(BettingStructure b) { return lookupDisplay(kBetting, b); }

std::vector<HandType> standardOrder() {
    return {HandType::HighCard, HandType::Pair, HandType::TwoPair, HandType::ThreeOfAKind,
            HandType::Straight, HandType::Flush, HandType::FullHouse, HandType::FourOfAKind,
            HandType::StraightFlush};
}

std::vector<HandType> shortDeckOrder() {
    return {HandType::HighCard, HandType::Pair, HandType::TwoPair, HandType::Straight,
            HandType::ThreeOfAKind, HandType::FullHouse, HandType::Flush, HandType::FourOfAKind,
            HandType::StraightFlush};
}

RuleSet::RuleSet() : order(standardOrder()) {}

bool RuleSet::isEnabled(HandType t) const {
    return std::find(order.begin(), order.end(), t) != order.end();
}

std::vector<std::string> sanitize(RuleSet& r) {
    std::vector<std::string> w;
    auto fixed = [&](const std::string& what, int from, int to) {
        w.push_back(what + " " + std::to_string(from) + " adjusted to " + std::to_string(to));
    };

    // Deck: 24..52 in steps of 4.
    int deck = std::clamp(r.deckSize, 24, 52);
    deck = 24 + ((deck - 24 + 2) / 4) * 4;  // round to nearest multiple of 4
    deck = std::min(deck, 52);
    if (deck != r.deckSize) { fixed("Deck size", r.deckSize, deck); r.deckSize = deck; }

    const int jokers = std::clamp(r.jokers, 0, 2);
    if (jokers != r.jokers) { fixed("Jokers", r.jokers, jokers); r.jokers = jokers; }

    if (r.wild == WildMode::Jokers && r.jokers == 0) {
        w.push_back("Jokers are wild but the deck has none; added 1 joker");
        r.jokers = 1;
    }
    if (r.wild != WildMode::Jokers && r.jokers > 0 && r.wild != WildMode::None) {
        w.push_back("Jokers in the deck are not wild under this wild-card mode; they act as dead cards");
    }
    if (r.wild == WildMode::Deuces && r.minRank() > 0) {
        w.push_back("Deuces are wild but the deck has no deuces; no card is wild");
    }

    // Hand-ranking order: no duplicates, High Card first, Five of a Kind only with wilds.
    std::vector<HandType> cleaned;
    for (HandType t : r.order) {
        if (t == HandType::HighCard) continue;
        if (std::find(cleaned.begin(), cleaned.end(), t) != cleaned.end()) {
            w.push_back("Duplicate hand type " + quote(handTypeDisplay(t)) + " removed");
            continue;
        }
        if (t == HandType::FiveOfAKind && !r.hasWilds()) {
            w.push_back("Five of a Kind needs wild cards; removed");
            continue;
        }
        cleaned.push_back(t);
    }
    cleaned.insert(cleaned.begin(), HandType::HighCard);
    if (cleaned != r.order) {
        const bool highFirst = !r.order.empty() && r.order.front() == HandType::HighCard;
        if (!highFirst) w.push_back("High Card is always enabled and weakest; moved to the front");
        r.order = std::move(cleaned);
    }

    // Money.
    if (r.bigBlind < 1) { fixed("Big blind", r.bigBlind, 1); r.bigBlind = 1; }
    if (r.smallBlind < 0) { fixed("Small blind", r.smallBlind, 0); r.smallBlind = 0; }
    if (r.smallBlind > r.bigBlind) { fixed("Small blind", r.smallBlind, r.bigBlind); r.smallBlind = r.bigBlind; }
    if (r.ante < 0) { fixed("Ante", r.ante, 0); r.ante = 0; }
    if (r.startingStack < r.bigBlind) {
        fixed("Starting stack", r.startingStack, r.bigBlind * 10);
        r.startingStack = r.bigBlind * 10;
    }

    const int raises = std::clamp(r.maxRaises, 1, 10);
    if (raises != r.maxRaises) { fixed("Max raises", r.maxRaises, raises); r.maxRaises = raises; }

    if (r.name.empty()) { r.name = "Custom Rules"; w.push_back("Rule set had no name; called it \"Custom Rules\""); }
    return w;
}

LoadResult rulesFromJsonText(std::string_view text) {
    LoadResult res;
    json doc = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) { res.error = "Not valid JSON"; return res; }
    if (!doc.is_object()) { res.error = "Top level must be a JSON object"; return res; }

    RuleSet rs;
    try {
        ObjectReader root(doc, "", res.warnings);
        readString(root, "name", rs.name);
        readEnum(root, "wild", kWild, rs.wild);
        readInt(root, "starting_stack", rs.startingStack);

        if (const json* deck = readObject(root, "deck")) {
            ObjectReader d(*deck, "deck.", res.warnings);
            readInt(d, "size", rs.deckSize);
            readInt(d, "jokers", rs.jokers);
            d.warnUnknown();
        }
        if (const json* hr = readObject(root, "hand_rankings")) {
            ObjectReader h(*hr, "hand_rankings.", res.warnings);
            if (const json* ord = h.get("order")) {
                if (!ord->is_array()) throw ParseError{"\"hand_rankings.order\" must be an array"};
                rs.order.clear();
                for (const json& e : *ord) {
                    if (!e.is_string()) throw ParseError{"\"hand_rankings.order\" entries must be strings"};
                    auto t = handTypeFromKey(e.get<std::string>());
                    if (!t) res.warnings.push_back("Unknown hand type " + quote(e.get<std::string>()) + " ignored");
                    else rs.order.push_back(*t);
                }
            }
            readBool(h, "wheel_straight", rs.wheelStraight);
            h.warnUnknown();
        }
        if (const json* b = readObject(root, "blinds")) {
            ObjectReader o(*b, "blinds.", res.warnings);
            readInt(o, "small", rs.smallBlind);
            readInt(o, "big", rs.bigBlind);
            readInt(o, "ante", rs.ante);
            o.warnUnknown();
        }
        if (const json* b = readObject(root, "betting")) {
            ObjectReader o(*b, "betting.", res.warnings);
            readEnum(o, "structure", kBetting, rs.betting);
            readInt(o, "max_raises", rs.maxRaises);
            o.warnUnknown();
        }
        root.warnUnknown();
    } catch (const ParseError& e) {
        res.error = e.message;
        res.warnings.clear();
        return res;
    }

    auto fixes = sanitize(rs);
    res.warnings.insert(res.warnings.end(), fixes.begin(), fixes.end());
    res.rules = std::move(rs);
    return res;
}

LoadResult loadRulesFile(const std::string& path) {
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) { LoadResult r; r.error = "Cannot open " + path; return r; }
    std::stringstream ss;
    ss << in.rdbuf();
    return rulesFromJsonText(ss.str());
}

std::string rulesToJsonText(const RuleSet& r) {
    json order = json::array();
    for (HandType t : r.order) order.push_back(std::string(handTypeKey(t)));
    json doc = {
        {"name", r.name},
        {"deck", {{"size", r.deckSize}, {"jokers", r.jokers}}},
        {"wild", std::string(wildModeKey(r.wild))},
        {"hand_rankings", {{"order", order}, {"wheel_straight", r.wheelStraight}}},
        {"blinds", {{"small", r.smallBlind}, {"big", r.bigBlind}, {"ante", r.ante}}},
        {"starting_stack", r.startingStack},
        {"betting", {{"structure", std::string(bettingKey(r.betting))}, {"max_raises", r.maxRaises}}},
    };
    return doc.dump(2) + "\n";
}

bool saveRulesFile(const RuleSet& rules, const std::string& path, std::string* error) {
    std::ofstream out(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out) { if (error) *error = "Cannot write " + path; return false; }
    out << rulesToJsonText(rules);
    if (!out) { if (error) *error = "Write failed for " + path; return false; }
    return true;
}

}  // namespace pf
