#define SDL_MAIN_HANDLED
#include "ui/App.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder: the default layout; kept in this file only
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include "ui/AppState.hpp"
#include "ui/DrawUtil.hpp"
#include "ui/Panels.hpp"

namespace pf::ui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kDockName = "PokerForgeDock";

struct SdlWindow {
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    ~SdlWindow() {
        if (gl) SDL_GL_DeleteContext(gl);
        if (window) SDL_DestroyWindow(window);
    }
};

// Display scale: the DPI factor for sizes, and the framebuffer ratio for crisp fonts.
struct Scale {
    float dpi = 1.0f;
    float pixelRatio = 1.0f;
};

Scale measureScale(SDL_Window* w) {
    Scale s;
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(w, &ww, &wh);
    SDL_GL_GetDrawableSize(w, &dw, &dh);
    if (ww > 0) s.pixelRatio = static_cast<float>(dw) / static_cast<float>(ww);
#ifndef __APPLE__
    float ddpi = 0;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(w), &ddpi, nullptr, nullptr) == 0 && ddpi > 0)
        s.dpi = ddpi / 96.0f;
#endif
    s.dpi = std::clamp(s.dpi, 1.0f, 3.0f);
    s.pixelRatio = std::clamp(s.pixelRatio, 1.0f, 4.0f);
    return s;
}

std::string executableDir() {
    char* base = SDL_GetBasePath();
    std::string out = base ? base : "";
    SDL_free(base);
    return out;
}

std::string findRulesDir(const std::string& requested) {
    if (!requested.empty()) return requested;
    const fs::path beside = fs::path(executableDir()) / "rules";
    if (fs::exists(beside)) return beside.string();
#ifdef PF_SOURCE_RULES_DIR
    if (fs::exists(PF_SOURCE_RULES_DIR)) return PF_SOURCE_RULES_DIR;
#endif
    return beside.string();
}

// Left column: Settings over Rules. Centre: Table over Console. Right: Equity.
void buildDefaultLayout(ImGuiID dock, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, size);

    ImGuiID rest = dock, left = 0, right = 0, centre = 0;
    left = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.21f, nullptr, &rest);
    right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.34f, nullptr, &centre);
    ImGuiID leftBottom = 0;
    const ImGuiID leftTop = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, nullptr, &leftBottom);
    ImGuiID centreBottom = 0;
    const ImGuiID centreTop = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Up, 0.74f, nullptr, &centreBottom);

    ImGui::DockBuilderDockWindow(PanelTitles::Settings, leftTop);
    ImGui::DockBuilderDockWindow(PanelTitles::Rules, leftBottom);
    ImGui::DockBuilderDockWindow(PanelTitles::Table, centreTop);
    ImGui::DockBuilderDockWindow(PanelTitles::Console, centreBottom);
    ImGui::DockBuilderDockWindow(PanelTitles::Equity, right);
    ImGui::DockBuilderFinish(dock);
}

