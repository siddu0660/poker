#include "test_main.hpp"
#include "core/Evaluator.hpp"
#include "engine/Simulator.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace pf;

namespace {

CardId c(const char* s) { return *parseCard(s); }

Scenario scenario(const RuleSet& r, std::vector<Seat> seats, std::vector<CardId> board = {}) {
    Scenario s;
    s.evaluator = std::make_shared<const Evaluator>(r);
    s.seats = std::move(seats);
    s.board = std::move(board);
    s.seed = 1;
    return s;
}

Seat known(const char* a, const char* b) { return Seat{true, {c(a), c(b)}}; }
Seat unknownSeat() { return Seat{true, {kNoCard, kNoCard}}; }

struct Counts {
    std::uint64_t deals = 0, heroWins = 0, heroTies = 0, oppWins = 0;
};

// Independent brute force for heads-up: hero known, opponent random or known,
// loops written out by hand (no shared code with the enumerator).
Counts bruteForceHeadsUp(const Scenario& s) {
    const Evaluator& ev = *s.evaluator;
    CardMask used = 0;
    for (CardId x : s.board) used |= bit(x);
    for (const Seat& seat : s.seats)
        for (CardId x : seat.hole) if (x != kNoCard) used |= bit(x);
    std::vector<CardId> left;
    for (CardId x : deckFor(ev.rules())) if (!(used & bit(x))) left.push_back(x);
    const int n = static_cast<int>(left.size());
    const int missing = 5 - static_cast<int>(s.board.size());
    CardMask boardKnown = 0;
    for (CardId x : s.board) boardKnown |= bit(x);
    const CardMask hero = bit(s.seats[0].hole[0]) | bit(s.seats[0].hole[1]);
    const bool oppRandom = s.seats[1].hole[0] == kNoCard;

    Counts out;
    auto showdown = [&](CardMask opp, CardMask board) {
        const HandScore a = ev.evaluate(board | hero), b = ev.evaluate(board | opp);
        ++out.deals;
        if (a > b) ++out.heroWins;
        else if (a == b) ++out.heroTies;
        else ++out.oppWins;
    };
    // Every way to finish the board from `rest`, written as plain recursion.
    std::function<void(const std::vector<CardId>&, std::size_t, int, CardMask, CardMask)> finish =
        [&](const std::vector<CardId>& rest, std::size_t from, int need, CardMask board, CardMask opp) {
            if (need == 0) { showdown(opp, board); return; }
            for (std::size_t i = from; i < rest.size(); ++i) finish(rest, i + 1, need - 1, board | bit(rest[i]), opp);
        };
    auto boards = [&](CardMask opp, CardMask taken) {
        std::vector<CardId> rest;
        for (CardId x : left) if (!(taken & bit(x))) rest.push_back(x);
        finish(rest, 0, missing, boardKnown, opp);
    };
    if (!oppRandom) {
        const CardMask opp = bit(s.seats[1].hole[0]) | bit(s.seats[1].hole[1]);
        boards(opp, 0);
    } else {
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                const CardMask opp = bit(left[static_cast<std::size_t>(i)]) | bit(left[static_cast<std::size_t>(j)]);
                boards(opp, opp);
            }
    }
    return out;
}

void checkMatches(const Scenario& s, const Tally& t) {
    const Counts b = bruteForceHeadsUp(s);
    CHECK_EQ(t.runs, b.deals);
    CHECK_EQ(t.seats[0].wins, b.heroWins);
    CHECK_EQ(t.seats[0].ties, b.heroTies);
    CHECK_EQ(t.seats[1].wins, b.oppWins);
    CHECK_EQ(t.seats[0].equity + t.seats[1].equity, t.runs * kEquityUnits);
}

}  // namespace

