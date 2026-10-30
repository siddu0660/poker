#include "test_main.hpp"
#include "core/Betting.hpp"
#include "engine/TableState.hpp"

#include <algorithm>
#include <memory>
#include <set>

using namespace pf;

namespace {

std::shared_ptr<const Evaluator> evaluatorFor(const RuleSet& r) { return std::make_shared<const Evaluator>(r); }

std::set<CardId> allCards(const TableState& t) {
    std::set<CardId> s(t.fullBoard().begin(), t.fullBoard().end());
    for (int i = 0; i < t.seatCount(); ++i) {
        s.insert(t.seat(i).hole[0]);
        s.insert(t.seat(i).hole[1]);
    }
    return s;
}

}  // namespace

PF_TEST(betting_no_limit_pot_limit_fixed_limit) {
    RuleSet r;  // blinds 10/20
    BettingContext utg;
    utg.pot = 30; utg.currentBet = 20; utg.playerBet = 0; utg.stack = 2000; utg.lastRaise = 20;

    auto nl = raiseRange(r, utg);
    CHECK(nl.canRaise);
    CHECK_EQ(nl.minTo, 40);
    CHECK_EQ(nl.maxTo, 2000);

    r.betting = BettingStructure::PotLimit;
    auto pl = raiseRange(r, utg);
    CHECK_EQ(pl.minTo, 40);
    CHECK_EQ(pl.maxTo, 70);  // call 20, then raise by the pot of 50

    r.betting = BettingStructure::FixedLimit;
    auto fl = raiseRange(r, utg);
    CHECK_EQ(fl.minTo, 40);
    CHECK_EQ(fl.maxTo, 40);
    utg.street = Street::Turn;
    utg.currentBet = 0; utg.lastRaise = 0;
    CHECK_EQ(raiseRange(r, utg).minTo, 40);  // big bet on turn and river is 2 x BB
    utg.street = Street::Preflop; utg.currentBet = 20; utg.lastRaise = 20;
    utg.raisesMade = r.maxRaises;
    CHECK(!raiseRange(r, utg).canRaise);
}

PF_TEST(betting_short_stacks_cap_at_all_in) {
    RuleSet r;
    BettingContext c;
    c.pot = 30; c.currentBet = 20; c.stack = 15; c.lastRaise = 20;
    CHECK(!raiseRange(r, c).canRaise);  // cannot even cover the call
    CHECK_EQ(toCall(c), 15);

    c.stack = 30;
    auto range = raiseRange(r, c);
    CHECK(range.canRaise);
    CHECK_EQ(range.minTo, 30);  // the minimum raise is more than the stack: all-in only
    CHECK_EQ(range.maxTo, 30);
}

PF_TEST(table_deals_unique_cards_and_posts_blinds) {
    TableState t(evaluatorFor(RuleSet{}), 6, 7);
    CHECK_EQ(allCards(t).size(), 17u);  // 5 board + 6 x 2 hole, no repeats
    CHECK_EQ(t.dealer(), 0);
    CHECK_EQ(t.smallBlindSeat(), 1);
    CHECK_EQ(t.bigBlindSeat(), 2);
    CHECK_EQ(t.seat(1).bet, 10);
    CHECK_EQ(t.seat(1).stack, 1990);
    CHECK_EQ(t.seat(2).bet, 20);
    CHECK_EQ(t.seat(2).stack, 1980);
    CHECK_EQ(t.seat(3).stack, 2000);
    CHECK_EQ(t.pot(), 30);
    CHECK(t.street() == Street::Preflop);
    CHECK(t.board().empty());
    CHECK(t.seat(0).known);   // the hero sees their own hand
    CHECK(!t.seat(1).known);  // opponents start as random hands

    t.newHand();
    CHECK_EQ(t.handNumber(), 2);
    CHECK_EQ(t.dealer(), 1);  // the button moves
    CHECK_EQ(t.smallBlindSeat(), 2);
    CHECK_EQ(t.seat(1).bet, 0);  // old blinds are gone
    CHECK_EQ(t.pot(), 30);
}

