/**
 * @file gui_disassembly.cpp
 * @brief Capstone Real-Time Disassembly Table, AstraC Annotated Disassembler (.DSM),
 *        Section Viewers (.rodata, .data, .bss), and ACFH Header & Tables Inspector.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace em836 {

// -----------------------------------------------------------------------------
// Helper: Extract ASCII and Escape Sequences from .rodata
// -----------------------------------------------------------------------------

struct RoDataStringEntry {
    uint32_t offset = 0;
    uint32_t physAddr = 0;
    size_t length = 0;
    std::string text;
};

static std::vector<RoDataStringEntry> ExtractRoDataStrings(const uint8_t* data, size_t size, uint32_t basePhysAddr) {
    std::vector<RoDataStringEntry> results;
    if (!data || size == 0) return results;

    size_t start = 0;
    while (start < size) {
        // Skip leading nulls or non-printables
        while (start < size && (data[start] == 0 || (!std::isprint(data[start]) && data[start] != '\n' && data[start] != '\t'))) {
            ++start;
        }
        if (start >= size) break;

        size_t end = start;
        while (end < size && data[end] != 0 && (std::isprint(data[end]) || data[end] == '\t' || data[end] == '\n' || data[end] == '\r')) {
            ++end;
        }

        if (end > start) {
            std::string s;
            for (size_t k = start; k < end; ++k) {
                if (data[k] == '\n') s += "\\n";
                else if (data[k] == '\r') s += "\\r";
                else if (data[k] == '\t') s += "\\t";
                else s += static_cast<char>(data[k]);
            }
            results.push_back({
                static_cast<uint32_t>(start),
                basePhysAddr + static_cast<uint32_t>(start),
                end - start,
                s
            });
        }
        start = end + 1;
    }
    return results;
}

// -----------------------------------------------------------------------------
// Main Disassembly Window Renderer
// -----------------------------------------------------------------------------

void GuiManager::RenderDisassemblyWindow(Emulator& emu, GuiState& state) {
    if (!state.showDisassembly) return;

    if (ImGui::Begin("Disassembly & Sections", &state.showDisassembly)) {
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

                bool isRunning = (cpu.status == EmulatorStatus::Running);
                ImGui::SameLine();
                ImGui::BeginDisabled(isRunning);
                if (ImGui::Button(" Step Into (F11) ")) {
                    emu.StepInto();
                }
                ImGui::SameLine();
                if (ImGui::Button(" Step Over (F10) ")) {
                    emu.StepOver();
                }
                ImGui::SameLine();
                if (ImGui::Button(" Step Out (Shift+F11) ")) {
                    emu.StepOut();
                }
                ImGui::EndDisabled();

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
                bool isRunning = (cpu.status == EmulatorStatus::Running);

                // Auto-refresh modules if empty and program is loaded
                if (state.dsmModules.empty()) {
                    std::string target = emu.GetConfig().lastLoadedFile;
                    if (target.empty() && !state.editorFilePath.empty()) {
                        target = state.editorFilePath;
                    }
                    if (!target.empty()) {
                        RefreshDsmModules(emu, state);
                    }
                }

                // Row 1: Actions & Stepping Toolbar
                if (ImGui::Button(" Disassemble All Modules ")) {
                    RefreshDsmModules(emu, state);
                }
                ImGui::SameLine();
                if (ImGui::Button(" Open .DSM / .BIN... ")) {
                    state.showFileOpenModal = true;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(isRunning);
                if (ImGui::Button(" Step Into (F11) ")) {
                    emu.StepInto();
                }
                ImGui::SameLine();
                if (ImGui::Button(" Step Over (F10) ")) {
                    emu.StepOver();
                }
                ImGui::SameLine();
                if (ImGui::Button(" Step Out (Shift+F11) ")) {
                    emu.StepOut();
                }
                ImGui::EndDisabled();

                // Row 2: Module Selector, Filters & EIP Sync
                if (!state.dsmModules.empty()) {
                    std::vector<std::string> modLabels;
                    std::vector<const char*> modLabelPtrs;
                    for (size_t m = 0; m < state.dsmModules.size(); ++m) {
                        const auto& mod = state.dsmModules[m];
                        std::string label;
                        if (mod.isLibrary) {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "[Lib] %s (@ 0x%08X)", mod.moduleName.c_str(), mod.baseAddress);
                            label = buf;
                        } else {
                            label = "[Exe] " + mod.moduleName;
                        }
                        modLabels.push_back(label);
                    }
                    for (const auto& l : modLabels) {
                        modLabelPtrs.push_back(l.c_str());
                    }

                    ImGui::SetNextItemWidth(260);
                    int currentMod = state.selectedDsmModuleIndex;
                    if (ImGui::Combo("##dsm_module_combo", &currentMod, modLabelPtrs.data(), static_cast<int>(modLabelPtrs.size()))) {
                        if (currentMod >= 0 && currentMod < static_cast<int>(state.dsmModules.size())) {
                            state.selectedDsmModuleIndex = currentMod;
                            state.dsmLines = state.dsmModules[currentMod].lines;
                            state.dsmLoadedPath = state.dsmModules[currentMod].filePath;
                        }
                    }
                    ImGui::SameLine();
                }

                ImGui::RadioButton("32-bit", &state.dsmBits, 32);
                ImGui::SameLine();
                ImGui::RadioButton("16-bit", &state.dsmBits, 16);
                ImGui::SameLine();
                ImGui::Checkbox("Sync EIP", &state.dsmSyncWithEip);
                ImGui::SameLine();

                // Search / Filter Input
                ImGui::SetNextItemWidth(160);
                ImGui::InputTextWithHint("##dsm_filter", "Filter symbols...", state.dsmSearchFilter, sizeof(state.dsmSearchFilter));

                if (!state.dsmLoadedPath.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", std::filesystem::path(state.dsmLoadedPath).filename().string().c_str());
                }

                // Dynamic EIP Synchronization & Module Detection
                if (state.dsmSyncWithEip && !state.dsmModules.empty()) {
                    int detectedModIdx = -1;
                    for (size_t m = 0; m < state.dsmModules.size(); ++m) {
                        const auto& mod = state.dsmModules[m];
                        if (mod.isLibrary) {
                            uint32_t codeStart = mod.baseAddress + mod.codeOffset;
                            uint32_t codeEnd = codeStart + mod.codeSize;
                            if (cpu.eip >= codeStart && cpu.eip < codeEnd) {
                                detectedModIdx = static_cast<int>(m);
                                break;
                            }
                        }
                    }

                    if (detectedModIdx == -1) {
                        for (size_t m = 0; m < state.dsmModules.size(); ++m) {
                            const auto& mod = state.dsmModules[m];
                            if (!mod.isLibrary) {
                                uint32_t codeStart = mod.codeOffset;
                                uint32_t codeEnd = codeStart + mod.codeSize;
                                if (cpu.eip >= codeStart && cpu.eip < codeEnd) {
                                detectedModIdx = static_cast<int>(m);
                                break;
                            }
                        }
                    }
                }

                if (detectedModIdx != -1 && detectedModIdx != state.selectedDsmModuleIndex &&
                    detectedModIdx < static_cast<int>(state.dsmModules.size())) {
                    state.selectedDsmModuleIndex = detectedModIdx;
                    state.dsmLines = state.dsmModules[detectedModIdx].lines;
                    state.dsmLoadedPath = state.dsmModules[detectedModIdx].filePath;
                }
            }

            // If debugging inside library, show prominent banner
            bool isInsideLibrary = false;
            std::string currentLibName;
            uint32_t currentLibBase = 0;
            if (!state.dsmModules.empty() && state.selectedDsmModuleIndex < static_cast<int>(state.dsmModules.size())) {
                const auto& curMod = state.dsmModules[state.selectedDsmModuleIndex];
                if (curMod.isLibrary) {
                    isInsideLibrary = true;
                    currentLibName = curMod.moduleName;
                    currentLibBase = curMod.baseAddress;
                }
            }

            if (isInsideLibrary) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.32f, 0.22f, 0.90f));
                if (ImGui::BeginChild("lib_debug_banner", ImVec2(0, 30), true)) {
                    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.50f, 1.0f), "DEBUGGING DYNAMIC LIBRARY: %s", currentLibName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Base: 0x%08X)", currentLibBase);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(isRunning);
                    if (ImGui::Button(" Step Out to Return to Caller (Shift+F11) ")) {
                        emu.StepOut();
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            ImGui::Separator();

            // Listing
            ImGui::BeginChild("dsm_scroll_region", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

            if (state.dsmLines.empty()) {
                ImGui::TextDisabled("No AstraC disassembly loaded yet.");
                ImGui::TextWrapped("Click 'Disassemble All Modules' above to run AstraC disassembler on the active executable and libraries.");
                if (!emu.GetConfig().lastLoadedFile.empty()) {
                    ImGui::Spacing();
                    if (ImGui::Button(("Disassemble: " + emu.GetConfig().lastLoadedFile).c_str())) {
                        RefreshDsmModules(emu, state);
                    }
                }
            } else {
                char eipHexStr[16] = "";
                uint32_t relEip = cpu.eip;

                if (!state.dsmModules.empty() && state.selectedDsmModuleIndex < static_cast<int>(state.dsmModules.size())) {
                    const auto& curMod = state.dsmModules[state.selectedDsmModuleIndex];
                    if (curMod.isLibrary) {
                        if (cpu.eip >= curMod.baseAddress + curMod.codeOffset) {
                            relEip = cpu.eip - (curMod.baseAddress + curMod.codeOffset);
                        }
                    } else {
                        if (cpu.eip >= curMod.codeOffset) {
                            relEip = cpu.eip - curMod.codeOffset;
                        }
                    }
                } else {
                    const auto& mainMod = emu.GetMainModule();
                    if (mainMod.has_value() && mainMod->header.code_offset > 0 && cpu.eip >= mainMod->header.code_offset) {
                        relEip = cpu.eip - mainMod->header.code_offset;
                    }
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

                    if (!filterStr.empty()) {
                        std::string lineLower = line;
                        std::transform(lineLower.begin(), lineLower.end(), lineLower.begin(), [](unsigned char c) { return std::tolower(c); });
                        if (lineLower.find(filterStr) == std::string::npos) {
                            continue;
                        }
                    }

                    // Check if line contains a hex instruction offset
                    uint32_t relOffset = 0;
                    bool hasValidOffset = false;
                    if (line.length() >= 8) {
                        bool isHex = true;
                        for (int k = 0; k < 8; ++k) {
                            if (!std::isxdigit(static_cast<unsigned char>(line[k]))) {
                                isHex = false;
                                break;
                            }
                        }
                        if (isHex && (line.length() == 8 || line[8] == ' ' || line[8] == '<' || line[8] == ':')) {
                            relOffset = static_cast<uint32_t>(strtoul(line.substr(0, 8).c_str(), nullptr, 16));
                            hasValidOffset = true;
                        }
                    }

                    // 1. Breakpoint Gutter Indicator & Toggle
                    if (hasValidOffset) {
                        uint32_t physAddr = 0;
                        if (!state.dsmModules.empty() && state.selectedDsmModuleIndex < static_cast<int>(state.dsmModules.size())) {
                            const auto& curMod = state.dsmModules[state.selectedDsmModuleIndex];
                            physAddr = curMod.baseAddress + curMod.codeOffset + relOffset;
                        } else {
                            const auto& mainMod = emu.GetMainModule();
                            uint32_t codeOff = mainMod.has_value() ? mainMod->header.code_offset : 0;
                            physAddr = codeOff + relOffset;
                        }

                        bool hasBp = emu.HasBreakpoint(physAddr);
                        ImGui::PushID(static_cast<int>(i));
                        if (hasBp) {
                            ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "(●)");
                        } else {
                            ImGui::TextDisabled("( )");
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Click to toggle breakpoint @ 0x%08X", physAddr);
                        }
                        if (ImGui::IsItemClicked()) {
                            emu.ToggleBreakpoint(physAddr);
                        }
                        ImGui::PopID();
                        ImGui::SameLine();
                    } else {
                        ImGui::TextDisabled("   ");
                        ImGui::SameLine();
                    }

                    // 2. Current EIP Pointer
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

                    // 3. Parse symbol tags <...> and library calls
                    std::string callLibName;
                    std::string callFuncName;
                    bool isLibCall = false;
                    size_t openBracket = line.find('<');
                    size_t closeBracket = (openBracket != std::string::npos) ? line.find('>', openBracket) : std::string::npos;
                    if (openBracket != std::string::npos && closeBracket != std::string::npos && closeBracket > openBracket) {
                        std::string sym = line.substr(openBracket + 1, closeBracket - openBracket - 1);
                        size_t sep = sym.find("::");
                        if (sep != std::string::npos) {
                            isLibCall = true;
                            callLibName = sym.substr(0, sep);
                            callFuncName = sym.substr(sep + 2);
                        }
                    }

                    // 4. Line Rendering with Syntax Coloring
                    if (line[0] == ';') {
                        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "%s", line.c_str());
                    } else if (line.find(" <") != std::string::npos && line.back() == ':') {
                        ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.40f, 1.0f), "%s", line.c_str());
                    } else if (isLibCall) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", line.c_str());
                        ImGui::SameLine();
                        std::string viewBtn = " View in " + callLibName + " ##view_" + std::to_string(i);
                        if (ImGui::SmallButton(viewBtn.c_str())) {
                            JumpToDsmSymbol(state, callLibName, callFuncName);
                        }
                        if (isEipLine) {
                            ImGui::SameLine();
                            std::string stepInBtn = " Step Into (F11) ##stepin_" + std::to_string(i);
                            ImGui::BeginDisabled(isRunning);
                            if (ImGui::SmallButton(stepInBtn.c_str())) {
                                emu.StepInto();
                            }
                            ImGui::EndDisabled();
                        }
                    } else if (line.find("<") != std::string::npos && line.find(">") != std::string::npos) {
                        ImGui::TextColored(ImVec4(0.60f, 0.90f, 1.0f, 1.0f), "%s", line.c_str());
                    } else {
                        ImGui::TextUnformatted(line.c_str());
                    }

                    if (state.dsmSelectedLine == static_cast<int>(i)) {
                        ImGui::SetScrollHereY(0.3f);
                        state.dsmSelectedLine = -1;
                    } else if (isEipLine) {
                        ImGui::SetScrollHereY(0.5f);
                    }
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // =============================================================
        // Tab 3: .rodata (Strings & Constants) Section Inspector
        // =============================================================
        if (ImGui::BeginTabItem(".rodata (Strings)")) {
            const auto& mainMod = emu.GetMainModule();
            const auto& libs = emu.GetLoadedLibraries();

            std::vector<std::string> modNames;
            if (mainMod.has_value()) {
                modNames.push_back("[Exe] " + mainMod->fileName);
            }
            for (const auto& lib : libs) {
                modNames.push_back("[Lib] " + lib.fileName);
            }

            static int selectedMod = 0;
            if (selectedMod >= static_cast<int>(modNames.size())) {
                selectedMod = 0;
            }

            if (modNames.empty()) {
                ImGui::TextDisabled("No ACFH binary module or dynamic library loaded.");
            } else {
                std::vector<const char*> ptrs;
                for (const auto& n : modNames) ptrs.push_back(n.c_str());
                ImGui::SetNextItemWidth(260);
                ImGui::Combo("Module##rodata_mod", &selectedMod, ptrs.data(), static_cast<int>(ptrs.size()));

                const AcfhParsedModule* activeMod = nullptr;
                if (mainMod.has_value() && selectedMod == 0) {
                    activeMod = &(*mainMod);
                } else {
                    size_t libIdx = mainMod.has_value() ? (selectedMod - 1) : selectedMod;
                    if (libIdx < libs.size()) {
                        activeMod = &libs[libIdx];
                    }
                }

                if (activeMod) {
                    uint32_t roOffset = activeMod->header.rodata_offset;
                    uint32_t roSize = activeMod->header.rodata_size;
                    uint32_t physBase = activeMod->baseAddress + roOffset;

                    ImGui::Text("Offset: 0x%08X  |  Physical RAM: 0x%08X  |  Size: %u bytes",
                        roOffset, physBase, roSize);
                    ImGui::Separator();

                    if (roSize == 0) {
                        ImGui::TextDisabled("This module does not define a .rodata section (rodata_size = 0).");
                    } else {
                        std::vector<uint8_t> roBytes;
                        if (roOffset < activeMod->rawBuffer.size()) {
                            size_t avail = std::min(static_cast<size_t>(roSize), activeMod->rawBuffer.size() - roOffset);
                            roBytes.assign(activeMod->rawBuffer.begin() + roOffset, activeMod->rawBuffer.begin() + roOffset + avail);
                        } else {
                            roBytes.resize(roSize);
                            emu.ReadMemory(physBase, roBytes.data(), roSize);
                        }

                        auto stringsList = ExtractRoDataStrings(roBytes.data(), roBytes.size(), physBase);
                        ImGui::Text("Strings Detected: %zu", stringsList.size());

                        if (ImGui::BeginTable("RoDataTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                            ImGui::TableSetupColumn("Len", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                            ImGui::TableSetupColumn("Decoded String Content", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                            ImGui::TableHeadersRow();

                            for (size_t s = 0; s < stringsList.size(); ++s) {
                                const auto& item = stringsList[s];
                                ImGui::TableNextRow();

                                ImGui::TableNextColumn();
                                ImGui::Text("+0x%04X", item.offset);

                                ImGui::TableNextColumn();
                                ImGui::Text("0x%08X", item.physAddr);

                                ImGui::TableNextColumn();
                                ImGui::Text("%zu", item.length);

                                ImGui::TableNextColumn();
                                ImGui::TextColored(ImVec4(0.81f, 0.57f, 0.47f, 1.0f), "\"%s\"", item.text.c_str());

                                ImGui::TableNextColumn();
                                ImGui::PushID(static_cast<int>(s));
                                if (ImGui::SmallButton("Hex")) {
                                    state.memoryCurrentAddress = item.physAddr;
                                    snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", item.physAddr);
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Watch")) {
                                    emu.AddWatchpoint(item.physAddr, 4, WatchCondition::OnChange, 0, item.text.substr(0, 16));
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                    }
                }
            }
            ImGui::EndTabItem();
        }

        // =============================================================
        // Tab 4: .data & .bss Sections Inspector
        // =============================================================
        if (ImGui::BeginTabItem(".data & .bss")) {
            const auto& mainMod = emu.GetMainModule();
            const auto& libs = emu.GetLoadedLibraries();

            std::vector<std::string> modNames;
            if (mainMod.has_value()) {
                modNames.push_back("[Exe] " + mainMod->fileName);
            }
            for (const auto& lib : libs) {
                modNames.push_back("[Lib] " + lib.fileName);
            }

            static int selectedDataMod = 0;
            if (selectedDataMod >= static_cast<int>(modNames.size())) {
                selectedDataMod = 0;
            }

            if (modNames.empty()) {
                ImGui::TextDisabled("No ACFH binary module or dynamic library loaded.");
            } else {
                std::vector<const char*> ptrs;
                for (const auto& n : modNames) ptrs.push_back(n.c_str());
                ImGui::SetNextItemWidth(260);
                ImGui::Combo("Module##databss_mod", &selectedDataMod, ptrs.data(), static_cast<int>(ptrs.size()));

                const AcfhParsedModule* activeMod = nullptr;
                if (mainMod.has_value() && selectedDataMod == 0) {
                    activeMod = &(*mainMod);
                } else {
                    size_t libIdx = mainMod.has_value() ? (selectedDataMod - 1) : selectedDataMod;
                    if (libIdx < libs.size()) {
                        activeMod = &libs[libIdx];
                    }
                }

                if (activeMod) {
                    // .data Section
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ .data Section (Initialized Globals) ]");
                    uint32_t dataOff = activeMod->header.data_offset;
                    uint32_t dataSz = activeMod->header.data_size;
                    uint32_t dataPhys = activeMod->baseAddress + dataOff;
                    ImGui::Text("File Offset: 0x%08X  |  RAM Address: 0x%08X - 0x%08X  |  Size: %u bytes",
                        dataOff, dataPhys, dataPhys + dataSz, dataSz);

                    if (dataSz > 0) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Jump Hex View##data")) {
                            state.memoryCurrentAddress = dataPhys;
                            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", dataPhys);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Watch Base##data")) {
                            emu.AddWatchpoint(dataPhys, 4, WatchCondition::OnChange, 0, ".data_base");
                        }
                    } else {
                        ImGui::TextDisabled("No initialized data defined in this module.");
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // .bss Section
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "[ .bss Section (Zero-Initialized Dynamic Storage) ]");
                    uint32_t bssOff = activeMod->header.bss_offset;
                    uint32_t bssSz = activeMod->header.bss_size;
                    uint32_t bssPhys = activeMod->baseAddress + bssOff;
                    ImGui::Text("RAM Address: 0x%08X - 0x%08X  |  Size: %u bytes (%u KB)",
                        bssPhys, bssPhys + bssSz, bssSz, (bssSz + 1023) / 1024);

                    if (bssSz > 0) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Jump Hex View##bss")) {
                            state.memoryCurrentAddress = bssPhys;
                            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", bssPhys);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Watch Base##bss")) {
                            emu.AddWatchpoint(bssPhys, 4, WatchCondition::OnChange, 0, ".bss_base");
                        }

                        // Preview first 64 bytes of live BSS in RAM
                        uint32_t previewCount = std::min(bssSz, 64u);
                        std::vector<uint8_t> bssLive(previewCount, 0);
                        emu.ReadMemory(bssPhys, bssLive.data(), previewCount);
                        std::ostringstream oss;
                        for (size_t b = 0; b < bssLive.size(); ++b) {
                            oss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<int>(bssLive[b]) << " ";
                            if ((b + 1) % 16 == 0) oss << "\n";
                        }
                        ImGui::TextDisabled("Live RAM Preview (first %u bytes):", previewCount);
                        ImGui::TextUnformatted(oss.str().c_str());
                    } else {
                        ImGui::TextDisabled("No uninitialized BSS storage declared.");
                    }
                }
            }
            ImGui::EndTabItem();
        }

        // =============================================================
        // Tab 5: ACFH Header & Tables (ot, it, ft)
        // =============================================================
        if (ImGui::BeginTabItem("Header & Tables (ot, it, ft)")) {
            const auto& mainMod = emu.GetMainModule();
            const auto& libs = emu.GetLoadedLibraries();

            std::vector<std::string> modNames;
            if (mainMod.has_value()) {
                modNames.push_back("[Exe] " + mainMod->fileName);
            }
            for (const auto& lib : libs) {
                modNames.push_back("[Lib] " + lib.fileName);
            }

            static int selectedTblMod = 0;
            if (selectedTblMod >= static_cast<int>(modNames.size())) {
                selectedTblMod = 0;
            }

            if (modNames.empty()) {
                ImGui::TextDisabled("No ACFH binary module or dynamic library loaded.");
            } else {
                std::vector<const char*> ptrs;
                for (const auto& n : modNames) ptrs.push_back(n.c_str());
                ImGui::SetNextItemWidth(260);
                ImGui::Combo("Module##tbl_mod", &selectedTblMod, ptrs.data(), static_cast<int>(ptrs.size()));

                const AcfhParsedModule* activeMod = nullptr;
                if (mainMod.has_value() && selectedTblMod == 0) {
                    activeMod = &(*mainMod);
                } else {
                    size_t libIdx = mainMod.has_value() ? (selectedTblMod - 1) : selectedTblMod;
                    if (libIdx < libs.size()) {
                        activeMod = &libs[libIdx];
                    }
                }

                if (activeMod) {
                    const auto& h = activeMod->header;
                    char magicBuf[5] = { h.magic[0], h.magic[1], h.magic[2], h.magic[3], '\0' };

                    // 112-Byte Header Breakdown
                    if (ImGui::CollapsingHeader("112-Byte AcFileHeader (ACFH)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Text("Magic: %s", magicBuf);
                        ImGui::SameLine(180);
                        ImGui::Text("Format Version: %u.%u", h.version >> 16, h.version & 0xFFFF);
                        ImGui::SameLine(360);
                        ImGui::Text("Entry Point: 0x%08X", (h.entry_point_offset != 0xFFFFFFFF) ? h.entry_point_offset : 0);

                        ImGui::Text("Flags: 0x%08X [ %s%s%s%s%s]",
                            h.flags,
                            (h.flags & AC_FLAG_EXECUTABLE) ? "EXECUTABLE " : "",
                            (h.flags & AC_FLAG_DYNAMIC) ? "DYNAMIC_LIB " : "",
                            (h.flags & AC_FLAG_HAS_RELOCS) ? "RELOCS " : "",
                            (h.flags & AC_FLAG_HAS_FUNCS) ? "EXPORT_FUNCS " : "",
                            (h.flags & AC_FLAG_HAS_IMPORTS) ? "IMPORTS " : "");

                        if (ImGui::BeginTable("HdrSecTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Section");
                            ImGui::TableSetupColumn("File Offset");
                            ImGui::TableSetupColumn("Size (Bytes)");
                            ImGui::TableSetupColumn("Runtime Base");
                            ImGui::TableHeadersRow();

                            auto addSecRow = [&](const char* name, uint32_t off, uint32_t sz) {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn(); ImGui::Text("%s", name);
                                ImGui::TableNextColumn(); ImGui::Text("0x%08X", off);
                                ImGui::TableNextColumn(); ImGui::Text("%u", sz);
                                ImGui::TableNextColumn(); ImGui::Text("0x%08X", activeMod->baseAddress + off);
                            };

                            addSecRow(".code", h.code_offset, h.code_size);
                            addSecRow(".data", h.data_offset, h.data_size);
                            addSecRow(".rodata", h.rodata_offset, h.rodata_size);
                            addSecRow(".bss", h.bss_offset, h.bss_size);
                            addSecRow("Relocation Table (ot)", h.reloc_offset, h.reloc_size);
                            addSecRow("Function Export Table (ft)", h.func_table_offset, h.func_table_size);
                            addSecRow("Import Table (it)", h.import_table_offset, h.import_table_size);

                            ImGui::EndTable();
                        }
                    }

                    // Relocation Table (ot)
                    if (ImGui::CollapsingHeader(("Relocation Table (ot) — " + std::to_string(activeMod->relocations.size()) + " entries").c_str())) {
                        if (activeMod->relocations.empty()) {
                            ImGui::TextDisabled("No relocations recorded in this module.");
                        } else {
                            if (ImGui::BeginTable("RelocTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                                ImGui::TableSetupColumn("Code Offset");
                                ImGui::TableSetupColumn("Target Physical Patch Address");
                                ImGui::TableHeadersRow();

                                for (size_t r = 0; r < activeMod->relocations.size(); ++r) {
                                    uint32_t off = activeMod->relocations[r];
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn(); ImGui::Text("%zu", r + 1);
                                    ImGui::TableNextColumn(); ImGui::Text("+0x%04X", off);
                                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", activeMod->baseAddress + h.code_offset + off);
                                }
                                ImGui::EndTable();
                            }
                        }
                    }

                    // Function Export Table (ft)
                    if (ImGui::CollapsingHeader(("Function Export Table (ft) — " + std::to_string(activeMod->exports.size()) + " functions").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (activeMod->exports.empty()) {
                            ImGui::TextDisabled("No exported functions in this module.");
                        } else {
                            if (ImGui::BeginTable("FtTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                                ImGui::TableSetupColumn("Function Name");
                                ImGui::TableSetupColumn("Code Offset");
                                ImGui::TableSetupColumn("Runtime Address");
                                ImGui::TableSetupColumn("Params (Bytes)");
                                ImGui::TableSetupColumn("Calling Convention");
                                ImGui::TableHeadersRow();

                                for (const auto& exp : activeMod->exports) {
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn(); ImGui::Text("%s", exp.name.c_str());
                                    ImGui::TableNextColumn(); ImGui::Text("+0x%04X", exp.codeOffset);
                                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", exp.runtimeAddress);
                                    ImGui::TableNextColumn(); ImGui::Text("%u", exp.paramSize);
                                    ImGui::TableNextColumn();
                                    std::string abi = (exp.flags & AC_FUNC_FLAG_STDCALL) ? "stdcall" : "cdecl";
                                    if (exp.flags & AC_FUNC_FLAG_VARIADIC) abi += " (variadic)";
                                    ImGui::Text("%s", abi.c_str());
                                }
                                ImGui::EndTable();
                            }
                        }
                    }

                    // Import Table (it)
                    if (ImGui::CollapsingHeader(("Import Table (it) — " + std::to_string(activeMod->imports.size()) + " imports").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (activeMod->imports.empty()) {
                            ImGui::TextDisabled("No imports defined in this module.");
                        } else {
                            if (ImGui::BeginTable("ItTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                                ImGui::TableSetupColumn("Library Name");
                                ImGui::TableSetupColumn("Function Name");
                                ImGui::TableSetupColumn("Code Patch Offset");
                                ImGui::TableSetupColumn("Runtime Patch Address");
                                ImGui::TableSetupColumn("Status");
                                ImGui::TableHeadersRow();

                                for (const auto& imp : activeMod->imports) {
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn(); ImGui::Text("%s", imp.libName.c_str());
                                    ImGui::TableNextColumn(); ImGui::Text("%s", imp.funcName.c_str());
                                    ImGui::TableNextColumn(); ImGui::Text("+0x%04X", imp.codePatchOffset);
                                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", imp.runtimePatchAddress);
                                    ImGui::TableNextColumn();
                                    if (imp.resolved) {
                                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "RESOLVED (0x%08X)", imp.resolvedAddress);
                                    } else {
                                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "UNRESOLVED");
                                    }
                                }
                                ImGui::EndTable();
                            }
                        }
                    }
                }
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace em836