// Both themes start from the same base; only colours and corner rounding differ.
void applyTheme(Theme theme, float dpi) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();  // forget the previous theme (and the previous DPI scaling)
    ImGui::StyleColorsDark();
    st.FramePadding = {6.0f, 4.0f};
    st.ItemSpacing = {8.0f, 6.0f};
    auto set = [&](ImGuiCol c, float r, float g, float b) { st.Colors[c] = ImVec4{r, g, b, 1.0f}; };

    if (theme == Theme::FlatModern) {
        st.WindowRounding = st.FrameRounding = st.GrabRounding = st.TabRounding = st.PopupRounding = 2.0f;
        st.WindowBorderSize = st.FrameBorderSize = 1.0f;
        set(ImGuiCol_WindowBg, 0.10f, 0.11f, 0.13f);
        set(ImGuiCol_PopupBg, 0.12f, 0.13f, 0.16f);
        set(ImGuiCol_Border, 0.20f, 0.23f, 0.28f);
        set(ImGuiCol_FrameBg, 0.15f, 0.17f, 0.21f);
        set(ImGuiCol_FrameBgHovered, 0.19f, 0.22f, 0.27f);
        set(ImGuiCol_FrameBgActive, 0.22f, 0.26f, 0.32f);
        set(ImGuiCol_TitleBg, 0.08f, 0.09f, 0.11f);
        set(ImGuiCol_TitleBgActive, 0.12f, 0.14f, 0.17f);
        set(ImGuiCol_Button, 0.15f, 0.55f, 0.50f);
        set(ImGuiCol_ButtonHovered, 0.18f, 0.66f, 0.60f);
        set(ImGuiCol_ButtonActive, 0.12f, 0.45f, 0.41f);
        set(ImGuiCol_Header, 0.15f, 0.40f, 0.38f);
        set(ImGuiCol_HeaderHovered, 0.18f, 0.50f, 0.46f);
        set(ImGuiCol_HeaderActive, 0.15f, 0.55f, 0.50f);
        set(ImGuiCol_SliderGrab, 0.20f, 0.72f, 0.65f);
        set(ImGuiCol_SliderGrabActive, 0.30f, 0.82f, 0.75f);
        set(ImGuiCol_CheckMark, 0.30f, 0.85f, 0.78f);
        set(ImGuiCol_Tab, 0.12f, 0.14f, 0.17f);
        set(ImGuiCol_TabHovered, 0.18f, 0.50f, 0.46f);
        set(ImGuiCol_TabSelected, 0.15f, 0.55f, 0.50f);
        set(ImGuiCol_TabDimmed, 0.10f, 0.11f, 0.14f);
        set(ImGuiCol_TabDimmedSelected, 0.13f, 0.30f, 0.30f);
    } else {
        st.WindowRounding = st.FrameRounding = st.GrabRounding = st.TabRounding = 4.0f;
        set(ImGuiCol_WindowBg, 0.09f, 0.10f, 0.12f);
        set(ImGuiCol_TitleBgActive, 0.16f, 0.22f, 0.30f);
        set(ImGuiCol_Tab, 0.14f, 0.17f, 0.22f);
        set(ImGuiCol_TabSelected, 0.22f, 0.36f, 0.52f);
    }
    st.ScaleAllSizes(dpi);
}

void drawMenuBar(AppState& app) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import Rules...", "Ctrl+O")) app.dialog = Dialog::ImportRules;
        if (ImGui::MenuItem("Export Rules...", "Ctrl+S")) app.dialog = Dialog::ExportRules;
        if (ImGui::MenuItem("Export Equity CSV...", "Ctrl+E")) app.dialog = Dialog::ExportCsv;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) app.quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Game")) {
        if (ImGui::MenuItem("New Hand", "F2")) app.newHand();
        if (ImGui::MenuItem("Next Street", "F3", false, app.table.street() != Street::River)) app.nextStreet();
        if (ImGui::MenuItem(app.paused ? "Resume Simulation" : "Pause Simulation", "F4")) app.setPaused(!app.paused);
        if (ImGui::MenuItem("Restart Simulation", "F5")) app.restart();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem(PanelTitles::Settings, nullptr, &app.visible.settings);
        ImGui::MenuItem(PanelTitles::Rules, nullptr, &app.visible.rules);
        ImGui::MenuItem(PanelTitles::Table, nullptr, &app.visible.table);
        ImGui::MenuItem(PanelTitles::Equity, nullptr, &app.visible.equity);
        ImGui::MenuItem(PanelTitles::Console, nullptr, &app.visible.console);
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            for (Theme t : {Theme::ClassicFelt, Theme::FlatModern})
                if (ImGui::MenuItem(std::string(themeDisplay(t)).c_str(), nullptr, app.theme == t)) app.theme = t;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset Layout")) app.layoutResetRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts", "F1")) app.dialog = Dialog::Shortcuts;
        if (ImGui::MenuItem("About PokerForge")) app.dialog = Dialog::About;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// Function keys and Ctrl chords, so they never collide with typing in a text box.
void handleShortcuts(AppState& app) {
    if (app.dialog != Dialog::None) return;
    const ImGuiIO& io = ImGui::GetIO();
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (pressed(ImGuiKey_F1)) app.dialog = Dialog::Shortcuts;
    if (pressed(ImGuiKey_F2)) app.newHand();
    if (pressed(ImGuiKey_F3) && app.table.street() != Street::River) app.nextStreet();
    if (pressed(ImGuiKey_F4)) app.setPaused(!app.paused);
    if (pressed(ImGuiKey_F5)) app.restart();
    if (io.KeyCtrl && pressed(ImGuiKey_O)) app.dialog = Dialog::ImportRules;
    if (io.KeyCtrl && pressed(ImGuiKey_S)) app.dialog = Dialog::ExportRules;
    if (io.KeyCtrl && pressed(ImGuiKey_E)) app.dialog = Dialog::ExportCsv;
}

