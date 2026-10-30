#include "core/HandAnalysis.hpp"

#include <algorithm>
#include <bit>

namespace pf {

namespace {

constexpr std::array<std::string_view, kRanks> kSingular{
    "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Jack", "Queen", "King", "Ace"};
constexpr std::array<std::string_view, kRanks> kPlural{
    "Twos", "Threes", "Fours", "Fives", "Sixes", "Sevens", "Eights", "Nines", "Tens", "Jacks", "Queens", "Kings", "Aces"};

// Tiebreak nibble i (0 = first/defining) as a rank, or -1 when empty.
int nib(HandScore s, int i) {
    const int v = static_cast<int>((s >> (16 - 4 * i)) & 0xF);
    return v == 0 ? -1 : std::min(v - 1, 12);
}

std::string name(int rank, bool plural = false) {
    if (rank < 0) return "?";
    return std::string(rankName(rank, plural));
}

std::string kicker(HandScore s, int index) {
    const int r = nib(s, index);
    return r < 0 ? std::string{} : ", " + name(r) + " kicker";
}

std::string describe(HandScore s, bool withKicker) {
    const HandType t = scoreCategory(s);
    auto k = [&](int index) { return withKicker ? kicker(s, index) : std::string{}; };
    switch (t) {
        case HandType::HighCard: return name(nib(s, 0)) + " high";
        case HandType::Pair: return "Pair of " + name(nib(s, 0), true) + k(1);
        case HandType::TwoPair:
            return "Two Pair, " + name(nib(s, 0), true) + " and " + name(nib(s, 1), true) + k(2);
        case HandType::ThreeOfAKind: return "Three of a Kind, " + name(nib(s, 0), true) + k(1);
        case HandType::Straight: return "Straight, " + name(nib(s, 0)) + " high";
        case HandType::Flush: return "Flush, " + name(nib(s, 0)) + " high";
        case HandType::FullHouse:
            return "Full House, " + name(nib(s, 0), true) + " full of " + name(nib(s, 1), true);
        case HandType::FourOfAKind: return "Four of a Kind, " + name(nib(s, 0), true) + k(1);
        case HandType::StraightFlush:
            return nib(s, 0) == 12 ? "Royal Flush" : "Straight Flush, " + name(nib(s, 0)) + " high";
        case HandType::FiveOfAKind: return "Five of a Kind, " + name(nib(s, 0), true);
        case HandType::Count: break;
    }
    return "?";
}

struct Combos {
    // Calls f(mask) for every k-subset of cards. Returns the number of subsets.
    template <class F>
    static void each(const std::vector<CardId>& cards, int k, F&& f) {
        const int n = static_cast<int>(cards.size());
        if (k > n) return;
        std::vector<int> idx(static_cast<std::size_t>(k));
        for (int i = 0; i < k; ++i) idx[static_cast<std::size_t>(i)] = i;
        while (true) {
            CardMask m = 0;
            for (int i : idx) m |= bit(cards[static_cast<std::size_t>(i)]);
            f(m);
            int i = k - 1;
            while (i >= 0 && idx[static_cast<std::size_t>(i)] == n - k + i) --i;
            if (i < 0) break;
            ++idx[static_cast<std::size_t>(i)];
            for (int j = i + 1; j < k; ++j) idx[static_cast<std::size_t>(j)] = idx[static_cast<std::size_t>(j - 1)] + 1;
        }
    }
};

std::uint64_t binomial(int n, int k) {
    if (k < 0 || k > n) return 0;
    std::uint64_t r = 1;
    for (int i = 1; i <= k; ++i) r = r * static_cast<std::uint64_t>(n - k + i) / static_cast<std::uint64_t>(i);
    return r;
}

CardMask maskOf(const HoleCards& hole, const std::vector<CardId>& board) {
    CardMask m = 0;
    for (CardId c : hole) m |= bit(c);
    for (CardId c : board) m |= bit(c);
    return m;
}

bool readyHole(const HoleCards& hole) { return hole[0] != kNoCard && hole[1] != kNoCard && hole[0] != hole[1]; }

std::vector<CardId> unseenCards(const Evaluator& ev, CardMask seen) {
    std::vector<CardId> out;
    for (CardId c : deckFor(ev.rules()))
        if (!(seen & bit(c))) out.push_back(c);
    return out;
}

int strengthOf(const RuleSet& r, HandType t) {
    const auto it = std::find(r.order.begin(), r.order.end(), t);
    return it == r.order.end() ? -1 : static_cast<int>(it - r.order.begin());
}

std::string suitWord(int suit) {
    constexpr std::array<std::string_view, kSuits> n{"club", "diamond", "heart", "spade"};
    return std::string(n[static_cast<std::size_t>(suit)]);
}

// "any Ten or Queen", "any heart", "As, Kd" ... from the list of out cards.
std::string describeNeeds(const std::vector<CardId>& unseen, const std::vector<CardId>& outs) {
    if (outs.empty()) return {};
    CardMask outMask = 0;
    for (CardId c : outs) outMask |= bit(c);

    // All outs share one suit, and every unseen card of that suit is an out.
    const int firstSuit = isJoker(outs[0]) ? -1 : suitOf(outs[0]);
    const bool oneSuit = firstSuit >= 0 && std::all_of(outs.begin(), outs.end(), [&](CardId c) {
        return !isJoker(c) && suitOf(c) == firstSuit;
    });
    if (oneSuit) {
        const auto suitUnseen = std::count_if(unseen.begin(), unseen.end(), [&](CardId c) {
            return !isJoker(c) && suitOf(c) == firstSuit;
        });
        if (static_cast<std::size_t>(suitUnseen) == outs.size()) return "any " + suitWord(firstSuit);
    }

    // Ranks where every unseen card is an out.
    std::vector<int> fullRanks;
    CardMask covered = 0;
    for (int r = 12; r >= 0; --r) {
        bool any = false, all = true;
        CardMask rankMask = 0;
        for (CardId c : unseen) {
            if (isJoker(c) || rankOf(c) != r) continue;
            any = true;
            rankMask |= bit(c);
            if (!(outMask & bit(c))) all = false;
        }
        if (any && all) { fullRanks.push_back(r); covered |= rankMask; }
    }
    if (!fullRanks.empty() && covered == outMask) {
        std::string text = "any ";
        for (std::size_t i = 0; i < fullRanks.size(); ++i) {
            if (i) text += i + 1 == fullRanks.size() ? " or " : ", ";
            text += name(fullRanks[i]);
        }
        return text;
    }

    if (outs.size() <= 4) {
        std::string text;
        for (CardId c : outs) text += (text.empty() ? "" : ", ") + cardName(c);
        return text;
    }
    return std::to_string(outs.size()) + " specific cards";
}

int unseenOfRank(const std::vector<CardId>& unseen, int rank) {
    return static_cast<int>(std::count_if(unseen.begin(), unseen.end(), [&](CardId c) {
        return !isJoker(c) && rankOf(c) == rank;
    }));
}

}  // namespace

std::string_view rankName(int rank, bool plural) {
    if (rank < 0 || rank >= kRanks) return "?";
    return plural ? kPlural[static_cast<std::size_t>(rank)] : kSingular[static_cast<std::size_t>(rank)];
}

std::string describeScore(HandScore score) { return describe(score, true); }
std::string describeScoreShort(HandScore score) { return describe(score, false); }

std::string describeStartingHand(const HoleCards& hole) {
    if (!readyHole(hole) || isJoker(hole[0]) || isJoker(hole[1])) return "Starting hand";
    int hi = rankOf(hole[0]), lo = rankOf(hole[1]);
    if (hi == lo) return "Pocket " + name(hi, true);
    if (hi < lo) std::swap(hi, lo);
    const bool suited = suitOf(hole[0]) == suitOf(hole[1]);
    return name(hi) + "-" + name(lo) + (suited ? ", suited" : ", offsuit");
}

MadeHand analyzeMadeHand(const Evaluator& ev, const HoleCards& hole, const std::vector<CardId>& board) {
    MadeHand out;
    if (!readyHole(hole)) return out;

    // Jokers that are not wild do nothing, so they never belong to the best five.
    std::vector<CardId> cards;
    for (CardId c : hole) cards.push_back(c);
    for (CardId c : board) cards.push_back(c);
    cards.erase(std::remove_if(cards.begin(), cards.end(),
                               [&](CardId c) { return isJoker(c) && !(ev.wildMask() & bit(c)); }),
                cards.end());

    CardMask all = 0;
    for (CardId c : cards) all |= bit(c);
    out.valid = true;
    out.score = ev.evaluate(all);
    out.type = scoreCategory(out.score);

    if (cards.size() <= 5) {
        out.bestMask = all;
    } else {
        HandScore best = 0;
        Combos::each(cards, 5, [&](CardMask m) {
            const HandScore s = ev.evaluate(m);
            if (out.bestMask == 0 || s > best) { best = s; out.bestMask = m; }
        });
    }

    if (board.empty()) {
        out.description = describeStartingHand(hole);
        out.label = out.description;
    } else {
        out.description = describeScore(out.score);
        out.label = describeScoreShort(out.score);
    }
    return out;
}

ExactDistribution exactFinalDistribution(const Evaluator& ev, const HoleCards& hole,
                                         const std::vector<CardId>& board, CardMask dead,
                                         std::uint64_t maxCompletions) {
    ExactDistribution out;
    if (!readyHole(hole) || board.size() > 5) return out;
    const int missing = 5 - static_cast<int>(board.size());
    const CardMask seen = maskOf(hole, board) | dead;
    const auto unseen = unseenCards(ev, seen);
    const std::uint64_t completions = binomial(static_cast<int>(unseen.size()), missing);
    if (completions == 0 || completions > maxCompletions) return out;

    const CardMask base = maskOf(hole, board);
    Combos::each(unseen, missing, [&](CardMask extra) {
        ++out.counts[static_cast<std::size_t>(scoreCategory(ev.evaluate(base | extra)))];
        ++out.total;
    });
    out.available = true;
    return out;
}

namespace {

void detectDraws(const Evaluator& ev, const HoleCards& hole, const std::vector<CardId>& board,
                 const std::vector<CardId>& unseen, const MadeHand& made, HandAnalysis& out,
                 std::string& drawLabel) {
    const RuleSet& rules = ev.rules();
    if (rules.hasWilds() || board.size() < 3 || board.size() > 4) return;  // draws need a flop or turn
    const bool flop = board.size() == 3;
    const int madeStrength = strengthOf(rules, made.type);

    // Flush draws.
    std::array<int, kSuits> total{}, inHole{};
    for (CardId c : board) if (!isJoker(c)) ++total[static_cast<std::size_t>(suitOf(c))];
    for (CardId c : hole) if (!isJoker(c)) { ++total[static_cast<std::size_t>(suitOf(c))]; ++inHole[static_cast<std::size_t>(suitOf(c))]; }
    if (rules.isEnabled(HandType::Flush) && madeStrength < strengthOf(rules, HandType::Flush)) {
        for (int s = 0; s < kSuits; ++s) {
            const auto su = static_cast<std::size_t>(s);
            if (inHole[su] == 0) continue;
            const int suitOuts = static_cast<int>(std::count_if(unseen.begin(), unseen.end(),
                                                                [&](CardId c) { return !isJoker(c) && suitOf(c) == s; }));
            if (total[su] == 4) {
                out.draws.push_back("Flush draw (" + std::to_string(suitOuts) + " outs)");
                if (drawLabel.empty()) drawLabel = "Flush draw";
            } else if (total[su] == 3 && flop) {
                out.draws.push_back("Backdoor flush draw");
                if (drawLabel.empty()) drawLabel = "Backdoor flush draw";
            }
        }
    }

    // Straight draws, using the rules' own straight table (short deck, wheel).
    if (rules.isEnabled(HandType::Straight) && madeStrength < strengthOf(rules, HandType::Straight)) {
        std::uint32_t all = 0, boardOnly = 0;
        for (CardId c : board) if (!isJoker(c)) { all |= 1u << rankOf(c); boardOnly |= 1u << rankOf(c); }
        for (CardId c : hole) if (!isJoker(c)) all |= 1u << rankOf(c);
        std::vector<int> completing;
        for (int r = rules.minRank(); r < kRanks; ++r) {
            if (all >> r & 1) continue;
            if (ev.straightHigh(static_cast<std::uint16_t>(all | 1u << r)) < 0) continue;
            if (ev.straightHigh(static_cast<std::uint16_t>(boardOnly | 1u << r)) >= 0) continue;  // the board plays
            if (unseenOfRank(unseen, r) > 0) completing.push_back(r);
        }
        if (!completing.empty()) {
            int outs = 0;
            for (int r : completing) outs += unseenOfRank(unseen, r);
            std::string kind;
            if (completing.size() == 1) kind = "Gutshot straight draw";
            else if (completing.size() == 2 && completing[1] - completing[0] == 5) kind = "Open-ended straight draw";
            else kind = "Double gutshot straight draw";
            out.draws.push_back(kind + " (" + std::to_string(outs) + " outs)");
            if (drawLabel.empty() || drawLabel.rfind("Backdoor", 0) == 0) {
                drawLabel = kind.substr(0, kind.size() - std::string(" straight draw").size()) + " straight draw";
            }
        }
    }

    // Pairs, sets and overpairs.
    int boardTop = -1;
    for (CardId c : board) if (!isJoker(c)) boardTop = std::max(boardTop, rankOf(c));
    const int h0 = isJoker(hole[0]) ? -1 : rankOf(hole[0]);
    const int h1 = isJoker(hole[1]) ? -1 : rankOf(hole[1]);
    if (h0 >= 0 && h0 == h1) {
        if (h0 > boardTop && made.type == HandType::Pair)
            out.draws.push_back("Overpair (" + std::to_string(unseenOfRank(unseen, h0)) + " outs to a set)");
        else if (made.type == HandType::Pair && rules.isEnabled(HandType::ThreeOfAKind))
            out.draws.push_back("Pocket pair (" + std::to_string(unseenOfRank(unseen, h0)) + " outs to a set)");
    } else if (made.type == HandType::HighCard && rules.isEnabled(HandType::Pair)) {
        int over = 0, outs = 0;
        for (int h : {h0, h1})
            if (h > boardTop) { ++over; outs += unseenOfRank(unseen, h); }
        if (over == 2) out.draws.push_back("Two overcards (" + std::to_string(outs) + " outs to top pair)");
        else if (over == 1) out.draws.push_back("One overcard (" + std::to_string(outs) + " outs to top pair)");
    }
}

}  // namespace

HandAnalysis analyzeHand(const Evaluator& ev, const HoleCards& hole, const std::vector<CardId>& board,
                         CardMask dead) {
    HandAnalysis out;
    out.made = analyzeMadeHand(ev, hole, board);
    if (!out.made.valid) return out;
    out.valid = true;

    const RuleSet& rules = ev.rules();
    const CardMask base = maskOf(hole, board);
    const auto unseen = unseenCards(ev, base | dead);
    out.unseen = static_cast<int>(unseen.size());
    const int madeStrength = scoreStrength(out.made.score);

    const ExactDistribution exact = exactFinalDistribution(ev, hole, board, dead);
    const bool nextCard = board.size() >= 3 && board.size() < 5;

    std::vector<std::vector<CardId>> outCards(rules.order.size());
    if (nextCard) {
        for (CardId u : unseen) {
            const int s = scoreStrength(ev.evaluate(base | bit(u)));
            for (std::size_t i = 0; i < rules.order.size(); ++i)
                if (static_cast<int>(i) > madeStrength && s >= static_cast<int>(i)) outCards[i].push_back(u);
        }
    }

    for (std::size_t i = 0; i < rules.order.size(); ++i) {
        HandTypeRow row;
        row.type = rules.order[i];
        row.have = madeStrength >= static_cast<int>(i);
        if (nextCard && !row.have) {
            row.outs = static_cast<int>(outCards[i].size());
            row.needs = describeNeeds(unseen, outCards[i]);
        }
        if (exact.available) {
            row.exactShare = exact.share(row.type);
            row.possible = row.have || exact.counts[static_cast<std::size_t>(row.type)] > 0;
        }
        out.rows.push_back(std::move(row));
    }

    std::string drawLabel;
    detectDraws(ev, hole, board, unseen, out.made, out, drawLabel);
    out.label = (out.made.type == HandType::HighCard && !board.empty() && !drawLabel.empty()) ? drawLabel
                                                                                               : out.made.label;
    return out;
}

std::array<double, kHandTypeCount> reachShares(const RuleSet& rules,
                                               const std::array<double, kHandTypeCount>& share) {
    std::array<double, kHandTypeCount> reach{};
    double running = 0;
    for (std::size_t i = rules.order.size(); i-- > 0;) {
        const auto t = static_cast<std::size_t>(rules.order[i]);
        running += share[t];
        reach[t] = running;
    }
    return reach;
}

}  // namespace pf