PF_TEST(exact_counts_deals) {
    const RuleSet r;
    CHECK(countDeals(scenario(r, {known("As", "Ah"), known("Kd", "Kc")}), 10'000'000) == std::optional<std::uint64_t>{1712304});
    CHECK(countDeals(scenario(r, {known("As", "Ah"), unknownSeat()}, {c("2c"), c("7d"), c("9h")}), 10'000'000) ==
          std::optional<std::uint64_t>{1081u * 990u});  // C(47,2) opponent hands x C(45,2) boards
    CHECK(countDeals(scenario(r, {known("As", "Ah"), known("Kd", "Kc")},
                              {c("2c"), c("7d"), c("9h"), c("Js"), c("3s")}), 10) == std::optional<std::uint64_t>{1});
    CHECK(!countDeals(scenario(r, {known("As", "Ah"), unknownSeat(), unknownSeat(), unknownSeat()}), 10'000'000).has_value());
    // A folded seat with unknown cards is never dealt; its known cards are dead.
    Scenario f = scenario(r, {known("As", "Ah"), unknownSeat(), Seat{false, {kNoCard, kNoCard}}}, {c("2c"), c("7d"), c("9h"), c("Js")});
    CHECK(countDeals(f, 10'000'000) == std::optional<std::uint64_t>{1035u * 44u});  // C(46,2) x 44
    f.seats[2].hole = {c("Qs"), c("Qh")};
    CHECK(countDeals(f, 10'000'000) == std::optional<std::uint64_t>{946u * 42u});  // C(44,2) x 42
    Scenario bad = scenario(r, {known("As", "Ah"), known("As", "Kc")});
    CHECK(!countDeals(bad, 10'000'000).has_value());
}

PF_TEST(exact_matches_brute_force_preflop) {
    const Scenario s = scenario(RuleSet{}, {known("As", "Ah"), known("Kd", "Kc")});
    const Tally t = enumerateExact(s, 0);
    checkMatches(s, t);
    // 81.06% win and 0.38% tie, as published for AsAh vs KdKc.
    CHECK(std::abs(static_cast<double>(t.seats[0].wins) / static_cast<double>(t.runs) - 0.8106) < 0.0001);
}

PF_TEST(exact_matches_brute_force_with_random_opponent) {
    const RuleSet r;
    const Scenario turn = scenario(r, {known("Ah", "Kh"), unknownSeat()}, {c("Qh"), c("Jh"), c("2c"), c("9d")});
    checkMatches(turn, enumerateExact(turn, 0));
    const Scenario flop = scenario(r, {known("8c", "9d"), unknownSeat()}, {c("6h"), c("7s"), c("Kd")});
    checkMatches(flop, enumerateExact(flop, 0));

    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    const Scenario shortDeck = scenario(sd, {known("As", "Ks"), unknownSeat()}, {c("Qd"), c("Tc"), c("6h")});
    checkMatches(shortDeck, enumerateExact(shortDeck, 0));
}

PF_TEST(exact_is_the_same_for_any_thread_count) {
    const Scenario s = scenario(RuleSet{}, {known("Ah", "Kh"), unknownSeat(), unknownSeat()},
                                {c("Qh"), c("Jh"), c("2c"), c("9d"), c("3s")});
    const Tally one = enumerateExact(s, 1);
    CHECK(one == enumerateExact(s, 4));
    CHECK(one == enumerateExact(s, 3));
    CHECK_EQ(one.runs, *countDeals(s, 100'000'000));
    std::uint64_t equity = 0;
    for (const auto& st : one.seats) equity += st.equity;
    CHECK_EQ(equity, one.runs * kEquityUnits);
    for (int seat = 0; seat < 3; ++seat) {
        std::uint64_t hands = 0;
        for (auto h : one.seats[static_cast<std::size_t>(seat)].hands) hands += h;
        CHECK_EQ(hands, one.runs);
    }
}

