#pragma once
#include "ui/AppState.hpp"

namespace pf::ui {

void drawSettingsPanel(AppState& app);
void drawRulesPanel(AppState& app);
void drawTablePanel(AppState& app);
void drawEquityPanel(AppState& app);
void drawConsolePanel(AppState& app);
// The one modal dialog selected by AppState::dialog (import, export, About, shortcuts).
void drawDialogs(AppState& app);

}  // namespace pf::ui
