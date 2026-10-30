#include "ui/DrawUtil.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace pf::ui {

namespace {
constexpr float kPi = 3.14159265358979f;

const char* rankLabel(int rank) {
    static const std::array<const char*, kRanks> labels{"2", "3", "4", "5", "6", "7", "8",
                                                        "9", "10", "J", "Q", "K", "A"};
    return labels[static_cast<std::size_t>(std::clamp(rank, 0, kRanks - 1))];
}
}  // namespace

ImU32 seatColor(int seat) {
    static constexpr std::array<ImU32, 6> colors{
        IM_COL32(86, 156, 245, 255), IM_COL32(240, 128, 70, 255), IM_COL32(120, 210, 120, 255),
        IM_COL32(214, 116, 214, 255), IM_COL32(240, 208, 80, 255), IM_COL32(100, 220, 220, 255)};
    return colors[static_cast<std::size_t>(((seat % 6) + 6) % 6)];
}

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 suitColor(int suit) {
    return (suit == 1 || suit == 2) ? IM_COL32(200, 30, 40, 255) : IM_COL32(28, 28, 34, 255);
}

void drawSuit(ImDrawList* dl, int suit, ImVec2 c, float r, ImU32 col) {
    switch (suit) {
        case 1:  // diamond
            dl->AddQuadFilled({c.x, c.y - r}, {c.x + r * 0.75f, c.y}, {c.x, c.y + r}, {c.x - r * 0.75f, c.y}, col);
            break;
        case 2: {  // heart: two lobes and a point
            const float lobe = r * 0.55f;
            dl->AddCircleFilled({c.x - lobe * 0.9f, c.y - r * 0.28f}, lobe, col, 20);
            dl->AddCircleFilled({c.x + lobe * 0.9f, c.y - r * 0.28f}, lobe, col, 20);
            dl->AddTriangleFilled({c.x - r * 1.02f, c.y - r * 0.05f}, {c.x + r * 1.02f, c.y - r * 0.05f},
                                  {c.x, c.y + r}, col);
            break;
        }
        case 3: {  // spade: inverted heart plus a stem
            const float lobe = r * 0.55f;
            dl->AddCircleFilled({c.x - lobe * 0.9f, c.y + r * 0.12f}, lobe, col, 20);
            dl->AddCircleFilled({c.x + lobe * 0.9f, c.y + r * 0.12f}, lobe, col, 20);
            dl->AddTriangleFilled({c.x - r * 1.02f, c.y + r * 0.0f}, {c.x + r * 1.02f, c.y + r * 0.0f},
                                  {c.x, c.y - r}, col);
            dl->AddTriangleFilled({c.x, c.y + r * 0.15f}, {c.x - r * 0.45f, c.y + r}, {c.x + r * 0.45f, c.y + r}, col);
            break;
        }
        default: {  // club: three circles and a stem
            const float lobe = r * 0.46f;
            dl->AddCircleFilled({c.x, c.y - r * 0.5f}, lobe, col, 20);
            dl->AddCircleFilled({c.x - r * 0.52f, c.y + r * 0.2f}, lobe, col, 20);
            dl->AddCircleFilled({c.x + r * 0.52f, c.y + r * 0.2f}, lobe, col, 20);
            dl->AddTriangleFilled({c.x, c.y}, {c.x - r * 0.4f, c.y + r}, {c.x + r * 0.4f, c.y + r}, col);
            break;
        }
    }
}

void drawEmptyCardSlot(ImDrawList* dl, ImVec2 p, ImVec2 size) {
    dl->AddRect(p, p + size, IM_COL32(255, 255, 255, 70), size.x * 0.1f, 0, 1.5f);
}

void drawCard(ImDrawList* dl, ImVec2 p, ImVec2 size, CardId card, bool faceDown, bool lift) {
    if (lift) p.y -= size.y * 0.14f;
    const float round = size.x * 0.12f;
    const ImVec2 q = p + size;
    dl->AddRectFilled(p + ImVec2{1.5f, 2.0f}, q + ImVec2{1.5f, 2.0f}, IM_COL32(0, 0, 0, 90), round);  // shadow

    if (faceDown) {
        dl->AddRectFilled(p, q, Palette::CardFace, round);
        dl->AddRectFilled(p + size * 0.08f, q - size * 0.08f, Palette::CardBack, round * 0.6f);
        dl->AddRect(p + size * 0.16f, q - size * 0.16f, IM_COL32(255, 255, 255, 90), round * 0.4f);
        return;
    }

    dl->AddRectFilled(p, q, Palette::CardFace, round);
    dl->AddRect(p, q, lift ? Palette::Gold : IM_COL32(60, 60, 60, 255), round, 0, lift ? 2.5f : 1.0f);

    if (isJoker(card)) {
        drawTextCentered(dl, p + size * 0.5f, "JK", IM_COL32(120, 40, 160, 255), size.x / 30.0f);
        return;
    }
    const int suit = suitOf(card);
    const ImU32 col = suitColor(suit);
    drawTextCentered(dl, p + ImVec2{size.x * 0.28f, size.y * 0.22f}, rankLabel(rankOf(card)), col, size.x / 28.0f);
    drawSuit(dl, suit, p + ImVec2{size.x * 0.28f, size.y * 0.46f}, size.x * 0.14f, col);
    drawSuit(dl, suit, p + ImVec2{size.x * 0.5f, size.y * 0.72f}, size.x * 0.27f, col);
}

void drawEllipse(ImDrawList* dl, ImVec2 c, float rx, float ry, ImU32 fill, ImU32 stroke, float width) {
    constexpr int kSteps = 96;
    std::vector<ImVec2> pts(kSteps);
    for (int i = 0; i < kSteps; ++i) {
        const float a = 2.0f * kPi * F(i) / F(kSteps);
        pts[static_cast<std::size_t>(i)] = {c.x + rx * std::cos(a), c.y + ry * std::sin(a)};
    }
    if ((fill >> IM_COL32_A_SHIFT) != 0) dl->AddConvexPolyFilled(pts.data(), kSteps, fill);
    if ((stroke >> IM_COL32_A_SHIFT) != 0) dl->AddPolyline(pts.data(), kSteps, stroke, ImDrawFlags_Closed, width);
}

void drawTextCentered(ImDrawList* dl, ImVec2 center, const char* text, ImU32 color, float scale) {
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * scale;
    const ImVec2 ext = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
    dl->AddText(font, size, center - ext * 0.5f, color, text);
}

void bigText(const char* text, float scale, ImU32 color) {
    ImGui::SetWindowFontScale(scale);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
}

std::string percent(double fraction, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f%%", decimals, fraction * 100.0);
    return buf;
}

}  // namespace pf::ui
