#include "test_main.hpp"
#include "RefEvaluator.hpp"
#include "core/Evaluator.hpp"
#include "core/Rng.hpp"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using namespace pf;

namespace {

CardMask hand(const std::string& text) {
    CardMask m = 0;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        auto c = parseCard(tok);
        if (!c) { std::cerr << "  bad card in test: " << tok << "\n"; continue; }
        m |= bit(*c);
    }
    return m;
}

std::vector<CardId> cardsOf(CardMask m) {
    std::vector<CardId> out;
    for (int i = 0; i < 64; ++i) if (m >> i & 1) out.push_back(static_cast<CardId>(i));
    return out;
}

std::vector<CardId> deckFor(const RuleSet& r) {
    std::vector<CardId> deck;
    for (int s = 0; s < kSuits; ++s)
        for (int k = r.minRank(); k < kRanks; ++k) deck.push_back(makeCard(s, k));
    return deck;
}

CardMask randomHand(Rng& rng, const std::vector<CardId>& deck, int n) {
    CardMask m = 0;
    while (popcount(m) < n) m |= bit(deck[rng.below(static_cast<std::uint32_t>(deck.size()))]);
    return m;
}

// Compares the evaluator against the brute-force reference on random hands.
int crossCheck(const RuleSet& rules, int hands, std::uint64_t seed) {
    Evaluator ev(rules);
    Rng rng(seed);
    const auto deck = deckFor(rules);
    int mismatches = 0;
    for (int i = 0; i < hands; ++i) {
        const int n = 5 + static_cast<int>(rng.below(3));  // 5, 6 or 7 cards
        const CardMask m = randomHand(rng, deck, n);
        const HandScore got = ev.evaluate(m);
        const HandScore want = pftest::refScore(rules, m);
        if (got != want && ++mismatches <= 3) {
            std::cerr << "  mismatch on";
            for (auto c : cardsOf(m)) std::cerr << " " << cardName(c);
            std::cerr << std::hex << ": got 0x" << got << " want 0x" << want << std::dec << "\n";
        }
    }
    return mismatches;
}

HandType cat(const Evaluator& ev, const std::string& h) { return scoreCategory(ev.evaluate(hand(h))); }

}  // namespace

PF_TEST(evaluator_categories_standard) {
    Evaluator ev{RuleSet{}};
    CHECK(ev.mode() == Evaluator::Mode::Standard);
    CHECK(cat(ev, "As Ks Qs Js Ts 2d 3c") == HandType::StraightFlush);
    CHECK(cat(ev, "Ah Ad Ac As Kd 2c 3c") == HandType::FourOfAKind);
    CHECK(cat(ev, "Ah Ad Ac Kd Ks 2c 3h") == HandType::FullHouse);
    CHECK(cat(ev, "Ah Ad Ac Kd Ks Kc 3h") == HandType::FullHouse);  // two sets
    CHECK(cat(ev, "2h 7h 9h Jh Kh Ad Ac") == HandType::Flush);
    CHECK(cat(ev, "9c Td Jh Qs Kd 2c 2d") == HandType::Straight);
    CHECK(cat(ev, "7c 7d 7h Ks 2d 4c 9h") == HandType::ThreeOfAKind);
    CHECK(cat(ev, "7c 7d Kh Ks 2d 2c 9h") == HandType::TwoPair);
    CHECK(cat(ev, "7c 7d Kh 5s 2d 3c 9h") == HandType::Pair);
    CHECK(cat(ev, "7c 8d Kh 5s 2d 3c 9h") == HandType::HighCard);
}

PF_TEST(evaluator_ordering_and_kickers) {
    Evaluator ev{RuleSet{}};
    auto s = [&](const std::string& h) { return ev.evaluate(hand(h)); };
    // wheel is the lowest straight; steel wheel is the lowest straight flush
    CHECK(s("Ah 2d 3c 4s 5h") < s("2d 3c 4s 5h 6d"));
    CHECK(scoreCategory(s("Ah 2d 3c 4s 5h")) == HandType::Straight);
    CHECK(s("Ah 2h 3h 4h 5h") < s("2h 3h 4h 5h 6h"));
    CHECK(scoreCategory(s("Qd Kd Ad 2d 3d")) == HandType::Flush);  // no wrap-around
    // kickers
    CHECK(s("Ah Ad Kc 9s 4h") > s("Ah Ad Qc Js Th"));
    CHECK(s("Ah Ad Kc 9s 4h") > s("Ah Ad Kc 9s 3h"));
    CHECK(s("Kh Kd 5c 5s Ah") > s("Kh Kd 5c 5s Qh"));
    CHECK(s("Kh Kd 5c 5s 2h 2d Ah") == s("Kh Kd 5c 5s As"));  // third pair can't play
    CHECK(s("Kh Kd 5c 5s 2h 2d 3h") < s("Kh Kd 5c 5s 4h"));
    CHECK(s("Kh Kd 5c 5s 4h 4d 3h") == s("Kh Kd 5c 5s 4c"));  // kicker from third pair
    CHECK(s("7h 7d 7c Ks 2h") > s("7h 7d 7c Qs Jh"));
    CHECK(s("7h 7d 7c 2s 2h") > s("6h 6d 6c As Ah"));
    CHECK(s("7h 7d 7c 2s 2h Ac Ad") == s("7h 7d 7c Ac Ad"));
    CHECK(s("9h 9d 9c 9s 2h") < s("9h 9d 9c 9s 3h"));
    CHECK(s("Ah 9h 8h 4h 3h") > s("Kh Qh Jh 9h 8h"));
    // identical ranks in different suits tie
    CHECK(s("Ah Kd Qc Js 9h") == s("Ad Kc Qs Jh 9c"));
    CHECK(s("As Ks") > s("As Qs"));  // fewer than 5 cards still compare
    CHECK(scoreCategory(s("As Ad")) == HandType::Pair);
}

