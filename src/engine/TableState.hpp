#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Betting.hpp"
#include "core/Cards.hpp"
#include "core/Evaluator.hpp"
#include "core/HandAnalysis.hpp"
#include "engine/Simulator.hpp"

namespace pf {

struct TableSeat {
    std::string name;
    HoleCards hole{kNoCard, kNoCard};  // always dealt, so a showdown has real cards
    bool known = false;                // false: the simulation treats the hand as random
    int stack = 0;                     // chips behind, after blinds and ante
    int bet = 0;                       // chips in front (blind posted this hand)
    bool folded = false;
};

// Per-seat equity (0..1) at one street. Folded seats hold kNoEquity.
constexpr double kNoEquity = -1.0;
struct EquityPoint {
    Street street = Street::Preflop;
    std::vector<double> equity;
};

// The hand being played at the table. v1 has no betting simulation: every hand
// starts from full stacks, blinds and antes are posted, and folds are toggled
// by hand. The whole board is dealt up front but only the revealed part is
// shown and simulated.
class TableState {
public:
    static constexpr int kMinSeats = 2;

    TableState(std::shared_ptr<const Evaluator> evaluator, int seats = 6, std::uint64_t seed = 1);

    // Swaps the rules and deals a new hand with fresh stacks.
    void setRules(std::shared_ptr<const Evaluator> evaluator);
    void setSeatCount(int n);  // clamped to 2..6; deals a new hand
    void setSeed(std::uint64_t seed) { seed_ = seed; }

    void newHand();
    // Records `equities` for the street being left (if given), then reveals the
    // next street. Returns false on the river.
    bool nextStreet(const std::vector<double>& equities = {});
    // Stores (or overwrites) the equity point for the current street.
    void recordEquity(const std::vector<double>& equities);

    // False when it would leave nobody in the hand.
    bool setFolded(int seat, bool folded);
    bool toggleFold(int seat) { return setFolded(seat, !seats_[static_cast<std::size_t>(seat)].folded); }
    void setKnown(int seat, bool known);  // the hero is always known
    void setHero(int seat);
    void setHole(int seat, const HoleCards& hole);  // swaps cards so the deck stays consistent

    Scenario scenario(std::uint64_t simSeed) const;
    // Seats that win at the river among those still in. Empty before the river.
    std::vector<int> showdownWinners() const;

    int seatCount() const { return static_cast<int>(seats_.size()); }
    const TableSeat& seat(int i) const { return seats_[static_cast<std::size_t>(i)]; }
    int hero() const { return hero_; }
    int dealer() const { return dealer_; }
    int smallBlindSeat() const { return sbSeat_; }
    int bigBlindSeat() const { return bbSeat_; }
    int handNumber() const { return handNumber_; }
    Street street() const { return street_; }
    int pot() const;
    int activeCount() const;
    std::vector<CardId> board() const;      // revealed cards
    const std::array<CardId, 5>& fullBoard() const { return board_; }
    const std::vector<EquityPoint>& equityHistory() const { return history_; }
    const Evaluator& evaluator() const { return *evaluator_; }
    std::shared_ptr<const Evaluator> evaluatorPtr() const { return evaluator_; }

    // Cards the hero knows are gone: known opponents' hole cards (folded or not).
    CardMask deadForHero() const;
    // Hero's betting context for the round at its start (blinds posted), used by the Rules preview.
    BettingContext heroBettingContext() const;

private:
    void resetSeat(TableSeat& s, int index);
    void postBlinds();

    std::shared_ptr<const Evaluator> evaluator_;
    std::vector<TableSeat> seats_;
    std::array<CardId, 5> board_{};
    std::vector<EquityPoint> history_;
    Street street_ = Street::Preflop;
    int hero_ = 0;
    int dealer_ = -1;
    int sbSeat_ = 0, bbSeat_ = 1;
    int handNumber_ = 0;
    int antes_ = 0;
    std::uint64_t seed_ = 1;
};

}  // namespace pf
