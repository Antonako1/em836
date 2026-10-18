/**
 * @file gui_registers.cpp
 * @brief CPU Register Inspector and Interactive EFLAGS Badges.
 */

#include "em836/gui.hpp"
#include <imgui.h>
#include <sstream>
#include <iomanip>

namespace em836 {

// Helper to render an interactive flag badge
static void RenderFlagBadge(const char* name, bool isSet, uint32_t bitMask, CpuState& state, Emulator& emu) {
    ImVec4 color = isSet ? ImVec4(0.2f, 0.75f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.15f, color.y + 0.15f, color.z + 0.15f, 1.0f));

    if (ImGui::SmallButton(name)) {
        if (state.status == EmulatorStatus::Paused) {
            state.eflags ^= bitMask;
            emu.ApplyCpuState(state);
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Flag: %s = %d (Click to toggle while paused)", name, isSet ? 1 : 0);
    }

    ImGui::PopStyleColor(2);
    ImGui::SameLine();
}

// Helper to render an editable 32-bit register field
static void RenderRegisterRow(const char* label, uint32_t& regVal, Emulator& emu, CpuState& state) {
    ImGui::Text("%s:", label);
    ImGui::SameLine(45);
    char buf[16];
    snprintf(buf, sizeof(buf), "%08X", regVal);

    ImGui::SetNextItemWidth(75);
    std::string id = "##reg_" + std::string(label);
    if (ImGui::InputText(id.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (state.status == EmulatorStatus::Paused || state.status == EmulatorStatus::Halted) {
            regVal = static_cast<uint32_t>(strtoul(buf, nullptr, 16));
            emu.ApplyCpuState(state);
        }
    }
}

void GuiManager::RenderRegistersWindow(Emulator& emu, GuiState& state) {
    if (!state.showRegisters) return;

    if (ImGui::Begin("CPU Registers", &state.showRegisters)) {
        CpuState cpu = emu.GetCpuStateSnapshot();

        // -------------------------------------------------------------
        // General Purpose Registers (2-column layout)
        // -------------------------------------------------------------
        ImGui::TextDisabled("General Purpose Registers");
        ImGui::Separator();

        ImGui::Columns(2, "gpr_columns", false);
        RenderRegisterRow("EAX", cpu.eax, emu, cpu);
        RenderRegisterRow("EBX", cpu.ebx, emu, cpu);
        RenderRegisterRow("ECX", cpu.ecx, emu, cpu);
        RenderRegisterRow("EDX", cpu.edx, emu, cpu);

        ImGui::NextColumn();
        RenderRegisterRow("ESI", cpu.esi, emu, cpu);
        RenderRegisterRow("EDI", cpu.edi, emu, cpu);
        RenderRegisterRow("ESP", cpu.esp, emu, cpu);
        RenderRegisterRow("EBP", cpu.ebp, emu, cpu);
        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::TextDisabled("Instruction Pointer & Flags");
        ImGui::Separator();

        RenderRegisterRow("EIP", cpu.eip, emu, cpu);
        ImGui::SameLine(135);
        RenderRegisterRow("EFLAGS", cpu.eflags, emu, cpu);

        // -------------------------------------------------------------
        // Interactive EFLAGS Badges
        // -------------------------------------------------------------
        ImGui::Spacing();
        ImGui::Text("Flags: ");
        ImGui::SameLine();
        RenderFlagBadge("CF", cpu.FlagCF(), 1 << 0, cpu, emu);
        RenderFlagBadge("PF", cpu.FlagPF(), 1 << 2, cpu, emu);
        RenderFlagBadge("AF", cpu.FlagAF(), 1 << 4, cpu, emu);
        RenderFlagBadge("ZF", cpu.FlagZF(), 1 << 6, cpu, emu);
        RenderFlagBadge("SF", cpu.FlagSF(), 1 << 7, cpu, emu);
        RenderFlagBadge("IF", cpu.FlagIF(), 1 << 9, cpu, emu);
        RenderFlagBadge("DF", cpu.FlagDF(), 1 << 10, cpu, emu);
        RenderFlagBadge("OF", cpu.FlagOF(), 1 << 11, cpu, emu);
        ImGui::NewLine();

        // -------------------------------------------------------------
        // Segment Registers
        // -------------------------------------------------------------
        ImGui::Spacing();
        ImGui::TextDisabled("Segment Registers");
        ImGui::Separator();
        ImGui::Text("CS: %04X   DS: %04X   ES: %04X", cpu.cs, cpu.ds, cpu.es);
        ImGui::Text("FS: %04X   GS: %04X   SS: %04X", cpu.fs, cpu.gs, cpu.ss);

        // -------------------------------------------------------------
        // Instruction Metrics & Status
        // -------------------------------------------------------------
        ImGui::Spacing();
        ImGui::TextDisabled("Metrics");
        ImGui::Separator();
        ImGui::Text("Instructions Executed: %llu", cpu.instructionCount);
    }
    ImGui::End();
}

} // namespace em836
