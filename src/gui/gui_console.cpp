/**
 * @file gui_console.cpp
 * @brief Dual-Purpose Virtual Terminal and AstraC Build Log Inspector.
 */

#include "em836/gui.hpp"
#include <imgui.h>

namespace em836 {

void GuiManager::RenderConsoleWindow(Emulator& emu, GuiState& state) {
    if (!state.showConsole) return;

    if (ImGui::Begin("Console & Output", &state.showConsole)) {
        if (ImGui::BeginTabBar("console_tab_bar")) {
            // -------------------------------------------------------------
            // Tab 1: Virtual Terminal (Output & Input)
            // -------------------------------------------------------------
            if (ImGui::BeginTabItem("Virtual Terminal")) {
                bool autoScroll = emu.GetConsole().GetAutoScroll();
                if (ImGui::Checkbox("Auto-scroll", &autoScroll)) {
                    emu.GetConsole().SetAutoScroll(autoScroll);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Terminal")) {
                    emu.GetConsole().Clear();
                }

                ImGui::Separator();

                // Scrollable Terminal Output Region
                float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
                ImGui::BeginChild("terminal_scroll_area", ImVec2(0, -footerHeight), true);

                std::string output = emu.GetConsole().GetOutputText();
                ImGui::TextUnformatted(output.c_str());

                if (emu.GetConsole().GetAutoScroll() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();

                // Interactive Input Prompt
                ImGui::PushItemWidth(-60);
                bool reclaimFocus = false;
                if (ImGui::InputText("##console_input", state.consoleInputBuffer, sizeof(state.consoleInputBuffer),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                    std::string line(state.consoleInputBuffer);
                    if (!line.empty()) {
                        emu.GetConsole().ProcessInputLine(line, emu);
                        state.consoleInputBuffer[0] = '\0';
                        reclaimFocus = true;
                    }
                }
                ImGui::PopItemWidth();

                ImGui::SetItemDefaultFocus();
                if (reclaimFocus) {
                    ImGui::SetKeyboardFocusHere(-1);
                }

                ImGui::SameLine();
                if (ImGui::Button("Send")) {
                    std::string line(state.consoleInputBuffer);
                    if (!line.empty()) {
                        emu.GetConsole().ProcessInputLine(line, emu);
                        state.consoleInputBuffer[0] = '\0';
                    }
                }

                ImGui::EndTabItem();
            }

            // -------------------------------------------------------------
            // Tab 2: AstraC Build Log
            // -------------------------------------------------------------
            if (ImGui::BeginTabItem("AstraC Build Log")) {
                if (ImGui::Button("Clear Log")) {
                    emu.GetConsole().ClearBuildLog();
                }

                ImGui::Separator();

                ImGui::BeginChild("build_log_scroll_area", ImVec2(0, 0), true);
                std::string buildLog = emu.GetConsole().GetBuildLog();
                if (buildLog.empty()) {
                    ImGui::TextDisabled("No compiler activity recorded yet.");
                } else {
                    ImGui::TextUnformatted(buildLog.c_str());
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace em836
