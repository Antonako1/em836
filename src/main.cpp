/**
 * @file main.cpp
 * @brief Application Entry Point, SDL3 Window Management, and Event Loop for em836.
 */

#include "em836/emulator.hpp"
#include "em836/astrac.hpp"
#include "em836/gui.hpp"
#include "em836/config.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>

int main(int argc, char* argv[]) {
    // -------------------------------------------------------------------------
    // 1. Initialize SDL3 Subsystems
    // -------------------------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return -1;
    }

    // Create desktop window with high DPI and resizable docking canvas
    const int initialWidth = 1440;
    const int initialHeight = 900;
    SDL_Window* window = SDL_CreateWindow(
        "em836 — Interactive i386 Emulator & Debugger",
        initialWidth,
        initialHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!window) {
        std::cerr << "Failed to create SDL3 window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // Create SDL3 hardware accelerated renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "Failed to create SDL3 renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // -------------------------------------------------------------------------
    // 2. Initialize Dear ImGui
    // -------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Apply dark color theme
    ImGui::StyleColorsDark();

    // Adjust theme styling for clean IDE aesthetics
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;

    // Initialize ImGui SDL3 and SDL_Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // -------------------------------------------------------------------------
    // 3. Initialize Emulator Engine
    // -------------------------------------------------------------------------
    em836::EmulatorConfig config = em836::ConfigManager::Load();
    em836::Emulator emulator;
    if (!emulator.Initialize(config)) {
        std::cerr << "Failed to initialize em836 emulator core." << std::endl;
    }

    em836::GuiState guiState;
    em836::GuiManager::Initialize(guiState);

    // Handle CLI argument (e.g. em836.exe my_file.as or app.bin)
    if (argc > 1) {
        std::string cliPath = argv[1];
        emulator.GetConsole().WriteText("[CLI] Loading program from command-line: " + cliPath + "\n");
        em836::AstraCToolchain::IngestFile(cliPath, emulator, config.astracPath);
        guiState.disasmBaseAddress = emulator.GetCodeBaseAddress();
        snprintf(guiState.disasmAddressInput, sizeof(guiState.disasmAddressInput), "%08X", guiState.disasmBaseAddress);
        guiState.disasmFollowEip = true;
    }

    // -------------------------------------------------------------------------
    // 4. Main Event & Render Loop
    // -------------------------------------------------------------------------
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Forward event to Dear ImGui
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }

            // Drag and drop ingestion handler
            if (event.type == SDL_EVENT_DROP_FILE) {
                if (event.drop.data) {
                    std::string droppedPath = event.drop.data;
                    emulator.GetConsole().WriteText("[Drop] Ingesting file: " + droppedPath + "\n");

                    std::filesystem::path p(droppedPath);
                    std::string ext = p.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (ext == ".as" || ext == ".asm" || ext == ".ac") {
                        em836::GuiManager::LoadFileIntoEditor(guiState, droppedPath, &emulator);
                    }

                    if (em836::AstraCToolchain::IngestFile(droppedPath, emulator, emulator.GetConfig().astracPath)) {
                        guiState.disasmBaseAddress = emulator.GetCpuStateSnapshot().eip;
                        snprintf(guiState.disasmAddressInput, sizeof(guiState.disasmAddressInput), "%08X", guiState.disasmBaseAddress);
                        guiState.disasmFollowEip = true;
                    }
                }
            }

            // Global Keyboard Shortcuts
            if (event.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard) {
                if (event.key.key == SDLK_F5) {
                    // Toggle Run / Pause
                    em836::CpuState state = emulator.GetCpuStateSnapshot();
                    if (state.status == em836::EmulatorStatus::Running) {
                        emulator.Pause();
                    } else {
                        emulator.Start();
                    }
                } else if (event.key.key == SDLK_F10) {
                    // Single Step
                    emulator.Step();
                } else if (event.key.key == SDLK_R && (event.key.mod & SDL_KMOD_CTRL)) {
                    // Reset
                    emulator.Reset();
                } else if (event.key.key == SDLK_L && (event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_SHIFT)) {
                    // Reset Default GUI Layout
                    guiState.requestLayoutReset = true;
                }
            }
        }

        // Start Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Render all docking views and tools
        em836::GuiManager::RenderAll(emulator, guiState, renderer);

        // Render draw data
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // -------------------------------------------------------------------------
    // 5. Clean Shutdown
    // -------------------------------------------------------------------------
    emulator.Shutdown();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
