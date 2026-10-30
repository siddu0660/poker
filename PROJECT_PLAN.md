# PokerForge — Custom Poker Engine & Analyzer

A desktop tool for real-time poker odds. A parallel Monte Carlo backend (C++20, OpenMP, O(1) bitboard hand evaluation) drives a dockable Dear ImGui / SDL2 / ImPlot front end. Rules are data-driven through JSON.

> Status: **M0 (headless) and M1 done; M2 next.** This file is the single source of truth for what gets built, in what order, and how it is verified.

---

## 1. Goals

1. Real-time equity (win / split / lose %) for 2–6 players, updating live while the simulation runs.
2. Evaluation speed in the tens of millions of 7-card hands per second per core, with a constant-time (O(1)) evaluator.
3. Custom rules without recompiling: deck size, hand-ranking order and toggles, wild cards, blinds and betting structure, all via JSON and editable in the UI.
4. A responsive multi-panel UI. The simulation never blocks the render thread.
5. Reproducible and testable: seedable RNG, unit tests, and a brute-force cross-check of the evaluator.

Non-goals for v1: online play, bots or solvers (CFR), Omaha or stud engines, networking.

---

## 2. Tech stack

| Concern | Choice |
|---|---|
| Language | C++20 (GCC 12+, Clang 15+, MSVC 2022) |
| Build | CMake ≥ 3.24 + Ninja, `CMakePresets.json` |
| Parallelism | OpenMP; falls back to single thread if not found |
| UI | Dear ImGui (**docking** branch, pinned tag `v1.91.9-docking`) with SDL2 and the OpenGL3 backend |
| Plots | ImPlot (pinned to a release or commit compatible with that ImGui) |
| Rules / config | nlohmann/json (v3.11.x) |
| Dependencies | `FetchContent` for ImGui, ImPlot, nlohmann/json. SDL2 via `find_package`, falling back to `FetchContent`. |
| Tests | Small in-repo test runner registered with CTest (no extra dependency) |

Notes:
- The OpenGL3 backend is chosen over `sdlrenderer2` because ImPlot needs 32-bit indices or vtx-offset support for large plots.
- macOS Apple Clang has no OpenMP. Use `brew install libomp` and pass `-DOpenMP_ROOT=$(brew --prefix libomp)`.

### Cross-platform requirements (Linux, Windows, macOS from day one)
| Area | Plan |
|---|---|
| Compilers | GCC 12+, Clang 15+, MSVC 2022 (17.x). Code stays standard C++20: `std::popcount` / `std::countl_zero` instead of `__builtin_*` or `_BitScan*`. |
| OpenMP | Loop variables are signed integers and reductions are done by hand with thread-local data, so it works with MSVC's OpenMP 2.0 (`/openmp`) as well as GCC/Clang. Missing OpenMP falls back to one thread with a warning. |
| Graphics | OpenGL3 backend on all three. GLSL 130 on Linux/Windows, 150 core + forward-compat on macOS. ImGui ships its own GL loader, so no GLEW/glad. |
| SDL2 | `find_package` first, then `FetchContent` fallback so a clean Windows machine builds with just CMake + a compiler. |
| Paths and files | `std::filesystem` with UTF-8 paths (`std::u8string` conversions where needed). User settings in `SDL_GetPrefPath`. Rules folder found relative to the executable, not the working directory. |
| High DPI | Scale fonts and style by the display DPI. SDL `ALLOW_HIGHDPI` on macOS and Windows. |
| Endianness / types | Fixed-width integer types everywhere. No `long` assumptions (it is 32-bit on Windows). |
| Warnings | `-Wall -Wextra -Wconversion` on GCC/Clang, `/W4` on MSVC. Warnings as errors in CI only. |
| CI | GitHub Actions matrix: ubuntu-latest, windows-latest, macos-latest × Release. Each job builds, runs the unit tests, and runs a short evaluator benchmark. |
| Presets | `linux-*`, `windows-*` (Ninja + MSVC via `vcvars`), `macos-*` presets in `CMakePresets.json`. |