// Turns "f3" or "ctrl+e" into the SDL key event a keyboard would send.
bool pushKeyEvent(SDL_Window* window, const std::string& name, bool down) {
    SDL_Keycode key = SDLK_UNKNOWN;
    Uint16 mod = KMOD_NONE;
    std::string k = name;
    if (k.rfind("ctrl+", 0) == 0) { mod = KMOD_LCTRL; k = k.substr(5); }
    if (k.size() >= 2 && k[0] == 'f' && std::isdigit(static_cast<unsigned char>(k[1])))
        key = SDLK_F1 + (std::atoi(k.c_str() + 1) - 1);
    else if (k.size() == 1 && k[0] >= 'a' && k[0] <= 'z')
        key = static_cast<SDL_Keycode>(k[0]);
    if (key == SDLK_UNKNOWN) return false;
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    e.key.keysym.mod = mod;
    e.key.windowID = SDL_GetWindowID(window);  // ImGui drops events addressed to another window
    return SDL_PushEvent(&e) == 1;
}

Dialog dialogFromName(const std::string& name) {
    if (name == "import") return Dialog::ImportRules;
    if (name == "export") return Dialog::ExportRules;
    if (name == "csv") return Dialog::ExportCsv;
    if (name == "about") return Dialog::About;
    if (name == "shortcuts") return Dialog::Shortcuts;
    return Dialog::None;
}

bool savePpm(const std::string& path, int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)  // GL rows run bottom to top
        out.write(reinterpret_cast<const char*>(px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3),
                  static_cast<std::streamsize>(w) * 3);
    return static_cast<bool>(out);
}

}  // namespace

