/**
 * @file gui_disassembly.cpp
 * @brief Capstone Real-Time Disassembly Table and AstraC Annotated Disassembler (.DSM).
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace em836 {

void GuiManager::RenderDisassemblyWindow(Emulator& emu, GuiState& state) {
    if (!state.showDisassembly) return;

    if (ImGui::Begin("Disassembly", &state.showDisassembly)) {
        if (ImGui::BeginTabBar("disasm_tab_bar")) {
            // =============================================================
            // Tab 1: Live RAM Disassembly (Capstone Engine)
            // =============================================================
            if (ImGui::BeginTabItem("Live RAM (Capstone)")) {
                CpuState cpu = emu.GetCpuStateSnapshot();

                // Toolbar Controls
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
                if (ImGui::InputText("##disasm_addr_input", state.disasmAddressInput, sizeof(state.disasmAddressInput),
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                    state.disasmBaseAddress = static_cast<uint32_t>(strtoul(state.disasmAddressInput, nullptr, 16));
                    state.disasmFollowEip = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(" Jump ")) {
                    state.disasmBaseAddress = static_cast<uint32_t>(strtoul(state.disasmAddressInput, nullptr, 16));
                    state.disasmFollowEip = false;
                }

                ImGui::Separator();

                // Instruction Range Decoding
                std::vector<DisassemblyItem> instructions = emu.DisassembleRange(state.disasmBaseAddress, 30);

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

                ImGui::EndTabItem();
            }

            // =============================================================
            // Tab 2: AstraC Annotated Disassembler (.DSM)
            // =============================================================
            if (ImGui::BeginTabItem("AstraC Annotated (.DSM)")) {
                CpuState cpu = emu.GetCpuStateSnapshot();

                // Toolbar
                if (ImGui::Button(" Disassemble Active Binary ")) {
                    std::string target = emu.GetConfig().lastLoadedFile;
                    if (target.empty() && !state.editorFilePath.empty()) {
                        target = state.editorFilePath;
                    }
                    if (!target.empty()) {
                        LoadDsmForFile(state, target, emu.GetConfig().astracPath);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(" Open .DSM / .BIN... ")) {
                    state.showFileOpenModal = true;
                }
                ImGui::SameLine();
                ImGui::RadioButton("32-bit", &state.dsmBits, 32);
                ImGui::SameLine();
                ImGui::RadioButton("16-bit", &state.dsmBits, 16);
                ImGui::SameLine();
                ImGui::Checkbox("Sync EIP", &state.dsmSyncWithEip);

                // Search / Filter Input
                ImGui::SetNextItemWidth(180);
                ImGui::InputTextWithHint("##dsm_filter", "Filter symbols/text...", state.dsmSearchFilter, sizeof(state.dsmSearchFilter));

                if (!state.dsmLoadedPath.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", std::filesystem::path(state.dsmLoadedPath).filename().string().c_str());
                }

                ImGui::Separator();

                // Body: Monospaced Listing
                ImGui::BeginChild("dsm_scroll_region", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

                if (state.dsmLines.empty()) {
                    ImGui::TextDisabled("No AstraC disassembly loaded yet.");
                    ImGui::TextWrapped("Click 'Disassemble Active Binary' above to run AstraC disassembler on the currently active program.");
                    if (!emu.GetConfig().lastLoadedFile.empty()) {
                        ImGui::Spacing();
                        if (ImGui::Button(("Disassemble: " + emu.GetConfig().lastLoadedFile).c_str())) {
                            LoadDsmForFile(state, emu.GetConfig().lastLoadedFile, emu.GetConfig().astracPath);
                        }
                    }
                } else {
                    // Compute hex address string of current instruction in .code to match EIP
                    char eipHexStr[16] = "";
                    uint32_t relEip = cpu.eip;
                    // If ACFH main module, code begins at code_offset (0x70) so relative code offset is EIP - 0x70
                    const auto& mainMod = emu.GetMainModule();
                    if (mainMod.has_value() && mainMod->header.code_offset > 0 && cpu.eip >= mainMod->header.code_offset) {
                        relEip = cpu.eip - mainMod->header.code_offset;
                    }
                    snprintf(eipHexStr, sizeof(eipHexStr), "%08X", relEip);

                    std::string filterStr = state.dsmSearchFilter;
                    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), [](unsigned char c) { return std::tolower(c); });

                    for (size_t i = 0; i < state.dsmLines.size(); ++i) {
                        const std::string& line = state.dsmLines[i];
                        if (line.empty()) {
                            ImGui::Spacing();
                            continue;
                        }

                        // Apply text search filter if active
                        if (!filterStr.empty()) {
                            std::string lineLower = line;
                            std::transform(lineLower.begin(), lineLower.end(), lineLower.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (lineLower.find(filterStr) == std::string::npos) {
                                continue;
                            }
                        }

                        // Check if this line corresponds to current EIP
                        bool isEipLine = false;
                        if (state.dsmSyncWithEip && line.length() >= 8) {
                            if (line.compare(0, 8, eipHexStr) == 0 && line.find("<") == std::string::npos) {
                                isEipLine = true;
                            }
                        }

                        if (isEipLine) {
                            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "=> ");
                            ImGui::SameLine();
                        }

                        // Syntax color coding based on line semantics
                        if (line[0] == ';') {
                            // Comment line
                            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "%s", line.c_str());
                        } else if (line.find(" <") != std::string::npos && line.back() == ':') {
                            // Function header e.g. "00000005 <MAIN>:"
                            ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.40f, 1.0f), "%s", line.c_str());
                        } else if (line.find("::") != std::string::npos) {
                            // Dynamic library import call e.g. "<math_lib.lib::LIB_ADD>"
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", line.c_str());
                        } else if (line.find("<") != std::string::npos && line.find(">") != std::string::npos) {
                            // Local function call e.g. "<PRINT_STR>"
                            ImGui::TextColored(ImVec4(0.60f, 0.90f, 1.0f, 1.0f), "%s", line.c_str());
                        } else {
                            // Standard instruction
                            ImGui::TextUnformatted(line.c_str());
                        }

                        if (isEipLine) {
                            ImGui::SetScrollHereY(0.5f);
                        }
                    }
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
