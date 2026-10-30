#include "test_main.hpp"
#include "core/HandAnalysis.hpp"
#include "engine/Simulator.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace pf;

namespace {

CardId c(const char* s) { return *parseCard(s); }

std::vector<CardId> cards(const std::string& text) {
    std::vector<CardId> out;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) out.push_back(c(tok.c_str()));
    return out;
}

HoleCards hole(const char* a, const char* b) { return {c(a), c(b)}; }

Evaluator holdem() { return Evaluator{RuleSet{}}; }

std::string describeHand(const Evaluator& ev, const char* a, const char* b, const std::string& board) {
    return analyzeMadeHand(ev, hole(a, b), cards(board)).description;
}

const HandTypeRow& row(const HandAnalysis& a, HandType t) {
    for (const auto& r : a.rows) if (r.type == t) return r;
    static const HandTypeRow none;
    std::cerr << "  no row for " << handTypeDisplay(t) << "\n";
    return none;
}

bool hasDraw(const HandAnalysis& a, const std::string& text) {
    return std::any_of(a.draws.begin(), a.draws.end(),
                       [&](const std::string& d) { return d.find(text) != std::string::npos; });
}

}  // namespace

PF_TEST(analysis_descriptions_every_category) {
    const Evaluator ev = holdem();
    CHECK_EQ(describeHand(ev, "Th", "Qs", "Td 7c 2d 4s 9h"), std::string("Pair of Tens, Queen kicker"));
    CHECK_EQ(describeHand(ev, "Kh", "Ts", "Kd Tc Qs 2d 3c"), std::string("Two Pair, Kings and Tens, Queen kicker"));
    CHECK_EQ(describeHand(ev, "7h", "7d", "7c Ks 2d 4c 9h"), std::string("Three of a Kind, Sevens, King kicker"));
    CHECK_EQ(describeHand(ev, "9c", "Td", "Jh Qs Kd 2c 2d"), std::string("Straight, King high"));
    CHECK_EQ(describeHand(ev, "Ah", "5d", "2c 3s 4h Kd Qs"), std::string("Straight, Five high"));
    CHECK_EQ(describeHand(ev, "Ah", "9h", "2h 7h Kh 3c 3d"), std::string("Flush, Ace high"));
    CHECK_EQ(describeHand(ev, "Ah", "Ad", "Ac Kd Ks 2c 3h"), std::string("Full House, Aces full of Kings"));
    CHECK_EQ(describeHand(ev, "Ah", "Ad", "Ac As Kd 2c 3h"), std::string("Four of a Kind, Aces, King kicker"));
    CHECK_EQ(describeHand(ev, "9h", "Th", "Jh Qh Kh 2c 3d"), std::string("Straight Flush, King high"));
    CHECK_EQ(describeHand(ev, "Ah", "Kh", "Qh Jh Th 2c 3d"), std::string("Royal Flush"));
    CHECK_EQ(describeHand(ev, "Ah", "9c", "2d 5s 7h Jc 3d"), std::string("Ace high"));
    CHECK_EQ(describeHand(ev, "Th", "Td", ""), std::string("Pocket Tens"));
    CHECK_EQ(describeHand(ev, "Qh", "Ts", ""), std::string("Queen-Ten, offsuit"));
    CHECK_EQ(describeHand(ev, "Kh", "Ah", ""), std::string("Ace-King, suited"));
    CHECK_EQ(analyzeMadeHand(ev, hole("Kh", "Ts"), cards("Kd Tc Qs 2d 3c")).label, std::string("Two Pair, Kings and Tens"));
}

PF_TEST(analysis_best_five_cards) {
    const Evaluator ev = holdem();
    const auto made = analyzeMadeHand(ev, hole("Th", "Qs"), cards("Td 7c 2d 4s 9h"));
    CHECK_EQ(made.bestMask, bit(c("Td")) | bit(c("Th")) | bit(c("Qs")) | bit(c("9h")) | bit(c("7c")));
    CHECK_EQ(popcount(made.bestMask), 5);
    // The straight uses five of the seven cards; the pair of deuces stays out.
    const auto st = analyzeMadeHand(ev, hole("9c", "Td"), cards("Jh Qs Kd 2c 2d"));
    CHECK_EQ(st.bestMask, bit(c("9c")) | bit(c("Td")) | bit(c("Jh")) | bit(c("Qs")) | bit(c("Kd")));
    CHECK(!analyzeMadeHand(ev, {kNoCard, c("As")}, {}).valid);
}

PF_TEST(analysis_wild_and_short_deck_descriptions) {
    RuleSet r;
    r.wild = WildMode::Deuces;
    r.order.push_back(HandType::FiveOfAKind);
    sanitize(r);
    const Evaluator wild(r);
    CHECK_EQ(describeHand(wild, "2c", "Ah", "Ad 7c 9s 3d Kh"), std::string("Three of a Kind, Aces, King kicker"));
    CHECK_EQ(describeHand(wild, "2c", "Ah", "Ad Ac 9s"), std::string("Four of a Kind, Aces, Nine kicker"));
    CHECK_EQ(describeHand(wild, "2c", "2d", "Ad Ac As"), std::string("Five of a Kind, Aces"));

    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    const Evaluator shortDeck(sd);
    CHECK_EQ(describeHand(shortDeck, "Ah", "6d", "7c 8s 9h Kd Qs"), std::string("Straight, Nine high"));  // A-6-7-8-9
}