int runApp(const AppOptions& opt) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__APPLE__)
    const char* glsl = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SdlWindow win;
    win.window = SDL_CreateWindow("PokerForge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width, opt.height,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    win.gl = SDL_GL_CreateContext(win.window);
    if (!win.gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(win.window, win.gl);
    SDL_GL_SetSwapInterval(opt.maxFrames > 0 ? 0 : 1);

    // ---- Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    // Layout and window positions persist in the user's pref folder.
    static std::string iniPath;
    if (char* pref = SDL_GetPrefPath("PokerForge", "PokerForge")) {
        iniPath = (fs::path(pref) / "imgui.ini").string();
        SDL_free(pref);
        io.IniFilename = iniPath.c_str();
    } else {
        io.IniFilename = nullptr;
    }
    const bool haveSavedLayout = !iniPath.empty() && fs::exists(iniPath) && opt.maxFrames == 0;
    if (opt.maxFrames > 0) io.IniFilename = nullptr;  // scripted runs never touch the user's layout

    const Scale scale = measureScale(win.window);
    ImFontConfig fontCfg;
    fontCfg.SizePixels = 14.0f * scale.dpi * scale.pixelRatio;
    io.Fonts->AddFontDefault(&fontCfg);
    io.FontGlobalScale = 1.0f / scale.pixelRatio;

    ImGui_ImplSDL2_InitForOpenGL(win.window, win.gl);
    ImGui_ImplOpenGL3_Init(glsl);

    {
        AppState app(findRulesDir(opt.rulesDir));

        // Settings persist in the user's pref folder; scripted runs neither read nor write them.
        std::string settingsPath;
        if (char* pref = SDL_GetPrefPath("PokerForge", "PokerForge")) {
            if (opt.maxFrames == 0) settingsPath = (fs::path(pref) / "settings.json").string();
            app.exportDir = (fs::path(pref) / "exports").string();
            SDL_free(pref);
        }
        if (!opt.settingsPath.empty()) settingsPath = opt.settingsPath;
        if (!settingsPath.empty()) {
            std::vector<std::string> warnings;
            app.applySettings(loadSettings(settingsPath, &warnings));
            for (const std::string& w : warnings) logWarn("[Engine] " + w);
        }
        if (opt.theme >= 0) app.theme = opt.theme == 1 ? Theme::FlatModern : Theme::ClassicFelt;
        app.dialog = dialogFromName(opt.dialog);
        app.forceTab = opt.tab;
        if (opt.fixedSeed) app.setSeed(true, opt.seed);
        if (opt.players > 0) app.setSeatCount(opt.players);
        for (int seat : opt.foldSeats)
            if (seat >= 1 && seat <= app.table.seatCount()) app.toggleFold(seat - 1);
        if (opt.fixedSeed || opt.players > 0) app.restart();
        app.layoutResetRequested = !haveSavedLayout;
        Theme appliedTheme = app.theme;
        applyTheme(appliedTheme, scale.dpi);

        const ImGuiID dockId = ImHashStr(kDockName);
        bool running = true;
        for (int frame = 0; running; ++frame) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_DROPFILE && e.drop.file) {
                    app.handleDroppedFile(e.drop.file);
                    SDL_free(e.drop.file);
                }
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                    e.window.windowID == SDL_GetWindowID(win.window))
                    running = false;
            }
            if (app.quitRequested) running = false;

            // Scripted input goes through the same SDL event queue a user's would.
            if (!opt.dropFile.empty() && frame == 5) {
                SDL_Event drop{};
                drop.type = SDL_DROPFILE;
                drop.drop.file = SDL_strdup(opt.dropFile.c_str());
                SDL_PushEvent(&drop);
            }
            for (std::size_t k = 0; k < opt.keys.size(); ++k) {
                const int start = 10 + 6 * static_cast<int>(k);
                if (frame == start && !pushKeyEvent(win.window, opt.keys[k], true))
                    std::fprintf(stderr, "unknown key \"%s\"\n", opt.keys[k].c_str());
                if (frame == start + 2) pushKeyEvent(win.window, opt.keys[k], false);
            }

            // Scripted runs press Next Street at fixed frame counts.
            if (opt.advanceStreets > 0 && frame > 0 && frame % 30 == 0 && frame / 30 <= opt.advanceStreets)
                app.nextStreet();

            if (opt.toggleThemeAtFrame > 0 && frame == opt.toggleThemeAtFrame)
                app.theme = app.theme == Theme::ClassicFelt ? Theme::FlatModern : Theme::ClassicFelt;
            if (app.theme != appliedTheme) {  // restyle between frames, never in the middle of one
                appliedTheme = app.theme;
                applyTheme(appliedTheme, scale.dpi);
                if (!settingsPath.empty()) saveSettings(app.currentSettings(), settingsPath);
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            app.frame();
            handleShortcuts(app);
            drawMenuBar(app);
            ImGui::DockSpaceOverViewport(dockId, ImGui::GetMainViewport());
            if (app.layoutResetRequested) {
                app.visible = PanelVisibility{};
                buildDefaultLayout(dockId, ImGui::GetMainViewport()->WorkSize);
                app.layoutResetRequested = false;
            }
            drawSettingsPanel(app);
            drawRulesPanel(app);
            drawTablePanel(app);
            drawEquityPanel(app);
            drawConsolePanel(app);
            drawDialogs(app);

            ImGui::Render();
            int dw = 0, dh = 0;
            SDL_GL_GetDrawableSize(win.window, &dw, &dh);
            glViewport(0, 0, dw, dh);
            const ImVec4 clear = themeColors(app.theme).clear;
            glClearColor(clear.x, clear.y, clear.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            const bool last = opt.maxFrames > 0 && frame + 1 >= opt.maxFrames;
            if (last && !opt.screenshotPath.empty() && !savePpm(opt.screenshotPath, dw, dh))
                std::fprintf(stderr, "could not write %s\n", opt.screenshotPath.c_str());
            SDL_GL_SwapWindow(win.window);
            if (last) running = false;
            if (opt.maxFrames > 0) SDL_Delay(8);  // let the simulation thread make progress between frames
        }
        if (!settingsPath.empty()) {
            std::string err;
            if (!saveSettings(app.currentSettings(), settingsPath, &err)) std::fprintf(stderr, "%s\n", err.c_str());
        }
    }  // AppState (and its worker thread) goes away before the GL context

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_Quit();
    return 0;
}

}  // namespace pf::ui