---

## 3. Architecture

```
┌──────────────────────────── UI thread (SDL2 + ImGui) ────────────────────────────┐
│  Settings │ Rules │ Table │ Equity │ Console          AppState (single owner)     │
│                    │ pushes Scenario / Rules / speed       ▲ pulls Snapshot       │
└────────────────────┼───────────────────────────────────────┼─────────────────────┘
                     ▼                                       │
┌────────────── SimEngine (worker thread) ─────────────────────────────────────────┐
│  loop: copy Scenario → run batch (OpenMP parallel for) → merge → publish Snapshot │
│  throttle to target runs/sec · epoch counter drops stale batches · auto-stop CI   │
└──────────────┬───────────────────────────────────────────────────────────────────┘
               ▼
        Evaluator (immutable per RuleSet, shared_ptr)  ·  Rng (one per thread)
               ▲
        RuleSet (JSON ⇄ struct)  ·  Cards (bitboard)  ·  Log (thread-safe ring buffer)
```

Principles:
- **Core library has no UI dependency.** `pokercore` (cards, rules, evaluator, simulator, table state) builds and tests headless. The UI is a thin client.
- **One writer per piece of state.** The UI thread owns `AppState`. The worker owns accumulators. They exchange only an immutable `Scenario` (in) and a `Snapshot` copy (out), guarded by one mutex plus an epoch counter.
- **No allocation in the hot loop.** Per-thread pool array, in-place partial Fisher–Yates, thread-local counters merged once per batch.

---

## 4. Project structure

```
PokerForge/
├─ CMakeLists.txt
├─ CMakePresets.json            # linux/mac/windows-ninja, debug/release/relwithdebinfo
├─ cmake/Dependencies.cmake     # FetchContent for imgui, implot, json, SDL2 fallback
├─ README.md                    # (this plan, trimmed to a README once built)
├─ rules/
│  ├─ texas_holdem.json
│  ├─ short_deck_36.json        # flush > full house, trips > straight, A-6-7-8-9 wheel
│  ├─ deuces_wild.json
│  └─ jokers_wild.json
├─ src/
│  ├─ main.cpp
│  ├─ core/                     # → library: pokercore
│  │  ├─ Cards.hpp              # CardId, CardMask, names
│  │  ├─ Rng.hpp                # xoshiro256** + splitmix64
│  │  ├─ Rules.hpp/.cpp         # RuleSet, JSON load/save/validate
│  │  ├─ Evaluator.hpp/.cpp     # O(1) evaluator (+ wild-card path)
│  │  └─ Betting.hpp            # min/max raise by structure
│  ├─ engine/
│  │  ├─ TableState.hpp/.cpp    # seats, board, street, deal, blinds
│  │  ├─ Simulator.hpp/.cpp     # SimEngine worker, batch kernel, Snapshot
│  │  └─ Benchmark.hpp/.cpp     # evaluator throughput / thread scaling
│  ├─ util/
│  │  └─ Log.hpp/.cpp           # thread-safe ring buffer, levels
│  └─ ui/                       # → executable: pokerforge
│     ├─ App.hpp/.cpp           # SDL/GL init, main loop, dockspace, menu bar
│     ├─ AppState.hpp           # everything the panels share
│     ├─ DrawUtil.hpp/.cpp      # card + suit drawing, big-text helper
│     ├─ SettingsPanel.cpp
│     ├─ RulesPanel.cpp
│     ├─ EquityPanel.cpp
│     ├─ TablePanel.cpp
│     └─ ConsolePanel.cpp
└─ tests/
   ├─ test_main.cpp             # tiny runner
   ├─ test_evaluator.cpp
   ├─ test_rules.cpp
   └─ test_simulator.cpp
```

