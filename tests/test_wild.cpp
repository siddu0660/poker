#include "test_main.hpp"
#include "RefEvaluator.hpp"
#include "core/Evaluator.hpp"
#include "core/Rng.hpp"

#include <sstream>
#include <string>
#include <vector>

using namespace pf;

namespace {

CardMask hand(const std::string& text) {
    CardMask m = 0;
    std::istringstream in(text);
    std::string tok;
    int jokers = 0;
    while (in >> tok) {
        auto c = parseCard(tok);
        if (!c) { std::cerr << "  bad card in test: " << tok << "\n"; continue; }
        if (isJoker(*c)) c = static_cast<CardId>(kJokerA + jokers++);
        m |= bit(*c);
    }
    return m;
}

RuleSet withWild(WildMode w) {
    RuleSet r;
    r.wild = w;
    r.jokers = w == WildMode::Jokers ? 2 : 0;
    r.order.push_back(HandType::FiveOfAKind);
    sanitize(r);
    return r;
}

std::vector<CardId> wildCards(const Evaluator& ev) {
    std::vector<CardId> out;
    for (int i = 0; i < 64; ++i) if (ev.wildMask() >> i & 1) out.push_back(static_cast<CardId>(i));
    return out;
}

// Random hands with exactly `wilds` wild cards, checked against brute force.
int crossCheckWild(const RuleSet& rules, int hands, int maxWilds, std::uint64_t seed) {
    Evaluator ev(rules);
    const auto wild = wildCards(ev);
    std::vector<CardId> naturals;
    for (int s = 0; s < kSuits; ++s)
        for (int k = rules.minRank(); k < kRanks; ++k)
            if (!(ev.wildMask() & bit(makeCard(s, k)))) naturals.push_back(makeCard(s, k));
    Rng rng(seed);
    int mismatches = 0;
    for (int i = 0; i < hands; ++i) {
        const int n = 5 + static_cast<int>(rng.below(3));
        const int maxW = std::min({maxWilds, static_cast<int>(wild.size()), n == 5 ? 4 : n == 6 ? 3 : 2});
        const int w = 1 + static_cast<int>(rng.below(static_cast<std::uint32_t>(maxW)));
        CardMask m = 0;
        while (popcount(m) < w) m |= bit(wild[rng.below(static_cast<std::uint32_t>(wild.size()))]);
        while (popcount(m) < n) m |= bit(naturals[rng.below(static_cast<std::uint32_t>(naturals.size()))]);
        const HandScore got = ev.evaluate(m);
        const HandScore want = pftest::refScore(rules, m);
        if (got != want && ++mismatches <= 3) {
            std::cerr << "  mismatch (" << wildModeDisplay(rules.wild) << ") on";
            for (int b = 0; b < 64; ++b) if (m >> b & 1) std::cerr << " " << cardName(static_cast<CardId>(b));
            std::cerr << std::hex << ": got 0x" << got << " want 0x" << want << std::dec << "\n";
        }
    }
    return mismatches;
}

}  // namespace

PF_TEST(wild_masks) {
    CHECK_EQ(popcount(Evaluator(withWild(WildMode::Deuces)).wildMask()), 4);
    CHECK_EQ(popcount(Evaluator(withWild(WildMode::Jokers)).wildMask()), 2);
    const CardMask oej = Evaluator(withWild(WildMode::OneEyedJacks)).wildMask();
    CHECK(oej == (bit(*parseCard("Js")) | bit(*parseCard("Jh"))));
    CHECK_EQ(Evaluator(RuleSet{}).wildMask(), CardMask{0});
    RuleSet shortDeuces = withWild(WildMode::Deuces);
    shortDeuces.deckSize = 36;
    CHECK_EQ(Evaluator(shortDeuces).wildMask(), CardMask{0});  // no deuces in a 36-card deck
}