PF_TEST(analysis_flush_draw_has_nine_outs) {
    const Evaluator ev = holdem();
    const auto a = analyzeHand(ev, hole("Ah", "Kh"), cards("7h 2h Jc"));
    CHECK(a.valid);
    CHECK_EQ(a.unseen, 47);
    CHECK_EQ(row(a, HandType::Flush).outs, 9);
    CHECK_EQ(row(a, HandType::Flush).needs, std::string("any heart"));
    CHECK(hasDraw(a, "Flush draw (9 outs)"));
    CHECK(!row(a, HandType::Flush).have);
    CHECK(row(a, HandType::HighCard).have);
    CHECK(row(a, HandType::HighCard).outs < 0);  // already held: not applicable
}

PF_TEST(analysis_straight_draws_and_their_outs) {
    const Evaluator ev = holdem();
    const auto open = analyzeHand(ev, hole("8c", "9d"), cards("6h 7s Kd"));
    CHECK_EQ(row(open, HandType::Straight).outs, 8);
    CHECK_EQ(row(open, HandType::Straight).needs, std::string("any Ten or Five"));
    CHECK(hasDraw(open, "Open-ended straight draw (8 outs)"));

    const auto gut = analyzeHand(ev, hole("9c", "Td"), cards("6h 7s Kd"));
    CHECK_EQ(row(gut, HandType::Straight).outs, 4);
    CHECK_EQ(row(gut, HandType::Straight).needs, std::string("any Eight"));
    CHECK(hasDraw(gut, "Gutshot straight draw (4 outs)"));

    const auto dbl = analyzeHand(ev, hole("7c", "9d"), cards("5h 8s Jd"));
    CHECK_EQ(row(dbl, HandType::Straight).outs, 8);
    CHECK(hasDraw(dbl, "Double gutshot straight draw (8 outs)"));

    // A straight on the board alone is not a draw for the hero.
    const auto board = analyzeHand(ev, hole("2c", "3d"), cards("6h 7s 8d 9c"));
    CHECK(!hasDraw(board, "straight draw"));
}

PF_TEST(analysis_short_deck_uses_its_own_straights) {
    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();
    const Evaluator ev(sd);
    // 6-7-8-9 can be completed by a Ten or by the Ace (A-6-7-8-9).
    const auto a = analyzeHand(ev, hole("8c", "9d"), cards("6h 7s Kd"));
    CHECK_EQ(row(a, HandType::Straight).outs, 8);
    CHECK(hasDraw(a, "Double gutshot straight draw (8 outs)"));
    CHECK_EQ(a.unseen, 31);  // 36 cards minus the 5 the hero can see
}

PF_TEST(analysis_pair_and_overcard_outs) {
    const Evaluator ev = holdem();
    CHECK(hasDraw(analyzeHand(ev, hole("Ah", "Kd"), cards("7c 2s 9d")), "Two overcards (6 outs to top pair)"));
    CHECK(hasDraw(analyzeHand(ev, hole("Ah", "9c"), cards("7c 2s Kd")), "One overcard (3 outs to top pair)"));
    CHECK(hasDraw(analyzeHand(ev, hole("Qh", "Qd"), cards("7c 2s 9d")), "Overpair (2 outs to a set)"));
    CHECK(hasDraw(analyzeHand(ev, hole("5h", "5d"), cards("7c 2s 9d")), "Pocket pair (2 outs to a set)"));
    CHECK(hasDraw(analyzeHand(ev, hole("Ah", "Kh"), cards("7h 2c 9d")), "Backdoor flush draw"));
    // Wild cards make draw text meaningless, so none is produced.
    RuleSet w;
    w.wild = WildMode::Deuces;
    sanitize(w);
    CHECK(analyzeHand(Evaluator(w), hole("Ah", "Kh"), cards("7h 3h 9d")).draws.empty());
}

PF_TEST(analysis_labels) {
    const Evaluator ev = holdem();
    CHECK_EQ(analyzeHand(ev, hole("Ah", "9h"), cards("7h 2h Kc")).label, std::string("Flush draw"));
    CHECK_EQ(analyzeHand(ev, hole("Th", "Qs"), cards("Td 7c 2d")).label, std::string("Pair of Tens"));
    CHECK_EQ(analyzeHand(ev, hole("Th", "Qs"), {}).label, std::string("Queen-Ten, offsuit"));
}