PF_TEST(table_heads_up_dealer_posts_small_blind) {
    TableState t(evaluatorFor(RuleSet{}), 2, 1);
    CHECK_EQ(t.dealer(), 0);
    CHECK_EQ(t.smallBlindSeat(), 0);
    CHECK_EQ(t.bigBlindSeat(), 1);
    t.newHand();
    CHECK_EQ(t.dealer(), 1);
    CHECK_EQ(t.smallBlindSeat(), 1);
    CHECK_EQ(t.bigBlindSeat(), 0);
}

PF_TEST(table_antes_and_short_deck_deal) {
    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    sd.ante = 20;
    TableState t(evaluatorFor(sd), 6, 3);
    CHECK_EQ(t.pot(), 6 * 20 + 10 + 20);
    CHECK_EQ(t.seat(3).stack, 2000 - 20);
    for (CardId c : allCards(t)) CHECK(rankOf(c) >= 4);  // ranks 6..A only
}

PF_TEST(table_streets_and_equity_history) {
    TableState t(evaluatorFor(RuleSet{}), 3, 1);
    CHECK(t.setFolded(2, true));
    t.recordEquity({0.6, 0.3, 0.1});
    t.recordEquity({0.55, 0.45, 0.0});  // same street: overwrites
    CHECK_EQ(t.equityHistory().size(), 1u);
    CHECK(t.nextStreet({0.5, 0.5, 0.0}));
    CHECK(t.street() == Street::Flop);
    CHECK_EQ(t.board().size(), 3u);
    CHECK_EQ(t.equityHistory().size(), 1u);  // nextStreet files the equity under the street being left
    CHECK(t.equityHistory().back().street == Street::Preflop);
    CHECK_EQ(t.equityHistory().back().equity[2], kNoEquity);  // folded seats carry no equity
    CHECK_EQ(t.equityHistory().back().equity[0], 0.5);

    CHECK(t.nextStreet());
    CHECK_EQ(t.board().size(), 4u);
    CHECK(t.nextStreet());
    CHECK_EQ(t.board().size(), 5u);
    CHECK(!t.nextStreet());  // the river is the last street
    CHECK(t.street() == Street::River);

    t.recordEquity({1.0, 0.0, 0.0});
    CHECK(t.equityHistory().back().street == Street::River);
    t.newHand();
    CHECK(t.equityHistory().empty());
    CHECK(t.street() == Street::Preflop);
    CHECK(!t.seat(2).folded);
}

PF_TEST(table_fold_rules) {
    TableState t(evaluatorFor(RuleSet{}), 3, 1);
    CHECK(t.toggleFold(1));
    CHECK(t.seat(1).folded);
    CHECK_EQ(t.activeCount(), 2);
    CHECK(t.toggleFold(2));
    CHECK(!t.toggleFold(0));  // the last player cannot fold
    CHECK(!t.seat(0).folded);
    CHECK(t.toggleFold(1));   // un-folding is always fine
    CHECK(!t.seat(1).folded);
}

PF_TEST(table_scenario_hides_unknown_cards_and_future_board) {
    TableState t(evaluatorFor(RuleSet{}), 4, 2);
    t.nextStreet();  // flop
    t.setFolded(3, true);
    Scenario s = t.scenario(99);
    CHECK_EQ(s.board.size(), 3u);
    CHECK_EQ(s.seed, 99u);
    CHECK_EQ(s.seats[0].hole[0], t.seat(0).hole[0]);   // hero known
    CHECK_EQ(s.seats[1].hole[0], kNoCard);              // opponent random
    CHECK(!s.seats[3].active);
    CHECK(validateScenario(s).empty());

    t.setKnown(1, true);
    CHECK_EQ(t.scenario(1).seats[1].hole[1], t.seat(1).hole[1]);
    t.setKnown(0, false);
    CHECK(t.seat(0).known);  // the hero stays known
    CHECK_EQ(popcount(t.deadForHero()), 2);  // seat 1's known cards

    t.setHero(2);
    CHECK_EQ(t.hero(), 2);
    CHECK(t.seat(2).known);
}

