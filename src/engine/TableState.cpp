#include "engine/TableState.hpp"

#include <algorithm>

#include "core/Rng.hpp"
#include "util/Log.hpp"

namespace pf {

TableState::TableState(std::shared_ptr<const Evaluator> evaluator, int seats, std::uint64_t seed)
    : evaluator_(std::move(evaluator)), seed_(seed) {
    seats = std::clamp(seats, kMinSeats, kMaxSeats);
    seats_.resize(static_cast<std::size_t>(seats));
    for (int i = 0; i < seats; ++i) resetSeat(seats_[static_cast<std::size_t>(i)], i);
    seats_[0].known = true;
    dealer_ = seats - 1;  // so the first hand's dealer is seat 0
    newHand();
}

void TableState::resetSeat(TableSeat& s, int index) {
    s.name = "Player " + std::to_string(index + 1);
    s.stack = evaluator_->rules().startingStack;
    s.bet = 0;
    s.folded = false;
}

void TableState::setRules(std::shared_ptr<const Evaluator> evaluator) {
    evaluator_ = std::move(evaluator);
    newHand();
}

void TableState::setSeatCount(int n) {
    n = std::clamp(n, kMinSeats, kMaxSeats);
    const int before = seatCount();
    seats_.resize(static_cast<std::size_t>(n));
    for (int i = before; i < n; ++i) resetSeat(seats_[static_cast<std::size_t>(i)], i);
    hero_ = std::min(hero_, n - 1);
    dealer_ = std::min(dealer_, n - 1);
    newHand();
}

void TableState::newHand() {
    const RuleSet& rules = evaluator_->rules();
    ++handNumber_;
    const int n = seatCount();
    dealer_ = (dealer_ + 1) % n;
    street_ = Street::Preflop;
    history_.clear();

    // Shuffle the whole deck: five board cards, then two per seat.
    auto deck = deckFor(rules);
    Rng rng(seed_, static_cast<std::uint64_t>(handNumber_));
    for (std::size_t i = 0; i + 1 < deck.size(); ++i)
        std::swap(deck[i], deck[i + rng.below(static_cast<std::uint32_t>(deck.size() - i))]);
    std::size_t next = 0;
    for (CardId& c : board_) c = deck[next++];
    for (int i = 0; i < n; ++i) {
        TableSeat& s = seats_[static_cast<std::size_t>(i)];
        s.stack = rules.startingStack;
        s.bet = 0;
        s.folded = false;
        s.hole = {deck[next], deck[next + 1]};
        next += 2;
    }
    seats_[static_cast<std::size_t>(hero_)].known = true;
    postBlinds();

    logInfo("[Game] New hand #" + std::to_string(handNumber_) + ", dealer seat " + std::to_string(dealer_ + 1));
}

void TableState::postBlinds() {
    const RuleSet& rules = evaluator_->rules();
    const int n = seatCount();
    antes_ = 0;
    if (rules.ante > 0) {
        for (TableSeat& s : seats_) {
            const int a = std::min(s.stack, rules.ante);
            s.stack -= a;
            antes_ += a;
        }
    }
    // Heads-up the dealer posts the small blind.
    sbSeat_ = n == 2 ? dealer_ : (dealer_ + 1) % n;
    bbSeat_ = (sbSeat_ + 1) % n;
    auto post = [&](int seat, int amount) {
        TableSeat& s = seats_[static_cast<std::size_t>(seat)];
        const int p = std::min(s.stack, amount);
        s.stack -= p;
        s.bet += p;
    };
    post(sbSeat_, rules.smallBlind);
    post(bbSeat_, rules.bigBlind);
}

void TableState::recordEquity(const std::vector<double>& equities) {
    if (equities.empty()) return;
    std::vector<double> e = equities;
    e.resize(seats_.size(), kNoEquity);
    for (std::size_t i = 0; i < seats_.size(); ++i)
        if (seats_[i].folded) e[i] = kNoEquity;
    if (!history_.empty() && history_.back().street == street_) history_.back().equity = std::move(e);
    else history_.push_back({street_, std::move(e)});
}

bool TableState::nextStreet(const std::vector<double>& equities) {
    recordEquity(equities);
    if (street_ == Street::River) return false;
    street_ = static_cast<Street>(static_cast<int>(street_) + 1);
    std::string line = std::string("[Game] ") + std::string(streetName(street_)) + ":";
    for (CardId c : board()) line += " " + cardName(c);
    logInfo(line);
    return true;
}

bool TableState::setFolded(int seat, bool folded) {
    TableSeat& s = seats_[static_cast<std::size_t>(seat)];
    if (s.folded == folded) return true;
    if (folded && activeCount() <= 1) return false;
    s.folded = folded;
    return true;
}

void TableState::setKnown(int seat, bool known) {
    seats_[static_cast<std::size_t>(seat)].known = known || seat == hero_;
}

void TableState::setHero(int seat) {
    hero_ = std::clamp(seat, 0, seatCount() - 1);
    seats_[static_cast<std::size_t>(hero_)].known = true;
}

void TableState::setHole(int seat, const HoleCards& hole) {
    // Every card in play lives in exactly one place, so a card that is already
    // elsewhere (board or another seat) trades places with the one it replaces.
    TableSeat& target = seats_[static_cast<std::size_t>(seat)];
    for (std::size_t k = 0; k < 2; ++k) {
        const CardId want = hole[k];
        if (want == kNoCard || want == target.hole[k]) continue;
        const CardId old = target.hole[k];
        for (CardId& c : board_) if (c == want) c = old;
        for (TableSeat& s : seats_)
            for (CardId& c : s.hole) if (&c != &target.hole[k] && c == want) c = old;
        target.hole[k] = want;
    }
}

std::vector<CardId> TableState::board() const {
    return {board_.begin(), board_.begin() + boardCardsFor(street_)};
}

int TableState::pot() const {
    int p = antes_;
    for (const TableSeat& s : seats_) p += s.bet;
    return p;
}

int TableState::activeCount() const {
    return static_cast<int>(std::count_if(seats_.begin(), seats_.end(), [](const TableSeat& s) { return !s.folded; }));
}

Scenario TableState::scenario(std::uint64_t simSeed) const {
    Scenario sc;
    sc.evaluator = evaluator_;
    sc.board = board();
    sc.seed = simSeed;
    for (const TableSeat& s : seats_) {
        Seat seat;
        seat.active = !s.folded;
        if (s.known) seat.hole = s.hole;
        sc.seats.push_back(seat);
    }
    return sc;
}

std::vector<int> TableState::showdownWinners() const {
    if (street_ != Street::River) return {};
    CardMask boardMask = 0;
    for (CardId c : board_) boardMask |= bit(c);
    HandScore best = 0;
    std::vector<int> winners;
    for (int i = 0; i < seatCount(); ++i) {
        const TableSeat& s = seats_[static_cast<std::size_t>(i)];
        if (s.folded) continue;
        const HandScore score = evaluator_->evaluate(boardMask | bit(s.hole[0]) | bit(s.hole[1]));
        if (winners.empty() || score > best) { best = score; winners = {i}; }
        else if (score == best) winners.push_back(i);
    }
    return winners;
}

CardMask TableState::deadForHero() const {
    CardMask dead = 0;
    for (int i = 0; i < seatCount(); ++i) {
        const TableSeat& s = seats_[static_cast<std::size_t>(i)];
        if (i != hero_ && s.known) dead |= bit(s.hole[0]) | bit(s.hole[1]);
    }
    return dead;
}

BettingContext TableState::heroBettingContext() const {
    const TableSeat& h = seats_[static_cast<std::size_t>(hero_)];
    BettingContext c;
    c.pot = pot();
    for (const TableSeat& s : seats_) c.currentBet = std::max(c.currentBet, s.bet);
    c.playerBet = h.bet;
    c.stack = h.stack;
    c.lastRaise = c.currentBet;  // the big blind counts as the opening bet
    c.street = street_;
    return c;
}

}  // namespace pf
