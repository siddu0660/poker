#pragma once
#include <string>

#include "imgui.h"

#include "core/Cards.hpp"

namespace pf::ui {

// Explicit int/double -> float, so -Wconversion stays quiet in drawing code.
template <class T>
constexpr float F(T v) noexcept { return static_cast<float>(v); }

inline ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
inline ImVec2 operator-(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
inline ImVec2 operator*(ImVec2 a, float k) { return {a.x * k, a.y * k}; }

// Colours shared by the panels.
struct Palette {
    static constexpr ImU32 Win = IM_COL32(72, 199, 116, 255);
    static constexpr ImU32 Split = IM_COL32(230, 168, 52, 255);
    static constexpr ImU32 Lose = IM_COL32(226, 84, 84, 255);
    static constexpr ImU32 Muted = IM_COL32(150, 154, 162, 255);
    static constexpr ImU32 Felt = IM_COL32(24, 92, 62, 255);
    static constexpr ImU32 FeltDark = IM_COL32(16, 66, 44, 255);
    static constexpr ImU32 Rail = IM_COL32(86, 54, 32, 255);
    static constexpr ImU32 RailLight = IM_COL32(120, 80, 50, 255);
    static constexpr ImU32 CardFace = IM_COL32(250, 248, 240, 255);
    static constexpr ImU32 CardBack = IM_COL32(42, 70, 140, 255);
    static constexpr ImU32 Gold = IM_COL32(244, 204, 90, 255);
};

// One colour per player for charts and seat accents.
ImU32 seatColor(int seat);
ImVec4 toVec4(ImU32 c);

// Suit symbols are drawn as shapes so no font glyphs are needed.
// suit: 0 club, 1 diamond, 2 heart, 3 spade.
void drawSuit(ImDrawList* dl, int suit, ImVec2 center, float radius, ImU32 color);
ImU32 suitColor(int suit);

// A playing card. Face-down shows the back; `lift` raises it and outlines it
// (used for the five cards that make the hero's hand).
void drawCard(ImDrawList* dl, ImVec2 topLeft, ImVec2 size, CardId card, bool faceDown, bool lift = false);
void drawEmptyCardSlot(ImDrawList* dl, ImVec2 topLeft, ImVec2 size);

// Filled/stroked ellipse as a polygon.
void drawEllipse(ImDrawList* dl, ImVec2 center, float rx, float ry, ImU32 fill, ImU32 stroke = 0,
                 float strokeWidth = 1.0f);

// Text at a larger scale than the current font, centred at `center`.
void drawTextCentered(ImDrawList* dl, ImVec2 center, const char* text, ImU32 color, float scale = 1.0f);
// Wrapper for ImGui::Text with a different size in the current window.
void bigText(const char* text, float scale, ImU32 color);

std::string percent(double fraction, int decimals = 1);

}  // namespace pf::ui