PF_TEST(table_deal_is_deterministic_per_seed_and_hand) {
    TableState a(evaluatorFor(RuleSet{}), 6, 11), b(evaluatorFor(RuleSet{}), 6, 11), other(evaluatorFor(RuleSet{}), 6, 12);
    CHECK(a.fullBoard() == b.fullBoard());
    CHECK(a.seat(3).hole == b.seat(3).hole);
    CHECK(a.fullBoard() != other.fullBoard());
    const auto first = a.fullBoard();
    a.newHand();
    CHECK(a.fullBoard() != first);
}

PF_TEST(table_set_hole_keeps_every_card_unique) {
    TableState t(evaluatorFor(RuleSet{}), 6, 4);
    const CardId boardCard = t.fullBoard()[0];
    const CardId otherSeatCard = t.seat(3).hole[1];
    t.setHole(0, {boardCard, otherSeatCard});
    CHECK_EQ(t.seat(0).hole[0], boardCard);
    CHECK_EQ(t.seat(0).hole[1], otherSeatCard);
    CHECK_EQ(allCards(t).size(), 17u);
}

PF_TEST(table_seat_count_and_rules_change) {
    TableState t(evaluatorFor(RuleSet{}), 6, 1);
    t.setHero(5);
    t.setSeatCount(3);
    CHECK_EQ(t.seatCount(), 3);
    CHECK_EQ(t.hero(), 2);
    t.setSeatCount(99);
    CHECK_EQ(t.seatCount(), 6);
    t.setSeatCount(1);
    CHECK_EQ(t.seatCount(), 2);

    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    sd.startingStack = 500;
    t.setRules(evaluatorFor(sd));
    CHECK_EQ(t.seat(1).stack + t.seat(1).bet, 500);
    for (CardId c : allCards(t)) CHECK(rankOf(c) >= 4);
}

PF_TEST(table_showdown_winners) {
    TableState t(evaluatorFor(RuleSet{}), 3, 6);
    CHECK(t.showdownWinners().empty());  // not before the river
    while (t.nextStreet()) {}
    const auto winners = t.showdownWinners();
    CHECK(!winners.empty());

    // The winner list matches evaluating every seat directly.
    CardMask board = 0;
    for (CardId c : t.fullBoard()) board |= bit(c);
    HandScore best = 0;
    for (int i = 0; i < 3; ++i)
        best = std::max(best, t.evaluator().evaluate(board | bit(t.seat(i).hole[0]) | bit(t.seat(i).hole[1])));
    for (int w : winners)
        CHECK_EQ(t.evaluator().evaluate(board | bit(t.seat(w).hole[0]) | bit(t.seat(w).hole[1])), best);

    // A folded seat can never win, even with the best cards.
    for (int i = 0; i < 3; ++i) {
        if (std::find(winners.begin(), winners.end(), i) == winners.end()) continue;
        if (t.setFolded(i, true)) {
            const auto after = t.showdownWinners();
            CHECK(std::find(after.begin(), after.end(), i) == after.end());
            break;
        }
    }
}

PF_TEST(table_hero_betting_context) {
    TableState t(evaluatorFor(RuleSet{}), 6, 1);
    t.setHero(3);  // under the gun: facing the big blind
    const auto ctx = t.heroBettingContext();
    CHECK_EQ(ctx.pot, 30);
    CHECK_EQ(ctx.currentBet, 20);
    CHECK_EQ(ctx.stack, 2000);
    const auto range = raiseRange(t.evaluator().rules(), ctx);
    CHECK_EQ(range.minTo, 40);
}
