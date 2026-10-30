#define SDL_MAIN_HANDLED
#include "ui/App.hpp"

#include <algorithm>
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

void applyStyle(float dpi) {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 4.0f;
    st.FrameRounding = 4.0f;
    st.GrabRounding = 4.0f;
    st.TabRounding = 4.0f;
    st.FramePadding = {6.0f, 4.0f};
    st.ItemSpacing = {8.0f, 6.0f};
    st.Colors[ImGuiCol_WindowBg] = ImVec4{0.09f, 0.10f, 0.12f, 1.0f};
    st.Colors[ImGuiCol_TitleBgActive] = ImVec4{0.16f, 0.22f, 0.30f, 1.0f};
    st.Colors[ImGuiCol_Tab] = ImVec4{0.14f, 0.17f, 0.22f, 1.0f};
    st.Colors[ImGuiCol_TabSelected] = ImVec4{0.22f, 0.36f, 0.52f, 1.0f};
    st.ScaleAllSizes(dpi);
}

void drawMenuBar(AppState& app) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", "Alt+F4")) app.quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem(PanelTitles::Settings, nullptr, &app.visible.settings);
        ImGui::MenuItem(PanelTitles::Rules, nullptr, &app.visible.rules);
        ImGui::MenuItem(PanelTitles::Table, nullptr, &app.visible.table);
        ImGui::MenuItem(PanelTitles::Equity, nullptr, &app.visible.equity);
        ImGui::MenuItem(PanelTitles::Console, nullptr, &app.visible.console);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) app.layoutResetRequested = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
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
    applyStyle(scale.dpi);

    ImGui_ImplSDL2_InitForOpenGL(win.window, win.gl);
    ImGui_ImplOpenGL3_Init(glsl);

    {
        AppState app(findRulesDir(opt.rulesDir));
        app.forceTab = opt.tab;
        if (opt.fixedSeed) app.setSeed(true, opt.seed);
        if (opt.players > 0) app.setSeatCount(opt.players);
        for (int seat : opt.foldSeats)
            if (seat >= 1 && seat <= app.table.seatCount()) app.toggleFold(seat - 1);
        if (opt.fixedSeed || opt.players > 0) app.restart();
        app.layoutResetRequested = !haveSavedLayout;

        const ImGuiID dockId = ImHashStr(kDockName);
        bool running = true;
        for (int frame = 0; running; ++frame) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                    e.window.windowID == SDL_GetWindowID(win.window))
                    running = false;
            }
            if (app.quitRequested) running = false;

            // Scripted runs press Next Street at fixed frame counts.
            if (opt.advanceStreets > 0 && frame > 0 && frame % 30 == 0 && frame / 30 <= opt.advanceStreets)
                app.nextStreet();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            app.frame();
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

            ImGui::Render();
            int dw = 0, dh = 0;
            SDL_GL_GetDrawableSize(win.window, &dw, &dh);
            glViewport(0, 0, dw, dh);
            glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            const bool last = opt.maxFrames > 0 && frame + 1 >= opt.maxFrames;
            if (last && !opt.screenshotPath.empty() && !savePpm(opt.screenshotPath, dw, dh))
                std::fprintf(stderr, "could not write %s\n", opt.screenshotPath.c_str());
            SDL_GL_SwapWindow(win.window);
            if (last) running = false;
            if (opt.maxFrames > 0) SDL_Delay(8);  // let the simulation thread make progress between frames
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
