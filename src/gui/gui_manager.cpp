/**
 * @file gui_manager.cpp
 * @brief Master GUI Orchestrator, Main Menu Bar, and DockSpace Setup.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>

namespace em836 {

// Initial default code loaded into the in-GUI editor
static const char* DEFAULT_EDITOR_CODE =
"; =============================================================\n"
"; em836 Assembly Program Sample\n"
"; Draws graphics and prints message to virtual terminal\n"
"; =============================================================\n"
"\n"
"start:\n"
"    ; Switch to 640x480 Graphics Mode\n"
"    mov eax, 0x40           ; SYS_GFX_SET_MODE\n"
"    mov ebx, 1              ; 1 = Graphics 640x480\n"
"    int 0x88\n"
"\n"
"    ; Clear screen to Black (Color 0)\n"
"    mov eax, 0x41           ; SYS_GFX_CLEAR\n"
"    mov ebx, 0\n"
"    int 0x88\n"
"\n"
"    ; Draw a Cyan header rectangle (Color 3)\n"
"    mov eax, 0x44           ; SYS_GFX_FILL_RECT\n"
"    mov ebx, 40             ; X\n"
"    mov ecx, 30             ; Y\n"
"    mov edx, 560            ; Width\n"
"    mov esi, 80             ; Height\n"
"    mov edi, 3              ; Color 3 (Cyan)\n"
"    int 0x88\n"
"\n"
"    ; Draw an intersecting Yellow diagonal line (Color 14)\n"
"    mov eax, 0x43           ; SYS_GFX_LINE\n"
"    mov ebx, 40\n"
"    mov ecx, 30\n"
"    mov edx, 600\n"
"    mov esi, 110\n"
"    mov edi, 14\n"
"    int 0x88\n"
"\n"
"    ; Print greeting string to terminal\n"
"    mov eax, 0x11           ; SYS_PRINT_STR\n"
"    mov ebx, hello_msg\n"
"    mov ecx, 0\n"
"    int 0x88\n"
"\n"
"    ; Clean program exit\n"
"    mov eax, 0x01           ; SYS_EXIT\n"
"    mov ebx, 0\n"
"    int 0x88\n"
"\n"
"hello_msg:\n"
"    db \"[Hello] em836 i386 Virtual Machine Running!\", 10, 0\n";

void GuiManager::Initialize(GuiState& state) {
    state.editorText = DEFAULT_EDITOR_CODE;
    state.editorLanguage = 0; // Assembly
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