### CMake plan
- Targets: `pokercore` (static), `pokerforge` (executable, links ImGui/ImPlot/SDL2/OpenGL), `pokercore_tests` (CTest).
- Options: `PF_BUILD_UI` (ON), `PF_BUILD_TESTS` (ON), `PF_NATIVE_ARCH` (OFF by default, adds `-march=native`), `PF_ENABLE_LTO` (ON in Release).
- ImGui and ImPlot are compiled into small static libs (they ship no CMake). ImGui backends: `imgui_impl_sdl2`, `imgui_impl_opengl3`.
- Rules JSON is copied next to the executable post-build.
- Build commands: `cmake --preset linux-release && cmake --build --preset linux-release`.

---

## 5. Core design

### 5.1 Cards
- `CardId` = `suit*13 + rank` (rank 0 = '2' … 12 = 'A'; suits c, d, h, s). Jokers are ids 52 and 53.
- A set of cards is a 64-bit `CardMask`. Dead cards, board, and hands are all masks.

### 5.2 Rule set (what the JSON controls)
- **Deck**: size 24–52 in steps of 4 (removes lowest ranks: 36 = 6…A), plus 0–2 jokers.
- **Wild cards**: none, deuces, jokers, one-eyed jacks (J♠ and J♥).
- **Hand rankings**: an ordered list (weakest → strongest). Listed types are enabled. Unlisted types are disabled. High Card is always on. Five of a Kind is available only with wild cards.
- **Wheel straight**: Ace plays low (A-2-3-4-5, or A-6-7-8-9 in a 36-card deck).
- **Money**: small blind, big blind, ante, starting stack.
- **Betting**: no-limit, pot-limit, fixed-limit (+ max raises per round).
- A `sanitize()` step fixes invalid values and returns human-readable warnings (shown in the console).

JSON example:
```json
{
  "name": "Short Deck (6+)",
  "deck": { "size": 36, "jokers": 0 },
  "wild": "none",
  "hand_rankings": {
    "order": ["high_card","pair","two_pair","straight","three_of_a_kind",
              "full_house","flush","four_of_a_kind","straight_flush"],
    "wheel_straight": true
  },
  "blinds": { "small": 10, "big": 20, "ante": 20 },
  "starting_stack": 2000,
  "betting": { "structure": "no_limit", "max_raises": 4 }
}
```

### 5.3 Evaluator (O(1))
Chosen approach: **bitboard rank-mask evaluation** with 8192-entry tables. It supports arbitrary rankings and decks, which fixed Cactus Kev or Two Plus Two tables cannot.
- Split the card mask into four 13-bit suit masks `c, d, h, s`.
- Bit-parallel rank multiplicity: `any = c|d|h|s`, `two` (≥2 suits), `three` (≥3), `four` (all 4).
- Tables built once per `RuleSet`:
  - `straightHigh[mask]`: highest straight in a rank mask (respects min rank and wheel).
  - `topPacked[mask]`: top-5 ranks packed in 4-bit nibbles for kicker comparison.
- Flush: the suit mask with popcount ≥ 5, then `straightHigh` on it gives a straight flush.
- Score layout (32-bit, plain integer comparison): `strength<<28 | category<<24 | tiebreak(20 bits)`. `strength` comes from the rule's ranking order, so custom orders just work.
- Disabled categories are skipped, so the hand "falls through" to the best enabled one (e.g. flush off → straight or pair).
- If the ranking order matches the natural order, return at the first hit (fast path). Otherwise evaluate all candidates and take the max.
- **Wild-card path** (taken only if the hand contains wilds): analytic, no enumeration. With `k` wilds and natural rank counts, pick the best category top-down using "missing cards ≤ k" tests:
  - Five/four/three of a kind: highest rank with `count + k ≥ n`.
  - Straight and straight flush: sliding windows, with the number of missing cards ≤ k.
  - Full house: the best (trips, pair) pair of ranks whose combined need ≤ k.
  - Flush: the best suit with `n + k ≥ 5`, wilds filling the highest missing ranks.
  - Kickers: leftover wilds become the highest unused rank.