PF_TEST(evaluator_five_card_distribution) {
    // Exact counts over all C(52,5) = 2,598,960 hands.
    Evaluator ev{RuleSet{}};
    std::array<std::uint64_t, kHandTypeCount> n{};
    for (int a = 0; a < 52; ++a)
        for (int b = a + 1; b < 52; ++b)
            for (int c = b + 1; c < 52; ++c)
                for (int d = c + 1; d < 52; ++d)
                    for (int e = d + 1; e < 52; ++e) {
                        const CardMask m = CardMask{1} << a | CardMask{1} << b | CardMask{1} << c |
                                           CardMask{1} << d | CardMask{1} << e;
                        ++n[static_cast<std::size_t>(scoreCategory(ev.evaluate(m)))];
                    }
    CHECK_EQ(n[0], 1302540u);
    CHECK_EQ(n[1], 1098240u);
    CHECK_EQ(n[2], 123552u);
    CHECK_EQ(n[3], 54912u);
    CHECK_EQ(n[4], 10200u);
    CHECK_EQ(n[5], 5108u);
    CHECK_EQ(n[6], 3744u);
    CHECK_EQ(n[7], 624u);
    CHECK_EQ(n[8], 40u);
}

PF_TEST(evaluator_matches_reference_standard) {
    CHECK_EQ(crossCheck(RuleSet{}, 40000, 11), 0);
}

PF_TEST(evaluator_short_deck) {
    RuleSet r;
    r.deckSize = 36;
    r.order = shortDeckOrder();
    CHECK(sanitize(r).empty());
    Evaluator ev(r);
    CHECK(ev.mode() == Evaluator::Mode::Monotone);
    auto s = [&](const std::string& h) { return ev.evaluate(hand(h)); };
    CHECK(scoreCategory(s("Ah 6d 7c 8s 9h")) == HandType::Straight);  // A-6-7-8-9 wheel
    CHECK(s("Ah 6d 7c 8s 9h") < s("6d 7c 8s 9h Td"));
    CHECK(s("Ah 6h 7h 8h 9h") < s("6h 7h 8h 9h Th"));
    CHECK(scoreCategory(s("Ah 6h 7h 8h 9h")) == HandType::StraightFlush);
    CHECK(s("6h 8h Th Qh Ah") > s("Kd Kc Ks Ad Ac"));  // flush beats full house
    CHECK(s("6h 6d 6c Qs Ah") > s("9h Td Jc Qs Kd"));  // trips beat straight
    CHECK(scoreCategory(s("Ah Ad Ac Kd Ks 9h 9d")) == HandType::FullHouse);
    CHECK(crossCheck(r, 40000, 12) == 0);
}

PF_TEST(evaluator_disabled_categories_fall_through) {
    RuleSet noFlush;
    noFlush.order = {HandType::HighCard, HandType::Pair, HandType::TwoPair, HandType::ThreeOfAKind,
                     HandType::Straight, HandType::FullHouse, HandType::FourOfAKind, HandType::StraightFlush};
    Evaluator ev(noFlush);
    CHECK(scoreCategory(ev.evaluate(hand("2h 7h 9h Jh Kh Kd 3c"))) == HandType::Pair);
    CHECK(scoreCategory(ev.evaluate(hand("2h 7h 9h Jh Kh 4d 3c"))) == HandType::HighCard);

    RuleSet noTwoPair;
    noTwoPair.order = {HandType::HighCard, HandType::Pair, HandType::ThreeOfAKind, HandType::Straight,
                       HandType::Flush, HandType::FullHouse, HandType::FourOfAKind, HandType::StraightFlush};
    Evaluator ev2(noTwoPair);
    CHECK(ev2.mode() == Evaluator::Mode::General);
    // Two pair counts as a pair; the second pair plays as kickers.
    CHECK(ev2.evaluate(hand("Ah Ad Kc Ks 2h")) > ev2.evaluate(hand("Ah Ad Kc Qs Jh")));
    CHECK(crossCheck(noFlush, 10000, 13) == 0);
    CHECK(crossCheck(noTwoPair, 10000, 14) == 0);
}

PF_TEST(evaluator_matches_reference_random_rules) {
    // Random ranking orders, disabled types, deck sizes and wheel settings.
    Rng rng(2024);
    int bad = 0;
    for (int trial = 0; trial < 40; ++trial) {
        RuleSet r;
        r.deckSize = 24 + 4 * static_cast<int>(rng.below(8));
        r.wheelStraight = rng.below(2) == 0;
        std::vector<HandType> types = standardOrder();
        for (std::size_t i = types.size() - 1; i > 1; --i)
            std::swap(types[i], types[1 + rng.below(static_cast<std::uint32_t>(i))]);
        r.order.clear();
        for (HandType t : types)
            if (t == HandType::HighCard || rng.below(10) >= 3) r.order.push_back(t);
        sanitize(r);
        bad += crossCheck(r, 3000, 100 + static_cast<std::uint64_t>(trial));
    }
    CHECK_EQ(bad, 0);
}
