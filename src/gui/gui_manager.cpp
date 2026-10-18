/**
 * @file gui_manager.cpp
 * @brief Master GUI Orchestrator, Main Menu Bar, and DockSpace Setup.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>

#include <filesystem>
#include <vector>

namespace em836 {

void GuiManager::Initialize(GuiState& state) {
    state.editorLanguage = 0; // Assembly
    state.disasmBaseAddress = 0x00000000;
    snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
    state.disasmFollowEip = true;

    // Load initial example program from disk
    std::filesystem::path defaultExample = FindExamplesDirectory() / "graphics_demo.as";
    bool loaded = false;
    if (std::filesystem::exists(defaultExample)) {
        loaded = LoadFileIntoEditor(state, defaultExample.string(), nullptr);
    }

    if (!loaded) {
        state.editorText = "; No example file found.\nstart:\n    mov eax, 1\n    int 0x88\n";
        state.editorFilePath = "";
        state.editorModified = false;
    }
}

void GuiManager::RenderMainMenuBar(Emulator& emu, GuiState& state, SDL_Renderer* renderer) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Program File...", "Ctrl+O")) {
                state.showFileOpenModal = true;
            }

            if (ImGui::MenuItem("Execution Setup / Load Options...")) {
                state.showLoadOptionsModal = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Settings...", "Ctrl+,")) {
                state.showSettingsModal = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // Exit can be signaled via SDL event or standard exit
                exit(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Emulation")) {
            CpuState cpu = emu.GetCpuStateSnapshot();
            bool isRunning = (cpu.status == EmulatorStatus::Running);

            if (isRunning) {
                if (ImGui::MenuItem("Pause", "F5")) {
                    emu.Pause();
                }
            } else {
                if (ImGui::MenuItem("Run / Resume", "F5")) {
                    emu.Start();
                }
            }

            if (ImGui::MenuItem("Single Step", "F10", false, !isRunning)) {
                emu.Step();
            }

            if (ImGui::MenuItem("Reset CPU & Memory", "Ctrl+R")) {
                emu.Reset();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Clear All Breakpoints")) {
                emu.ClearBreakpoints();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("CPU Registers", nullptr, &state.showRegisters);
            ImGui::MenuItem("Execution Controls", nullptr, &state.showExecution);
            ImGui::MenuItem("Disassembly", nullptr, &state.showDisassembly);
            ImGui::MenuItem("Memory Hex Viewer", nullptr, &state.showMemory);
            ImGui::MenuItem("VGA Display", nullptr, &state.showVga);
            ImGui::MenuItem("Console & Output", nullptr, &state.showConsole);
            ImGui::MenuItem("Code Editor", nullptr, &state.showEditor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About em836...")) {
                state.showAboutModal = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void GuiManager::RenderAll(Emulator& emu, GuiState& state, SDL_Renderer* renderer) {
    // Top Main Menu Bar
    RenderMainMenuBar(emu, state, renderer);

    // Initial Layout Settings (First Use Ever)
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 270), ImGuiCond_FirstUseEver);
    RenderRegistersWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(350, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 270), ImGuiCond_FirstUseEver);
    RenderExecutionWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(10, 310), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 280), ImGuiCond_FirstUseEver);
    RenderDisassemblyWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(10, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 270), ImGuiCond_FirstUseEver);
    RenderMemoryWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(640, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 420), ImGuiCond_FirstUseEver);
    RenderVgaWindow(emu, state, renderer);

    ImGui::SetNextWindowPos(ImVec2(640, 460), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 410), ImGuiCond_FirstUseEver);
    RenderConsoleWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(1100, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 840), ImGuiCond_FirstUseEver);
    RenderEditorWindow(emu, state);

    // Render Modals
    RenderSettingsModal(emu, state);
    RenderLoadOptionsModal(emu, state);
    RenderFileOpenModal(emu, state);
    RenderAboutModal(state);
}

} // namespace em836
