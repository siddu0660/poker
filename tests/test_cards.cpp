#include "test_main.hpp"
#include "core/Cards.hpp"
#include "core/Rng.hpp"

#include <array>

using namespace pf;

PF_TEST(cards_encoding_roundtrip) {
    for (int id = 0; id < kNumNaturalCards; ++id) {
        const auto c = static_cast<CardId>(id);
        CHECK_EQ(makeCard(suitOf(c), rankOf(c)), c);
        auto parsed = parseCard(cardName(c));
        CHECK(parsed.has_value());
        if (parsed) CHECK_EQ(*parsed, c);
    }
}

PF_TEST(cards_parse_edge_cases) {
    CHECK_EQ(*parseCard("As"), makeCard(3, 12));
    CHECK_EQ(*parseCard("2c"), makeCard(0, 0));
    CHECK_EQ(*parseCard("tD"), makeCard(1, 8));
    CHECK(isJoker(*parseCard("Jk")));
    CHECK(!parseCard("1s"));
    CHECK(!parseCard("Ax"));
    CHECK(!parseCard("A"));
    CHECK(!parseCard(""));
}

PF_TEST(cards_suit_masks) {
    CardMask m = bit(makeCard(2, 0)) | bit(makeCard(2, 12)) | bit(makeCard(0, 5));
    auto sm = suitMasks(m);
    CHECK_EQ(sm[0], static_cast<std::uint16_t>(1u << 5));
    CHECK_EQ(sm[2], static_cast<std::uint16_t>((1u << 0) | (1u << 12)));
    CHECK_EQ(sm[1], std::uint16_t{0});
    CHECK_EQ(popcount(m), 3);
}

PF_TEST(rng_deterministic_and_streams_differ) {
    Rng a(42, 0), b(42, 0), c(42, 1);
    bool differs = false;
    for (int i = 0; i < 64; ++i) {
        const auto x = a.next();
        CHECK_EQ(x, b.next());
        if (x != c.next()) differs = true;
    }
    CHECK(differs);
}

PF_TEST(rng_below_is_in_range_and_roughly_uniform) {
    Rng r(7);
    std::array<int, 52> hist{};
    constexpr int kN = 520000;
    for (int i = 0; i < kN; ++i) {
        const auto v = r.below(52);
        CHECK(v < 52);
        ++hist[v];
    }
    for (int h : hist) CHECK(h > 9500 && h < 10500);  // expect 10000 +- 5%
}