- Targets: ≥ 30M evals/s/core on the no-wild path.

### 5.4 Simulator
- **Scenario**: rules, board (0–5 cards), seats (active/folded; hole cards known or random).
- **Batch kernel**:
  1. Build the pool = deck − board − known holes.
  2. Each OpenMP thread gets its own pool copy and `Rng(seed, thread)`.
  3. Per iteration, partial Fisher–Yates draws the missing board cards and the random hole cards.
  4. Evaluate each active seat, compare scores, then tally per seat: win / tie (1/m equity share) / lose, plus a final-hand-category histogram.
  5. Thread-local totals are merged once per batch.
- **Worker loop**: batch size ≈ `rate × 50 ms`. Sleep to hold the target runs/sec. Log one line per second (e.g. `100,000 runs completed in 12 ms`). Drop results if the epoch changed.
- **Statistics**: 95% confidence interval `1.96·√(p(1−p)/n)`. Optional auto-pause when the hero's CI half-width falls below a target.
- **Snapshot** for the UI: totals, runs/sec, last batch time, paused flag, epoch.

### 5.5 Hand analysis (current hand, outs, draws)
Powers the "Your hand now" card and the "Hands you can get" table. It lives in the core library next to the evaluator.
- **Current hand**: evaluate hole + revealed board with the rules-aware evaluator. Returns the category, the winning 5 cards (for highlighting), and a readable description from the score ("Two Pair, Kings and Tens, Queen kicker"). Works with wild cards and custom rankings.
- **Outs (exact)**: for each unseen card, add it to the board and re-evaluate. The cards that upgrade the hero's category (or beat the current best score) are the outs. With at most 47 unseen cards this is tiny and exact, and it runs on the UI thread whenever the board or rules change. On the turn this gives "next card" outs. On the flop it also gives two-card combinations (about 1,000 evaluations), still instant.
- **Draw detection** for the "Needs" text, from rank and suit masks:
  - Flush draw (4 suited), backdoor flush draw (3 suited on the flop).
  - Open-ended straight draw, gutshot, double gutshot (uses the rules' straight table, so short deck works).
  - Pair, set, and overpair outs.
- **Reach probabilities**: from the Monte Carlo histogram of the hero's final category. `By river %` is the share of runs ending in that category, `Reach %` is the cumulative share at or above it. Before the flop these are the odds of making each hand by the river.
- **Opponent view**: the same analysis runs for any seat when selected as hero, so the table can show each seat's made hand.

### 5.6 Table state
- Seats 2–6: name, hole cards, known/random flag, stack, current bet, folded.
- The full 5-card board is dealt up front but only the revealed part is shown and simulated (no peeking).
- Actions: New Hand (posts blinds and antes, sets dealer), Next Street (flop → turn → river). Before each street change, the final equity of the street being left is saved into the equity history.
- `Betting.hpp`: min and max raise for each structure, used for the Rules panel preview and as the hook for future betting simulation.

---

## 6. UI specification

Main window uses a docking layout (resettable via View → Reset Layout). Layout is saved in `imgui.ini` under the user's pref path.

Default arrangement: Left column = Settings (top), Rules (bottom). Center = Table (top), Console (bottom). Right = Equity.

### Menu bar
File: Import Rules…, Export Rules…, Export Equity CSV, Quit. View: toggle each panel, Reset Layout. Help: shortcuts, About. Dropping a `.json` file onto the window imports it as rules.

### Panel 1 — Settings & Controls
- Buttons: **Restart**, **Pause Simulation** (becomes "Resume Simulation"), **Import Custom Rules (JSON)** (modal with a dropdown of `rules/*.json` plus a path field).
- Slider **Monte Carlo Speed (runs/sec)**: logarithmic, 1k → 20M, plus "Unlimited".
- Extras: thread count slider, seed input with "fixed seed" checkbox, auto-stop precision (±%), **Run Benchmark** button with last result shown, live status (runs, runs/sec, last batch ms).

### Panel 2 — Custom Rules Engine
Edits a **draft** copy; **Apply Rules** commits (highlighted when dirty), **Revert** discards. Applying rebuilds the evaluator and deals a new hand.
- **Variables**: sliders for Deck Size (24–52, snaps to multiples of 4), Small Blind, Big Blind, Ante, Starting Stack, Jokers (0–2).
- **Hand Rankings**: a checkbox per hand type (High Card locked on; Five of a Kind shown only with wilds), a ranking-order preset combo (Standard / Short Deck), and a read-only weakest→strongest list.
- **Wild Cards**: radio buttons: None, Deuces Wild, Jokers Wild, One-Eyed Jacks.
- **Betting Structure**: combo (No-Limit, Pot-Limit, Fixed-Limit), max raises for fixed-limit, and a live "max raise at current pot" preview.
- Rule presets combo (scans `rules/`), plus Export.

### Panel 3 — Real-time Odds & Equity
- Player selector (the "hero").
- Large readouts: **Win %**, **Split %**, **Lose %** (green / amber / red), with ±CI, total runs, and overall equity.
- ImPlot **multi-line chart**: one line per player. X axis = Preflop / Flop / Turn / River, Y axis = equity %. The current street's point updates live.
- **Your hand now** card: the best made hand from the hero's hole cards plus the revealed board, e.g. "Pair of Tens, Queen kicker", with the 5 cards that make it listed. Before the flop it shows the starting hand ("Q♥ 10♠, offsuit, unpaired"). It updates every street.
- **Hands you can get** table, one row per enabled hand type (Pair, Two Pair, Trips, Straight, Flush, Full House, Quads, Straight Flush, …). Columns:
  - `Have`: a check mark if the hero already holds this type or better.
  - `By river %`: chance of finishing with exactly this hand (from the Monte Carlo histogram).
  - `Reach %`: chance of finishing with this type or better.
  - `Outs`: number of cards that complete it on the next card (exact, from enumerating the remaining deck, not simulated).
  - `Needs`: a short text such as "any Ten or Queen", "one more heart".
  Rows the hero already has are highlighted. Types that can no longer be made are greyed out with 0%.
- ImPlot **horizontal bar chart**: the same "By river %" data (Pair, Two Pair, Flush, …), with a marker line on the hand type the hero currently has. Only enabled hand types are listed, in the rules' ranking order.
- **Panel tabs** (as in the reference screenshot): Equity | Hands | Odds | Simulation, so the line chart, the hand tables, and the pot-odds helper each get room.
- Pot-odds helper: enter pot and call amount to see required equity and EV of a call.
- Export equity history to CSV.

### Panel 4 — The Table & Players
- Everything drawn with `ImDrawList`: the table (rail and felt as polygon ellipses), cards (rounded rects), and suit symbols as procedural shapes (diamond, heart, spade, club), so no font glyphs are required.
- Center: community cards (Flop ×3, Turn, River) with empty slots outlined, plus the pot.
- Perimeter: 4–6 seat nodes showing hole cards, stack, current bet (chip near the center), dealer button, ACTIVE / FOLDED state, and live equity % with a mini bar. The hero is highlighted.
- Each seat also shows its **current made hand** as a short label under the name ("Pair of Tens", "Flush draw"), and the 5 cards that make the hero's hand are lifted or outlined on the table. At showdown the winner's hand is highlighted.
- Interaction: click a seat to toggle fold; right-click for a menu (fold, random/known cards, set hero). Header controls: players count, New Hand, Next Street.

### Panel 5 — Live Game Log & Console
- Scrolling, clipped list (`ImGuiListClipper`) of log lines with timestamp and level color (info, warn, error, sim, game).
- Examples: `100,000 runs completed in 12 ms (8.3M runs/s)`, `New hand #4, dealer seat 2`, `Flop: Ah Kd 7c`, `Rules loaded: Short Deck (6+)`.
- Auto-scroll toggle, level filter, text filter, Clear, Copy.

### Notes from the reference screenshot
What to keep:
- Left column for Settings, Rules in the middle, Equity on the right, Table below, Console at the bottom right.
- Big green Win / red Lose / grey Split readouts with a stacked green-blue-red bar under them.
- Equity chart with Flop / Turn / River on the X axis and one colored line per player.
- Seat nodes with an avatar circle, name, stack, bet, an Active (green) or Folded (red) badge, and equity % in green. Folded seats are dimmed.
- Community cards labelled "Community Cards" with empty outlined slots for cards not yet dealt.
- Log lines prefixed by source: `[Simulation]`, `[Game]`, `[Engine]`.

What to fix (visible in the screenshot):
- Garbled and duplicated labels ("Deck Size (0/52)" next to "Deck Size (36/52)", "Equiten Equity", "Burosrhlot"). Every label comes from one enum/string table so none are duplicated or misspelled.
- Equity axis shows -0.5 to 1.0. Equity is 0–100%, so the axis is fixed to that range.
- Equity numbers disagree (a seat shows 58% and 48% at once, and several seats show 58%). Equities across active seats must sum to 100%, and each seat shows one value.
- Hand-type checkboxes repeat ("Pair", "Pair"). One checkbox per hand type, from the category table.
- Folded Player 3 still shows a bet. Folded seats show no bet and no equity.
- The Console panel is cramped and the dock layout leaves a large empty area. Default layout fills the window (see Panel arrangement above).

---

## 7. Extra features added beyond the brief

1. **Confidence interval and auto-stop** so users see when the numbers have converged.
2. **Seeded, reproducible runs** (fixed-seed option).
3. **Built-in benchmark** (Mevals/s, thread scaling), which also validates the performance target.
4. **Wild-card support** (deuces, jokers, one-eyed jacks) with an analytic evaluator, including Five of a Kind.
5. **Short-deck and custom ranking orders** (flush > full house, etc.).
6. **Pot-odds and EV helper** next to the equity readout.
7. **Random-opponent mode** (a seat's cards hidden → equity vs. a random hand).
8. **Drag-and-drop rules import**, rules presets, and rules export.
9. **CSV export** of the equity history.
10. **Dockable, persistent layout** with a reset option.
11. **Current hand and "hands you can get"**: made-hand description, exact outs, draw detection, and by-river / reach probabilities for every hand type (see §5.5).
12. **Unit tests plus an exhaustive evaluator check** (see §9).
13. **Hooks for later**: exact enumeration when combos are few, hand-range input, Omaha, and a betting simulator.

---

## 8. Milestones

| # | Milestone | Done when |
|---|---|---|
| M0 | Skeleton | CMake + Ninja presets build an empty `pokercore` and a window with a dockspace |
| M1 | Cards, RNG, Rules | JSON load / save / validate round-trips; warnings logged |
| M2 | Evaluator (no wilds) | Passes known-hand tests and the full C(52,7) distribution check |
| M3 | Evaluator extras | Short deck, custom order, disabled categories, wild-card path matching brute force |
| M4 | Simulator | Multithreaded batches, snapshot, CI, speed control; benchmark meets the target |
| M5 | Table state + hand analysis | Deal, blinds, streets, fold, random hole cards, equity history; current hand, exact outs, draw detection |
| M6 | UI panels | Settings, Rules, Table, Equity, Console wired to the engine |
| M7 | Polish | Menu, drag-and-drop, CSV export, layout reset, README, packaging notes |

---

## 9. Verification plan

- **Unit tests**: card encoding, JSON parsing (valid, invalid, unknown keys), `sanitize()`, hand comparisons per category, kicker ordering, wheel straights in 52 and 36 card decks, ranking-order overrides, and disabled-category fall-through.
- **Exhaustive check**: enumerate all 133,784,560 seven-card hands and compare the category counts to the known distribution (straight flush 41,584; quads 224,848; full house 3,473,184; flush 4,047,644; straight 6,180,020; trips 6,461,620; two pair 31,433,400; pair 58,627,800; high card 23,294,460). Run on demand, not on every CI build.
- **Wild-card cross-check**: for random hands with 1–2 wilds, compare the analytic result to brute-force substitution through the non-wild evaluator.
- **Hand analysis**: known boards with a known number of outs (e.g. 9 outs for a flush draw on the flop, 8 for an open-ended straight draw), made-hand descriptions for every category, and a check that "By river %" from the simulator matches exact enumeration on the turn.
- **Simulator**: known matchups (e.g. AA vs KK preflop ≈ 81/19), a fixed-seed determinism test, and a scenario with all cards known (must give exactly 0/1/0.5 equity).
- **Performance**: benchmark must report ≥ 30M evals/s/core in Release (`-O3`, optionally `-march=native`).
- **UI**: manual checklist per panel, plus a run under ASan/UBSan for the core.

---

## 10. Risks and decisions to confirm

| Risk / question | Plan |
|---|---|
| ImGui, ImPlot and SDL2 version drift | Pin exact tags and commits in `Dependencies.cmake` |
| Docking API changes between ImGui versions | Pin `v1.91.9-docking`; isolate dock setup in `App.cpp` |
| Wild-card edge cases | Cross-check against brute force before trusting |
| Hand-ranking semantics when a type is disabled | Fall through to the next best enabled type (documented) |
| Game variant | v1 is Hold'em (2 hole cards). Omaha and others are out of scope. |
| Equity "by street" meaning | Equity is computed at each revealed street of the current hand and plotted as progression |
| Windows packaging | Out of scope for v1; build from source |

---

## 11. Decisions made

| Question | Decision |
|---|---|
| Platforms | Cross-platform from the start: Linux, Windows, macOS (see the cross-platform table in §2) |
| Betting simulation | Not in v1. The v1 table shows blinds, stacks, and bets only. Betting simulation is tracked in the backlog below. |
| Hole cards | Fixed at 2 for v1 (Hold'em). Omaha and others are tracked in the backlog. |

---

## 12. Task tracker

Status: `[ ]` not started, `[~]` in progress, `[x]` done. Update this table as work moves.

### v1 milestones
| ID | Task | Status |
|---|---|---|
| M0 | Skeleton: CMake + Ninja presets for all 3 OSes, empty window with dockspace | [~] headless core, presets, tests done; dockspace window waits for M6 deps |
| M1 | Cards, RNG, rules JSON (load / save / validate) | [x] |
| M2 | Evaluator without wilds, passes the C(52,7) distribution check | [ ] |
| M3 | Evaluator extras: short deck, custom order, disabled types, wild-card path | [ ] |
| M4 | Simulator: OpenMP batches, snapshot, CI, speed control, benchmark | [ ] |
| M5 | Table state + hand analysis (current hand, outs, draws) | [ ] |
| M6 | UI panels wired to the engine (Settings, Rules, Table, Equity, Console) | [ ] |
| M7 | Polish: menu, drag-and-drop, CSV export, layout reset, theme switch (classic felt / flat modern), README | [ ] |
| CI | GitHub Actions matrix (Linux, Windows, macOS) with tests and benchmark | [~] workflow written, untested on Windows/macOS; benchmark step waits for M4 |

### Backlog (deferred, tracked only)
| ID | Task | Status |
|---|---|---|
| B1 | Betting-action simulation (fold / call / raise per street, pot growth, side pots) | [ ] |
| B2 | Hand-range input (e.g. "top 15%") for opponents | [ ] |
| B3 | Exact enumeration mode when the number of combinations is small | [ ] |
| B4 | More variants: Omaha, hi-lo, variable hole-card count | [ ] |
| B5 | Strategy / solver hooks (CFR) | [ ] |
| B6 | Packaging: Windows installer, macOS app bundle, Linux AppImage | [ ] |
| B7 | Animation system (deal, flip, chip slide, equity easing) and sound | [ ] |

### Open items
None right now.
