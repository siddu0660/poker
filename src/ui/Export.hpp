#pragma once
#include <string>

#include "engine/TableState.hpp"

namespace pf::ui {

// The equity history of the current hand as CSV: one row per street played,
// one column per player, equity in percent. Folded players leave the cell empty.
//   hand,street,Player 1,Player 2
//   7,Preflop,52.3100,47.6900
std::string equityCsv(const TableState& table);

// A CSV cell: quoted when it holds a comma, quote or line break.
std::string csvCell(const std::string& text);

bool writeTextFile(const std::string& path, const std::string& text, std::string* error = nullptr);

}  // namespace pf::ui
