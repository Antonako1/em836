/**
 * @file gui_disassembly.cpp
 * @brief Capstone Real-Time Disassembly Table with Breakpoint Controls and EIP Marker.
 */

#include "em836/gui.hpp"
#include <imgui.h>
#include <sstream>
#include <iomanip>

namespace em836 {

void GuiManager::RenderDisassemblyWindow(Emulator& emu, GuiState& state) {
    if (!state.showDisassembly) return;

    if (ImGui::Begin("Disassembly (Capstone)", &state.showDisassembly)) {
        CpuState cpu = emu.GetCpuStateSnapshot();

        // Disassemble instructions around current EIP
        uint32_t startAddr = cpu.eip;
        std::vector<DisassemblyItem> instructions = emu.DisassembleRange(startAddr, 25);

        if (instructions.empty()) {
            ImGui::TextDisabled("No valid instructions decoded at EIP (0x%08X).", cpu.eip);
        } else {
            static ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV |
                                                ImGuiTableFlags_BordersH |
                                                ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_ScrollY;

            if (ImGui::BeginTable("disasm_table", 5, tableFlags, ImVec2(0, 0))) {
                ImGui::TableSetupColumn("BP", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Ptr", ImGuiTableColumnFlags_WidthFixed, 25.0f);
                ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& item : instructions) {
                    ImGui::TableNextRow();

                    if (item.isCurrentEip) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.25f, 0.35f, 0.5f, 0.65f)));
                    }

                    // 1. Breakpoint Checkbox
                    ImGui::TableSetColumnIndex(0);
                    bool bp = item.hasBreakpoint;
                    std::string bpId = "##bp_" + std::to_string(item.address);
                    if (ImGui::Checkbox(bpId.c_str(), &bp)) {
                        emu.ToggleBreakpoint(item.address);
                    }

                    // 2. EIP Pointer Arrow
                    ImGui::TableSetColumnIndex(1);
                    if (item.isCurrentEip) {
                        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "=>");
                    }

                    // 3. Address
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%08X", item.address);

                    // 4. Hex Opcode Bytes
                    ImGui::TableSetColumnIndex(3);
                    std::ostringstream bytesOss;
                    for (size_t b = 0; b < item.bytes.size() && b < 4; ++b) {
                        bytesOss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<int>(item.bytes[b]) << " ";
                    }
                    if (item.bytes.size() > 4) bytesOss << "..";
                    ImGui::TextDisabled("%s", bytesOss.str().c_str());

                    // 5. Mnemonic and Operands
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", item.mnemonic.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", item.opStr.c_str());
                }

                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

} // namespace em836