PF_TEST(wild_known_hands) {
    Evaluator ev(withWild(WildMode::Deuces));
    auto s = [&](const std::string& h) { return ev.evaluate(hand(h)); };
    auto cat = [&](const std::string& h) { return scoreCategory(s(h)); };
    CHECK(cat("2c Ah Ad Ac As") == HandType::FiveOfAKind);
    CHECK(cat("2c 2d 2h 2s 7d") == HandType::FiveOfAKind);  // four deuces + a card
    CHECK(s("2c 2d 2h 2s 7d") == s("2c 2d 2h 7c 7d"));      // both are five sevens
    CHECK(cat("2c Ah Kh Qh Jh") == HandType::StraightFlush);
    CHECK(cat("2c 2d 7h 9s Kd") == HandType::ThreeOfAKind);
    CHECK(s("2c 2d 7h 9s Kd") == s("Kc Kh Kd 9s 7h"));
    CHECK(cat("2c 3d 4h 5s 7d") == HandType::Straight);  // deuce plays as the 6
    CHECK(s("2c 3d 4h 5s 7d") == s("3d 4h 5s 6c 7d"));
    CHECK(cat("2c 3d 4h 5s 9d") == HandType::Pair);      // one wild can't bridge 5 to 9
    CHECK(cat("2s Ks Qs 9s 4s") == HandType::Flush);
    CHECK(s("2s Ks Qs 9s 4s") == s("As Ks Qs 9s 4s"));   // wild fills the missing ace
    CHECK(s("2s As Qs 9s 4s") == s("As Ks Qs 9s 4s"));   // ace held: wild becomes the king
    CHECK(s("2c Ah Kd 7c 5s") == s("Ac Ah Kd 7c 5s"));   // pair of aces, natural kickers
    CHECK(cat("2c 7d 7h 9s 9d") == HandType::FullHouse);

    Evaluator jk(withWild(WildMode::Jokers));
    CHECK(scoreCategory(jk.evaluate(hand("Jk Jk Ah Ad Ac"))) == HandType::FiveOfAKind);
    CHECK(jk.evaluate(hand("Jk 9h 9d 4c 3s Kd 8h")) == jk.evaluate(hand("9c 9h 9d Kd 8h")));

    Evaluator oej(withWild(WildMode::OneEyedJacks));
    CHECK(scoreCategory(oej.evaluate(hand("Js Jd Jc 4h 5d"))) == HandType::ThreeOfAKind);  // J♠ wild
    CHECK(scoreCategory(oej.evaluate(hand("Js Jh Jd Jc 5d"))) == HandType::FourOfAKind);   // both wild
    CHECK(scoreCategory(oej.evaluate(hand("Jd Jc Qd 4h 5d"))) == HandType::Pair);         // natural jacks
}

PF_TEST(wild_five_of_a_kind_disabled_falls_to_quads) {
    RuleSet r = withWild(WildMode::Deuces);
    r.order.pop_back();  // drop Five of a Kind
    Evaluator ev(r);
    const auto s = ev.evaluate(hand("2c Ah Ad Ac As"));
    CHECK(scoreCategory(s) == HandType::FourOfAKind);
    CHECK_EQ(scoreTiebreak(s), (13u << 16) | (13u << 12));  // quad aces, wild ace kicker
}

PF_TEST(wild_dead_jokers_are_ignored) {
    RuleSet r = withWild(WildMode::Deuces);
    r.jokers = 2;
    Evaluator ev(r);
    CHECK(ev.evaluate(hand("Jk Ah Kd 7c 5s")) == ev.evaluate(hand("Ah Kd 7c 5s")));
}

// Larger samples and an exhaustive 5-card check live in pokercore_exhaustive --wild.
PF_TEST(wild_matches_reference_presets) {
    CHECK_EQ(crossCheckWild(withWild(WildMode::Deuces), 600, 4, 21), 0);
    CHECK_EQ(crossCheckWild(withWild(WildMode::Jokers), 600, 2, 22), 0);
    CHECK_EQ(crossCheckWild(withWild(WildMode::OneEyedJacks), 600, 2, 23), 0);
    RuleSet sd = withWild(WildMode::Jokers);
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    sd.order.push_back(HandType::FiveOfAKind);
    sanitize(sd);
    CHECK_EQ(crossCheckWild(sd, 600, 2, 24), 0);
}

PF_TEST(wild_matches_reference_random_rules) {
    Rng rng(4242);
    int bad = 0;
    for (int trial = 0; trial < 30; ++trial) {
        RuleSet r;
        r.wild = static_cast<WildMode>(1 + rng.below(3));
        r.jokers = r.wild == WildMode::Jokers ? 2 : 0;
        r.deckSize = r.wild == WildMode::Deuces ? 52 : 24 + 4 * static_cast<int>(rng.below(8));
        r.wheelStraight = rng.below(2) == 0;
        std::vector<HandType> types = standardOrder();
        types.push_back(HandType::FiveOfAKind);
        for (std::size_t i = types.size() - 1; i > 1; --i)
            std::swap(types[i], types[1 + rng.below(static_cast<std::uint32_t>(i))]);
        r.order.clear();
        for (HandType t : types)
            if (t == HandType::HighCard || rng.below(10) >= 3) r.order.push_back(t);
        sanitize(r);
        bad += crossCheckWild(r, 100, 4, 300 + static_cast<std::uint64_t>(trial));
    }
    CHECK_EQ(bad, 0);
}
