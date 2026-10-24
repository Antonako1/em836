/**
 * @file gui_execution.cpp
 * @brief Execution Controls, Clock Speed Throttling, and VM Lifecycle Management.
 */

#include "em836/gui.hpp"
#include <imgui.h>

namespace em836 {

void GuiManager::RenderExecutionWindow(Emulator& emu, GuiState& state) {
    if (!state.showExecution) return;

    if (ImGui::Begin("Execution Controls", &state.showExecution)) {
        CpuState cpu = emu.GetCpuStateSnapshot();

        // -------------------------------------------------------------
        // Primary Execution Buttons
        // -------------------------------------------------------------
        bool isRunning = (cpu.status == EmulatorStatus::Running);

        if (isRunning) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button(" Pause ", ImVec2(70, 30))) {
                emu.Pause();
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("  Run  ", ImVec2(70, 30))) {
                emu.Start();
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(isRunning);
        if (ImGui::Button(" Step In ", ImVec2(65, 30))) {
            emu.StepInto();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Step Into next instruction (F11)");
        }

        ImGui::SameLine();
        if (ImGui::Button(" Step Over ", ImVec2(75, 30))) {
            emu.StepOver();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Step Over subroutine/call (F10)");
        }

        ImGui::SameLine();
        if (ImGui::Button(" Step Out ", ImVec2(70, 30))) {
            emu.StepOut();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Step Out to return address in caller (Shift+F11)");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button(" Reset ", ImVec2(60, 30))) {
            emu.Reset();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();

        // -------------------------------------------------------------
        // Clock Speed Throttling Dropdown
        // -------------------------------------------------------------
        uint32_t currentSpeed = emu.GetClockSpeed();
        const char* speedLabels[] = {
            "Unlimited (Max)",
            "10 Hz",
            "100 Hz",
            "1 kHz",
            "10 kHz",
            "100 kHz",
            "1 MHz"
        };
        const uint32_t speedValues[] = { 0, 10, 100, 1000, 10000, 100000, 1000000 };

        int selectedSpeedIndex = 0;
        for (int i = 0; i < 7; ++i) {
            if (speedValues[i] == currentSpeed) {
                selectedSpeedIndex = i;
                break;
            }
        }

        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("Clock Speed", &selectedSpeedIndex, speedLabels, IM_ARRAYSIZE(speedLabels))) {
            emu.SetClockSpeed(speedValues[selectedSpeedIndex]);
        }

        // -------------------------------------------------------------
        // CPU Architecture Mode Selector
        // -------------------------------------------------------------
        uint32_t currentMode = emu.GetCpuMode();
        int modeIndex = (currentMode == 16) ? 1 : 0;
        const char* modeLabels[] = { "32-bit Protected Mode", "16-bit Real Mode" };

        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("CPU Architecture", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
            emu.SetCpuMode((modeIndex == 1) ? 16 : 32);
        }

        ImGui::Spacing();
        ImGui::Separator();

        // -------------------------------------------------------------
        // Status Badge
        // -------------------------------------------------------------
        ImGui::Text("Status: ");
        ImGui::SameLine();
        switch (cpu.status) {
            case EmulatorStatus::Running:
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[ RUNNING ]");
                break;
            case EmulatorStatus::Paused:
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[ PAUSED ]");
                break;
            case EmulatorStatus::Stepping:
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[ STEPPING ]");
                break;
            case EmulatorStatus::Halted:
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ HALTED ]");
                break;
            case EmulatorStatus::Error:
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[ ERROR ]");
                break;
            default:
                ImGui::TextDisabled("[ STOPPED ]");
                break;
        }

        ImGui::Text("Active EIP: 0x%08X", cpu.eip);
    }
    ImGui::End();
}

} // namespace em836
