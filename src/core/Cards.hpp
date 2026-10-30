#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pf {

using CardId = std::uint8_t;
using CardMask = std::uint64_t;

constexpr int kRanks = 13;
constexpr int kSuits = 4;
constexpr int kNumNaturalCards = 52;
constexpr CardId kJokerA = 52;
constexpr CardId kJokerB = 53;
constexpr CardId kNoCard = 0xFF;

// Rank 0 = '2' ... 12 = 'A'. Suits: c, d, h, s.
constexpr CardId makeCard(int suit, int rank) noexcept {
    return static_cast<CardId>(suit * kRanks + rank);
}
constexpr bool isJoker(CardId c) noexcept { return c == kJokerA || c == kJokerB; }
constexpr int rankOf(CardId c) noexcept { return c % kRanks; }
constexpr int suitOf(CardId c) noexcept { return c / kRanks; }
constexpr CardMask bit(CardId c) noexcept { return CardMask{1} << c; }
constexpr int popcount(CardMask m) noexcept { return std::popcount(m); }

inline constexpr std::string_view kRankChars = "23456789TJQKA";
inline constexpr std::string_view kSuitChars = "cdhs";

inline std::string cardName(CardId c) {
    if (c == kJokerA || c == kJokerB) return "Jk";
    if (c >= kNumNaturalCards) return "??";
    return std::string{kRankChars[static_cast<std::size_t>(rankOf(c))],
                       kSuitChars[static_cast<std::size_t>(suitOf(c))]};
}

// Parses "As", "Td", "2c" (case-insensitive) or "Jk".
inline std::optional<CardId> parseCard(std::string_view s) {
    if (s.size() != 2) return std::nullopt;
    auto lower = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + 32) : ch; };
    if (lower(s[0]) == 'j' && lower(s[1]) == 'k') return kJokerA;
    const char r = (s[0] >= 'a' && s[0] <= 'z') ? static_cast<char>(s[0] - 32) : s[0];
    const auto ri = kRankChars.find(r);
    const auto si = kSuitChars.find(lower(s[1]));
    if (ri == std::string_view::npos || si == std::string_view::npos) return std::nullopt;
    return makeCard(static_cast<int>(si), static_cast<int>(ri));
}

// Split a card mask into four 13-bit suit masks (jokers excluded).
constexpr std::array<std::uint16_t, kSuits> suitMasks(CardMask m) noexcept {
    std::array<std::uint16_t, kSuits> out{};
    for (int s = 0; s < kSuits; ++s)
        out[static_cast<std::size_t>(s)] = static_cast<std::uint16_t>((m >> (s * kRanks)) & 0x1FFF);
    return out;
}

}  // namespace pf