PF_TEST(analysis_have_and_possible_flags) {
    const Evaluator ev = holdem();
    const auto trips = analyzeHand(ev, hole("Th", "Td"), cards("Ts 2c 3d"));
    CHECK(row(trips, HandType::Pair).have);
    CHECK(row(trips, HandType::TwoPair).have);   // trips outrank two pair
    CHECK(row(trips, HandType::ThreeOfAKind).have);
    CHECK(!row(trips, HandType::Straight).have);
    CHECK(row(trips, HandType::FullHouse).possible);

    // Trip kings on the board, hero holds 2c 3d: no flush or straight can still be made.
    const auto dead = analyzeHand(ev, hole("2c", "3d"), cards("Ks Kd Kc"));
    CHECK(!row(dead, HandType::Flush).possible);
    CHECK(!row(dead, HandType::Straight).possible);
    CHECK_EQ(row(dead, HandType::Flush).exactShare, 0.0);
    CHECK(row(dead, HandType::FullHouse).possible);

    // Preflop: nothing exact, nothing greyed out.
    const auto pre = analyzeHand(ev, hole("Th", "Td"), {});
    CHECK(row(pre, HandType::StraightFlush).possible);
    CHECK_EQ(row(pre, HandType::Flush).exactShare, -1.0);
}

PF_TEST(analysis_exact_distribution_sizes) {
    const Evaluator ev = holdem();
    const auto river = exactFinalDistribution(ev, hole("Ah", "Kh"), cards("Qh Jh Th 2c 3d"));
    CHECK(river.available);
    CHECK_EQ(river.total, 1u);
    CHECK_EQ(river.counts[static_cast<std::size_t>(HandType::StraightFlush)], 1u);

    const auto turn = exactFinalDistribution(ev, hole("Ah", "Kh"), cards("7h 2h Jc 9d"));
    CHECK_EQ(turn.total, 46u);
    CHECK_EQ(turn.counts[static_cast<std::size_t>(HandType::Flush)], 9u);

    const auto flop = exactFinalDistribution(ev, hole("Ah", "Kh"), cards("7h 2h Jc"));
    CHECK_EQ(flop.total, 1081u);  // C(47,2)
    std::uint64_t sum = 0;
    for (auto n : flop.counts) sum += n;
    CHECK_EQ(sum, flop.total);

    CHECK(!exactFinalDistribution(ev, hole("Ah", "Kh"), {}).available);  // C(50,5) is left to the simulator
    // Known opponent cards are removed from the completions.
    const auto withDead = exactFinalDistribution(ev, hole("Ah", "Kh"), cards("7h 2h Jc 9d"), bit(c("3h")) | bit(c("4h")));
    CHECK_EQ(withDead.total, 44u);
    CHECK_EQ(withDead.counts[static_cast<std::size_t>(HandType::Flush)], 7u);
}

PF_TEST(analysis_exact_matches_simulator_on_the_turn) {
    // The hero's final-hand histogram from the simulator must match exact enumeration.
    auto ev = std::make_shared<const Evaluator>(RuleSet{});
    const HoleCards h = hole("Ah", "Kh");
    const auto board = cards("Qh Jh 2c 9d");
    const auto exact = exactFinalDistribution(*ev, h, board);
    CHECK(exact.available);

    Scenario s;
    s.evaluator = ev;
    s.board = board;
    s.seats = {Seat{true, h}, Seat{true, {kNoCard, kNoCard}}, Seat{true, {kNoCard, kNoCard}}};
    s.seed = 5;
    const Tally t = runChunks(s, 0, 1600, 0);  // 409,600 runs
    const auto n = static_cast<double>(t.runs);
    for (int i = 0; i < kHandTypeCount; ++i) {
        const auto type = static_cast<HandType>(i);
        const double p = exact.share(type);
        const double simulated = static_cast<double>(t.seats[0].hands[static_cast<std::size_t>(i)]) / n;
        const double tol = 5 * std::sqrt(std::max(p * (1 - p), 1e-6) / n);
        if (std::abs(simulated - p) > tol)
            std::cerr << "  " << handTypeDisplay(type) << ": simulated " << simulated << " exact " << p << "\n";
        CHECK(std::abs(simulated - p) <= tol);
    }
}

PF_TEST(analysis_reach_shares) {
    RuleSet r;
    std::array<double, kHandTypeCount> share{};
    share[static_cast<std::size_t>(HandType::HighCard)] = 0.5;
    share[static_cast<std::size_t>(HandType::Pair)] = 0.3;
    share[static_cast<std::size_t>(HandType::Flush)] = 0.15;
    share[static_cast<std::size_t>(HandType::FullHouse)] = 0.05;
    const auto reach = reachShares(r, share);
    CHECK(std::abs(reach[static_cast<std::size_t>(HandType::HighCard)] - 1.0) < 1e-12);
    CHECK(std::abs(reach[static_cast<std::size_t>(HandType::Pair)] - 0.5) < 1e-12);
    CHECK(std::abs(reach[static_cast<std::size_t>(HandType::Straight)] - 0.2) < 1e-12);
    CHECK(std::abs(reach[static_cast<std::size_t>(HandType::FullHouse)] - 0.05) < 1e-12);
    CHECK_EQ(reach[static_cast<std::size_t>(HandType::FiveOfAKind)], 0.0);  // disabled
    RuleSet sd;
    sd.deckSize = 36;
    sd.order = shortDeckOrder();  // flush outranks full house here
    const auto sdReach = reachShares(sd, share);
    CHECK(std::abs(sdReach[static_cast<std::size_t>(HandType::FullHouse)] - 0.2) < 1e-12);
}
