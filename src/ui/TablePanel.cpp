#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Geometry {
    ImVec2 origin, size, center;
    float seatRx = 0, seatRy = 0;      // ellipse the seat nodes sit on
    float feltRx = 0, feltRy = 0;
    ImVec2 node;                        // seat box size
    ImVec2 card;                        // hole card size
    ImVec2 boardCard;                   // community card size
};

Geometry layout(ImVec2 origin, ImVec2 size) {
    Geometry g;
    g.origin = origin;
    g.size = size;
    const float fs = ImGui::GetFontSize();
    g.node = {std::clamp(size.x * 0.20f, fs * 11.6f, fs * 13.5f), fs * 4.7f};
    g.card = {g.node.y * 0.42f, g.node.y * 0.60f};
    const float topMargin = g.node.y * 0.5f + g.card.y * 0.75f + 4.0f;
    const float botMargin = g.node.y * 0.5f + 4.0f;
    g.seatRy = std::max(40.0f, (size.y - topMargin - botMargin) * 0.5f);
    g.seatRx = std::max(60.0f, (size.x - g.node.x - 8.0f) * 0.5f);
    g.center = {origin.x + size.x * 0.5f, origin.y + topMargin + g.seatRy};
    g.feltRx = g.seatRx * 0.93f;
    g.feltRy = g.seatRy * 0.82f;
    const float bw = std::clamp(g.feltRx * 0.15f, fs * 2.2f, fs * 4.0f);
    g.boardCard = {bw, bw * 1.4f};
    return g;
}

// Distance from a seat's centre to the edge of its box, along the unit direction u.
float edgeDistance(const Geometry& g, ImVec2 u) {
    const float tx = std::abs(u.x) > 1e-4f ? g.node.x * 0.5f / std::abs(u.x) : 1e9f;
    const float ty = std::abs(u.y) > 1e-4f ? g.node.y * 0.5f / std::abs(u.y) : 1e9f;
    return std::min(tx, ty);
}

// Seat i on screen, hero at the bottom, going clockwise.
ImVec2 seatPosition(const Geometry& g, int seat, int hero, int n) {
    const float angle = kPi * 0.5f + 2.0f * kPi * F(((seat - hero) % n + n) % n) / F(n);
    return {g.center.x + g.seatRx * std::cos(angle), g.center.y + g.seatRy * std::sin(angle)};
}