PF_TEST(exact_single_deal_and_wild_cards) {
    const Scenario river = scenario(RuleSet{}, {known("2s", "3h"), known("4d", "5c")},
                                    {c("As"), c("Ks"), c("Qs"), c("Js"), c("Ts")});
    const Tally t = enumerateExact(river, 0);
    CHECK_EQ(t.runs, 1u);
    CHECK_EQ(t.seats[0].ties, 1u);
    CHECK_EQ(t.seats[0].equity, static_cast<std::uint64_t>(kEquityUnits / 2));

    RuleSet w;
    w.wild = WildMode::Jokers;
    w.jokers = 2;
    w.order.push_back(HandType::FiveOfAKind);
    sanitize(w);
    // Three aces plus a joker on the river is four aces; five would need both jokers.
    const Scenario wild = scenario(w, {known("Ah", "Ad"), unknownSeat()}, {c("Ac"), c("7d"), c("2h"), c("9s")});
    const Tally tw = enumerateExact(wild, 0);
    CHECK_EQ(tw.runs, *countDeals(wild, 100'000'000));
    CHECK(tw.seats[0].hands[static_cast<std::size_t>(HandType::FourOfAKind)] > 0);
    CHECK_EQ(tw.seats[0].hands[static_cast<std::size_t>(HandType::FiveOfAKind)], 0u);
    checkMatches(wild, tw);
}

// No board cards left to deal: only hole cards are enumerated, and the board must
// still count (this shape once scored hands without it).
PF_TEST(exact_river_with_two_unknown_hands_matches_brute_force) {
    const Scenario s = scenario(RuleSet{}, {known("Ah", "Kh"), unknownSeat(), unknownSeat()},
                                {c("Qh"), c("Jh"), c("2c"), c("9d"), c("3s")});
    const Tally t = enumerateExact(s, 0);
    const Evaluator& ev = *s.evaluator;
    CardMask board = 0, used = bit(c("Ah")) | bit(c("Kh"));
    for (CardId x : s.board) { board |= bit(x); used |= bit(x); }
    std::vector<CardId> left;
    for (CardId x : deckFor(RuleSet{})) if (!(used & bit(x))) left.push_back(x);
    const HandScore hero = ev.evaluate(board | bit(c("Ah")) | bit(c("Kh")));
    std::uint64_t deals = 0, heroEquity = 0;
    const std::size_t n = left.size();
    for (std::size_t a = 0; a < n; ++a)
        for (std::size_t b = a + 1; b < n; ++b)
            for (std::size_t x = 0; x < n; ++x)
                for (std::size_t y = x + 1; y < n; ++y) {
                    if (x == a || x == b || y == a || y == b) continue;
                    const HandScore p = ev.evaluate(board | bit(left[a]) | bit(left[b]));
                    const HandScore q = ev.evaluate(board | bit(left[x]) | bit(left[y]));
                    const HandScore best = std::max({hero, p, q});
                    const int winners = (hero == best) + (p == best) + (q == best);
                    if (hero == best) heroEquity += static_cast<std::uint64_t>(kEquityUnits / winners);
                    ++deals;
                }
    CHECK_EQ(t.runs, deals);
    CHECK_EQ(t.seats[0].equity, heroEquity);
}

PF_TEST(exact_agrees_with_monte_carlo) {
    const Scenario s = scenario(RuleSet{}, {known("Ah", "Kh"), unknownSeat(), unknownSeat()},
                                {c("Qh"), c("Jh"), c("2c"), c("9d"), c("3s")});
    CHECK(countDeals(s, 5'000'000) == std::optional<std::uint64_t>{990u * 903u});
    const Tally exact = enumerateExact(s, 0);
    const Tally mc = runChunks(s, 0, 800, 0);
    for (int seat = 0; seat < 3; ++seat) {
        const auto& e = exact.seats[static_cast<std::size_t>(seat)];
        const auto& m = mc.seats[static_cast<std::size_t>(seat)];
        const double pe = static_cast<double>(e.equity) / static_cast<double>(exact.runs * kEquityUnits);
        const double pm = static_cast<double>(m.equity) / static_cast<double>(mc.runs * kEquityUnits);
        CHECK(std::abs(pe - pm) < 4.0 * std::sqrt(pe * (1 - pe) / static_cast<double>(mc.runs)));
    }
}
