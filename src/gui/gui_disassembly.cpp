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

        // -------------------------------------------------------------
        // Disassembly Toolbar Controls
        // -------------------------------------------------------------
        ImGui::Checkbox("Follow EIP", &state.disasmFollowEip);
        ImGui::SameLine();
        if (ImGui::Button(" Goto EIP ")) {
            state.disasmBaseAddress = (cpu.eip >= 16) ? (cpu.eip - 16) : cpu.eip;
            snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button(" Origin (0x0) ")) {
            state.disasmBaseAddress = emu.GetCodeBaseAddress();
            snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(75);
        if (ImGui::InputText("##disasm_addr_input", state.disasmAddressInput, sizeof(state.disasmAddressInput), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            state.disasmBaseAddress = static_cast<uint32_t>(strtoul(state.disasmAddressInput, nullptr, 16));
            state.disasmFollowEip = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(" Jump ")) {
            state.disasmBaseAddress = static_cast<uint32_t>(strtoul(state.disasmAddressInput, nullptr, 16));
            state.disasmFollowEip = false;
        }

        ImGui::Separator();

        // -------------------------------------------------------------
        // Instruction Range Decoding
        // -------------------------------------------------------------
        std::vector<DisassemblyItem> instructions = emu.DisassembleRange(state.disasmBaseAddress, 30);

        // Follow EIP auto-scroll adjustment if EIP jumped or stepped outside current decoded frame
        if (state.disasmFollowEip) {
            bool eipInView = false;
            for (const auto& item : instructions) {
                if (item.address == cpu.eip) {
                    eipInView = true;
                    break;
                }
            }
            if (!eipInView) {
                state.disasmBaseAddress = (cpu.eip >= 16) ? (cpu.eip - 16) : cpu.eip;
                snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
                instructions = emu.DisassembleRange(state.disasmBaseAddress, 30);
            }
        }

        if (instructions.empty()) {
            ImGui::TextDisabled("No valid instructions decoded at 0x%08X.", state.disasmBaseAddress);
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
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.20f, 0.40f, 0.65f, 0.75f)));
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
                    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "%s", item.mnemonic.c_str());
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
