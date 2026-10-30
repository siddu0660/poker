#include "test_main.hpp"
#include "core/Rules.hpp"

#include <algorithm>
#include <string>

using namespace pf;

namespace {
bool hasWarning(const LoadResult& r, const std::string& needle) {
    return std::any_of(r.warnings.begin(), r.warnings.end(),
                       [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}
}  // namespace

PF_TEST(rules_default_is_holdem_and_clean) {
    RuleSet r;
    CHECK(sanitize(r).empty());
    CHECK_EQ(r.deckSize, 52);
    CHECK(r.order == standardOrder());
    CHECK(!r.isEnabled(HandType::FiveOfAKind));
}

PF_TEST(rules_shipped_presets_load_without_warnings) {
    for (const char* f : {"texas_holdem", "short_deck_36", "deuces_wild", "jokers_wild"}) {
        auto res = loadRulesFile(std::string(PF_RULES_DIR) + "/" + f + ".json");
        CHECK(res.rules.has_value());
        CHECK(res.warnings.empty());
        if (!res.rules) std::cerr << "  " << f << ": " << res.error << "\n";
    }
    auto sd = loadRulesFile(std::string(PF_RULES_DIR) + "/short_deck_36.json");
    CHECK(sd.rules && sd.rules->deckSize == 36 && sd.rules->minRank() == 4);
    CHECK(sd.rules && sd.rules->order == shortDeckOrder());
    auto dw = loadRulesFile(std::string(PF_RULES_DIR) + "/deuces_wild.json");
    CHECK(dw.rules && dw.rules->wild == WildMode::Deuces && dw.rules->isEnabled(HandType::FiveOfAKind));
}

PF_TEST(rules_json_roundtrip) {
    for (const char* f : {"texas_holdem", "short_deck_36", "deuces_wild", "jokers_wild"}) {
        auto a = loadRulesFile(std::string(PF_RULES_DIR) + "/" + f + ".json");
        CHECK(a.rules.has_value());
        if (!a.rules) continue;
        auto b = rulesFromJsonText(rulesToJsonText(*a.rules));
        CHECK(b.rules.has_value());
        CHECK(b.warnings.empty());
        if (b.rules) CHECK(*a.rules == *b.rules);
    }
}

PF_TEST(rules_missing_keys_use_defaults) {
    auto r = rulesFromJsonText("{}");
    CHECK(r.rules.has_value());
    if (r.rules) CHECK(*r.rules == RuleSet{});
}

PF_TEST(rules_invalid_json_and_types_are_errors) {
    CHECK(!rulesFromJsonText("{not json").rules);
    CHECK(!rulesFromJsonText("[1,2]").rules);
    CHECK(!rulesFromJsonText(R"({"deck":{"size":"big"}})").rules);
    CHECK(!rulesFromJsonText(R"({"deck":5})").rules);
    CHECK(!rulesFromJsonText(R"({"wild":"aces"})").rules);
    CHECK(!rulesFromJsonText(R"({"betting":{"structure":"free_for_all"}})").rules);
    CHECK(!rulesFromJsonText(R"({"blinds":{"small":1.5}})").rules);
    auto e = rulesFromJsonText(R"({"deck":{"size":"big"}})");
    CHECK(e.error.find("deck.size") != std::string::npos);
}

PF_TEST(rules_unknown_keys_warn_but_load) {
    auto r = rulesFromJsonText(R"({"colour":"red","deck":{"size":52,"suits":4},
                                   "hand_rankings":{"order":["pair","royal_flush"]}})");
    CHECK(r.rules.has_value());
    CHECK(hasWarning(r, "colour"));
    CHECK(hasWarning(r, "deck.suits"));
    CHECK(hasWarning(r, "royal_flush"));
}

PF_TEST(sanitize_deck_size_snaps_to_multiple_of_four) {
    struct Case { int in, out; };
    for (Case c : {Case{10, 24}, Case{24, 24}, Case{25, 24}, Case{26, 28}, Case{37, 36},
                   Case{38, 40}, Case{51, 52}, Case{52, 52}, Case{99, 52}}) {
        RuleSet r; r.deckSize = c.in;
        sanitize(r);
        CHECK_EQ(r.deckSize, c.out);
        CHECK(r.deckSize % 4 == 0);
    }
}

PF_TEST(sanitize_hand_order) {
    RuleSet r;
    r.order = {HandType::Flush, HandType::Pair, HandType::Pair, HandType::HighCard,
               HandType::FiveOfAKind};
    auto w = sanitize(r);
    CHECK(!w.empty());
    CHECK(r.order.front() == HandType::HighCard);
    CHECK_EQ(std::count(r.order.begin(), r.order.end(), HandType::Pair), 1);
    CHECK(!r.isEnabled(HandType::FiveOfAKind));  // no wilds

    RuleSet wild; wild.wild = WildMode::Deuces;
    wild.order = {HandType::HighCard, HandType::Pair, HandType::FiveOfAKind};
    CHECK(sanitize(wild).empty());
    CHECK(wild.isEnabled(HandType::FiveOfAKind));

    RuleSet empty; empty.order.clear();
    sanitize(empty);
    CHECK(empty.order == std::vector<HandType>{HandType::HighCard});
}

PF_TEST(sanitize_money_and_jokers) {
    RuleSet r;
    r.bigBlind = 0; r.smallBlind = 50; r.ante = -5; r.startingStack = 0;
    r.jokers = 7; r.maxRaises = 0;
    sanitize(r);
    CHECK_EQ(r.bigBlind, 1);
    CHECK_EQ(r.smallBlind, 1);
    CHECK_EQ(r.ante, 0);
    CHECK(r.startingStack >= r.bigBlind);
    CHECK_EQ(r.jokers, 2);
    CHECK_EQ(r.maxRaises, 1);

    RuleSet jw; jw.wild = WildMode::Jokers; jw.jokers = 0;
    CHECK(!sanitize(jw).empty());
    CHECK_EQ(jw.jokers, 1);
}

PF_TEST(rules_label_tables_are_complete_and_unique) {
    const auto& t = handTypeTable();
    for (std::size_t i = 0; i < t.size(); ++i) {
        CHECK_EQ(static_cast<std::size_t>(t[i].type), i);
        CHECK(handTypeFromKey(t[i].key) == t[i].type);
        for (std::size_t j = i + 1; j < t.size(); ++j) {
            CHECK(t[i].key != t[j].key);
            CHECK(t[i].display != t[j].display);
        }
    }
}