ImU32 withAlpha(ImU32 c, float a) {
    const ImU32 alpha = static_cast<ImU32>(F((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

// Short text for a seat box: "Pair of Tens", "Ace high", otherwise just the hand type.
// `override` is a draw name ("Flush draw") that replaces a bare high card.
std::string seatLabel(const MadeHand& made, const std::string& override = {}) {
    if (!override.empty() && made.type == HandType::HighCard) return override;
    if (made.type == HandType::Pair || made.type == HandType::HighCard) return made.label;
    return std::string(handTypeDisplay(made.type));
}

void drawBadge(ImDrawList* dl, ImVec2 rightTop, const char* text, ImU32 col, float scale) {
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * scale;
    const ImVec2 ext = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
    const ImVec2 pad{size * 0.4f, size * 0.15f};
    const ImVec2 tl{rightTop.x - ext.x - pad.x * 2.0f, rightTop.y};
    dl->AddRectFilled(tl, tl + ext + pad * 2.0f, withAlpha(col, 0.25f), size * 0.5f);
    dl->AddRect(tl, tl + ext + pad * 2.0f, col, size * 0.5f);
    dl->AddText(font, size, tl + pad, col, text);
}

void drawSeat(ImDrawList* dl, AppState& app, const Geometry& g, int i, ImVec2 pos, bool winner, bool bestHighlight) {
    const TableSeat& seat = app.table.seat(i);
    const SeatView& view = app.seatViews[static_cast<std::size_t>(i)];
    const bool hero = i == app.table.hero();
    const bool folded = seat.folded;
    const float dim = folded ? 0.45f : 1.0f;
    const float fs = ImGui::GetFontSize();

    const ImVec2 tl = pos - g.node * 0.5f;
    const ImVec2 br = tl + g.node;
    const float round = fs * 0.5f;

    // Hole cards sit on the top edge of the box. Folded players have thrown theirs away.
    if (!folded) {
        const float gap = g.card.x * 0.12f;
        const float total = g.card.x * 2.0f + gap;
        ImVec2 c0{pos.x - total * 0.5f, tl.y - g.card.y * 0.75f};
        for (std::size_t k = 0; k < 2; ++k) {
            const CardId card = seat.hole[k];
            const bool lift = hero && bestHighlight && (app.heroAnalysis.made.bestMask & bit(card)) != 0;
            drawCard(dl, {c0.x + F(k) * (g.card.x + gap), c0.y}, g.card, card, !view.visible, lift);
        }
    }

    // Box.
    const ImU32 border = winner ? Palette::Gold : hero ? IM_COL32(110, 170, 255, 255) : IM_COL32(90, 96, 108, 255);
    dl->AddRectFilled(tl, br, IM_COL32(24, 28, 36, folded ? 150 : 235), round);
    dl->AddRect(tl, br, withAlpha(border, dim), round, 0, (winner || hero) ? 2.5f : 1.0f);

    // Avatar.
    const float ar = g.node.y * 0.2f;
    const ImVec2 av{tl.x + ar + fs * 0.4f, tl.y + ar + fs * 0.35f};
    dl->AddCircleFilled(av, ar, withAlpha(seatColor(i), dim), 24);
    char num[16];
    std::snprintf(num, sizeof num, "%d", i + 1);
    drawTextCentered(dl, av, num, IM_COL32(15, 15, 20, 255), 0.95f);

    // Name and status badge.
    const ImU32 text = withAlpha(IM_COL32(235, 238, 245, 255), dim);
    const ImU32 muted = withAlpha(Palette::Muted, dim);
    const float tx = av.x + ar + fs * 0.4f;
    std::string name = seat.name;
    if (hero) name += " (You)";
    dl->AddText({tx, tl.y + fs * 0.25f}, text, name.c_str());
    drawBadge(dl, {br.x - fs * 0.3f, tl.y + fs * 1.35f}, folded ? "FOLDED" : "ACTIVE",
              folded ? Palette::Lose : Palette::Win, 0.72f);

    // Stack, and what they have.
    char line[64];
    std::snprintf(line, sizeof line, "Stack %d", seat.stack);
    dl->AddText({tx, tl.y + fs * 1.4f}, muted, line);

    std::string label;
    if (folded) label = "Folded";
    else if (view.visible) label = seatLabel(view.made);
    else label = "Random hand";
    if (hero && !folded && !app.heroAnalysis.label.empty() && app.heroAnalysis.valid && !app.table.board().empty())
        label = seatLabel(app.heroAnalysis.made, app.heroAnalysis.label);
    dl->PushClipRect({tl.x + fs * 0.3f, tl.y}, {br.x - fs * 0.3f, br.y}, true);
    dl->AddText({tl.x + fs * 0.5f, tl.y + fs * 2.45f}, withAlpha(Palette::Gold, dim), label.c_str());

    // Live equity with a thin bar.
    if (!folded && app.snap.seats.size() == static_cast<std::size_t>(app.table.seatCount()) && app.snap.runs > 0) {
        const double eq = app.snap.equity(i);
        char e[24];
        std::snprintf(e, sizeof e, "%.1f%%", eq * 100.0);
        dl->AddText({tl.x + fs * 0.5f, tl.y + fs * 3.4f}, Palette::Win, e);
        const float barL = tl.x + fs * 4.0f, barR = br.x - fs * 0.5f, barY = tl.y + fs * 3.9f;
        dl->AddRectFilled({barL, barY - 3.0f}, {barR, barY + 3.0f}, IM_COL32(60, 64, 74, 255), 3.0f);
        dl->AddRectFilled({barL, barY - 3.0f}, {barL + (barR - barL) * F(std::clamp(eq, 0.0, 1.0)), barY + 3.0f},
                          seatColor(i), 3.0f);
    }
    dl->PopClipRect();

    if (winner) drawBadge(dl, {br.x, tl.y - fs * 0.2f}, "WINNER", Palette::Gold, 0.8f);
}

void drawCommunity(ImDrawList* dl, AppState& app, const Geometry& g) {
    const auto board = app.table.board();
    const float gap = g.boardCard.x * 0.18f;
    const float total = g.boardCard.x * 5.0f + gap * 4.0f;
    const ImVec2 start{g.center.x - total * 0.5f, g.center.y - g.boardCard.y * 0.55f};
    drawTextCentered(dl, {g.center.x, start.y - ImGui::GetFontSize() * 1.1f}, "Community Cards",
                     IM_COL32(255, 255, 255, 150), 0.9f);

    const bool heroHand = app.heroAnalysis.valid && board.size() >= 3;
    for (std::size_t k = 0; k < 5; ++k) {
        const ImVec2 p{start.x + F(k) * (g.boardCard.x + gap), start.y};
        if (k < board.size()) {
            const bool lift = heroHand && (app.heroAnalysis.made.bestMask & bit(board[k])) != 0;
            drawCard(dl, p, g.boardCard, board[k], false, lift);
        } else {
            drawEmptyCardSlot(dl, p, g.boardCard);
        }
    }
    char pot[48];
    std::snprintf(pot, sizeof pot, "Pot: %d", app.table.pot());
    drawTextCentered(dl, {g.center.x, start.y + g.boardCard.y + ImGui::GetFontSize() * 1.5f}, pot, Palette::Gold, 1.15f);
}

void seatMenu(AppState& app, int i) {
    const TableSeat& seat = app.table.seat(i);
    if (ImGui::MenuItem(seat.folded ? "Back into the hand" : "Fold")) app.toggleFold(i);
    ImGui::Separator();
    if (ImGui::MenuItem("Known cards", nullptr, seat.known, i != app.table.hero())) app.setSeatKnown(i, true);
    if (ImGui::MenuItem("Random cards", nullptr, !seat.known, i != app.table.hero())) app.setSeatKnown(i, false);
    ImGui::Separator();
    if (ImGui::MenuItem("Set as hero", nullptr, false, i != app.table.hero())) app.setHero(i);
}

}  // namespace

void drawTablePanel(AppState& app) {
    if (!app.visible.table) return;
    ImGui::SetNextWindowSizeConstraints({320.0f, 260.0f}, {FLT_MAX, FLT_MAX});
    if (!ImGui::Begin(PanelTitles::Table, &app.visible.table)) {
        ImGui::End();
        return;
    }
    TableState& t = app.table;

    // ---- header
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Players");
    ImGui::SameLine();
    int n = t.seatCount();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
    if (ImGui::BeginCombo("##players", std::to_string(n).c_str())) {
        for (int k = TableState::kMinSeats; k <= kMaxSeats; ++k)
            if (ImGui::Selectable(std::to_string(k).c_str(), k == n)) app.setSeatCount(k);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New Hand")) app.newHand();
    ImGui::SameLine();
    ImGui::BeginDisabled(t.street() == Street::River);
    if (ImGui::Button("Next Street")) app.nextStreet();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Hand #%d  |  %s", t.handNumber(), std::string(streetName(t.street())).c_str());

    if (app.seatViews.size() != static_cast<std::size_t>(t.seatCount())) {  // before the first frame() refresh
        ImGui::End();
        return;
    }

    // ---- table
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 320.0f);
    avail.y = std::max(avail.y, 240.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const Geometry g = layout(origin, avail);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    drawEllipse(dl, g.center, g.feltRx + 9.0f, g.feltRy + 9.0f, Palette::Rail, Palette::RailLight, 2.0f);
    drawEllipse(dl, g.center, g.feltRx, g.feltRy, Palette::Felt, Palette::FeltDark, 3.0f);
    drawEllipse(dl, g.center, g.feltRx * 0.82f, g.feltRy * 0.78f, 0, IM_COL32(255, 255, 255, 28), 1.5f);

    drawCommunity(dl, app, g);

    const auto winners = t.showdownWinners();
    const int count = t.seatCount();
    const bool bestHighlight = app.heroAnalysis.valid && !t.board().empty();

    // Chips and the dealer button sit just outside each seat box, towards the middle.
    for (int i = 0; i < count; ++i) {
        const TableSeat& s = t.seat(i);
        const ImVec2 p = seatPosition(g, i, t.hero(), count);
        const ImVec2 toward = g.center - p;
        const float len = std::max(1.0f, std::sqrt(toward.x * toward.x + toward.y * toward.y));
        const ImVec2 u{toward.x / len, toward.y / len};
        const ImVec2 perp{-u.y, u.x};
        const float r = ImGui::GetFontSize() * 0.75f;
        float edge = edgeDistance(g, u);
        if (u.y < -0.3f) edge += g.card.y * 0.95f * -u.y + r * 0.6f;  // hole cards stick out above the box
        if (s.bet > 0 && !s.folded) {
            const ImVec2 chip = p + u * (edge + r * 1.5f);
            dl->AddCircleFilled(chip, r, IM_COL32(190, 60, 60, 255), 24);
            dl->AddCircle(chip, r * 0.78f, IM_COL32(255, 255, 255, 190), 24, 1.5f);
            char b[16];
            std::snprintf(b, sizeof b, "%d", s.bet);
            drawTextCentered(dl, chip, b, IM_COL32(255, 255, 255, 255), 0.72f);
        }
        if (i == t.dealer()) {
            const ImVec2 d = p + u * (edge + r * 1.2f) + perp * (r * 2.6f);
            dl->AddCircleFilled(d, r * 0.85f, IM_COL32(245, 245, 245, 255), 20);
            drawTextCentered(dl, d, "D", IM_COL32(20, 20, 20, 255), 0.85f);
        }
    }

    for (int i = 0; i < count; ++i) {
        const ImVec2 p = seatPosition(g, i, t.hero(), count);
        const bool winner = std::find(winners.begin(), winners.end(), i) != winners.end();
        drawSeat(dl, app, g, i, p, winner, bestHighlight);
    }

    // Hit areas over the seat boxes: click folds, right-click opens the menu.
    for (int i = 0; i < count; ++i) {
        const ImVec2 p = seatPosition(g, i, t.hero(), count);
        ImGui::SetCursorScreenPos(p - g.node * 0.5f);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seat", g.node)) app.toggleFold(i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to fold or rejoin. Right-click for more.");
        if (ImGui::BeginPopupContextItem("seatmenu", ImGuiPopupFlags_MouseButtonRight)) {
            seatMenu(app, i);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + avail.y - 1.0f});
    ImGui::Dummy({1.0f, 1.0f});
    ImGui::End();
}

}  // namespace pf::ui
