/**
 * @file gui_manager.cpp
 * @brief Master GUI Orchestrator, Main Menu Bar, and DockSpace Setup.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>

#include <filesystem>
#include <fstream>
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
            if (ImGui::MenuItem("Reset Default Layout", "Ctrl+Shift+L")) {
                state.requestLayoutReset = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("CPU Registers", nullptr, &state.showRegisters);
            ImGui::MenuItem("Execution Controls", nullptr, &state.showExecution);
            ImGui::MenuItem("Disassembly", nullptr, &state.showDisassembly);
            ImGui::MenuItem("VGA Display", nullptr, &state.showVga);
            ImGui::MenuItem("Console & Output", nullptr, &state.showConsole);
            ImGui::MenuItem("Code Editor", nullptr, &state.showEditor);
            ImGui::MenuItem("Memory & ACFH Modules", nullptr, &state.showMemory);
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

bool GuiManager::LoadDsmForFile(GuiState& state, const std::string& filePath, const std::string& customAstraCPath) {
    if (filePath.empty()) return false;

    std::filesystem::path p(filePath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".dsm") {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;
        state.dsmLines.clear();
        state.dsmContent.clear();
        std::string line;
        while (std::getline(file, line)) {
            state.dsmLines.push_back(line);
            state.dsmContent += line + "\n";
        }
        state.dsmLoadedPath = filePath;
        return true;
    }

    DisassemblyResult res = AstraCToolchain::DisassembleFile(
        filePath,
        customAstraCPath,
        state.dsmBits,
        state.dsmOrg,
        true
    );

    if (res.success) {
        state.dsmLoadedPath = res.dsmPath;
        state.dsmContent = res.dsmContent;
        state.dsmLines = res.lines;
        return true;
    }
    return false;
}

void GuiManager::RenderAll(Emulator& emu, GuiState& state, SDL_Renderer* renderer) {
    // Top Main Menu Bar
    RenderMainMenuBar(emu, state, renderer);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float workX = vp->WorkPos.x;
    float workY = vp->WorkPos.y;
    float workW = vp->WorkSize.x;
    float workH = vp->WorkSize.y;

    ImGuiCond cond = state.requestLayoutReset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;

    float pad = 6.0f;
    float availableW = std::max(workW - pad * 4.0f, 960.0f);
    float availableH = std::max(workH - pad * 2.0f, 600.0f);

    // Responsive 3-Column Width Calculations:
    // Left column: ~24% of width (min 340px, max 460px)
    float leftColW = std::clamp(availableW * 0.24f, 340.0f, 460.0f);
    // Right column: ~33% of width (min 380px, max 620px)
    float rightColW = std::clamp(availableW * 0.33f, 380.0f, 620.0f);
    // Center column: gets all remaining screen space!
    float centerColW = availableW - leftColW - rightColW;
    if (centerColW < 360.0f) {
        centerColW = 360.0f;
    }

    float col1X = workX + pad;
    float col2X = col1X + leftColW + pad;
    float col3X = col2X + centerColW + pad;

    // =========================================================================
    // Column 1 (Left): CPU Registers, Execution Controls, Disassembly
    // =========================================================================
    float regH = 265.0f;
    float execH = 115.0f;
    float disasmY = workY + pad + regH + pad + execH + pad;
    float disasmH = (workY + workH) - disasmY - pad;
    if (disasmH < 150.0f) disasmH = 150.0f;

    ImGui::SetNextWindowPos(ImVec2(col1X, workY + pad), cond);
    ImGui::SetNextWindowSize(ImVec2(leftColW, regH), cond);
    RenderRegistersWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(col1X, workY + pad + regH + pad), cond);
    ImGui::SetNextWindowSize(ImVec2(leftColW, execH), cond);
    RenderExecutionWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(col1X, disasmY), cond);
    ImGui::SetNextWindowSize(ImVec2(leftColW, disasmH), cond);
    RenderDisassemblyWindow(emu, state);

    // =========================================================================
    // Column 2 (Center): VGA Display & Console / Virtual Terminal
    // =========================================================================
    float vgaH = std::clamp(centerColW * 0.60f, 280.0f, availableH * 0.52f);
    float consoleY = workY + pad + vgaH + pad;
    float consoleH = (workY + workH) - consoleY - pad;
    if (consoleH < 180.0f) consoleH = 180.0f;

    ImGui::SetNextWindowPos(ImVec2(col2X, workY + pad), cond);
    ImGui::SetNextWindowSize(ImVec2(centerColW, vgaH), cond);
    RenderVgaWindow(emu, state, renderer);

    ImGui::SetNextWindowPos(ImVec2(col2X, consoleY), cond);
    ImGui::SetNextWindowSize(ImVec2(centerColW, consoleH), cond);
    RenderConsoleWindow(emu, state);

    // =========================================================================
    // Column 3 (Right): Code Editor & Tabbed Memory / ACFH Inspector
    // =========================================================================
    float editorH = std::max(200.0f, (availableH - pad) * 0.54f);
    float botRightY = workY + pad + editorH + pad;
    float botRightH = (workY + workH) - botRightY - pad;
    if (botRightH < 180.0f) botRightH = 180.0f;

    ImGui::SetNextWindowPos(ImVec2(col3X, workY + pad), cond);
    ImGui::SetNextWindowSize(ImVec2(rightColW, editorH), cond);
    RenderEditorWindow(emu, state);

    ImGui::SetNextWindowPos(ImVec2(col3X, botRightY), cond);
    ImGui::SetNextWindowSize(ImVec2(rightColW, botRightH), cond);
    RenderMemoryAndAcfhWindow(emu, state);

    // Reset layout flag after applying to all windows
    if (state.requestLayoutReset) {
        state.requestLayoutReset = false;
    }

    // Render Modals
    RenderSettingsModal(emu, state);
    RenderLoadOptionsModal(emu, state);
    RenderFileOpenModal(emu, state);
    RenderAboutModal(state);
}

void GuiManager::RenderMemoryAndAcfhWindow(Emulator& emu, GuiState& state) {
    if (!state.showMemory && !state.showAcfhInspector) return;

    if (ImGui::Begin("Memory & ACFH Modules", &state.showMemory)) {
        if (ImGui::BeginTabBar("memory_acfh_tab_bar")) {
            if (ImGui::BeginTabItem("Memory Hex Viewer")) {
                RenderMemoryContent(emu, state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ACFH Modules & Libraries")) {
                RenderAcfhInspectorContent(emu, state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void GuiManager::RenderAcfhInspectorWindow(Emulator& emu, GuiState& state) {
    if (!state.showAcfhInspector) return;

    if (ImGui::Begin("ACFH Modules & Libraries", &state.showAcfhInspector)) {
        RenderAcfhInspectorContent(emu, state);
    }
    ImGui::End();
}

void GuiManager::RenderAcfhInspectorContent(Emulator& emu, GuiState& state) {
        // Quick Library Loader Bar
        ImGui::Text("Load Dynamic Library:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##load_lib_input", "Path to .lib file...", state.loadLibPathInput, sizeof(state.loadLibPathInput));
        ImGui::SameLine();
        if (ImGui::Button(" Load .lib ")) {
            if (state.loadLibPathInput[0] != '\0') {
                emu.LoadAcfhLibraryFile(state.loadLibPathInput);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(" Examples .lib... ")) {
            ImGui::OpenPopup("examples_lib_popup");
        }
        if (ImGui::BeginPopup("examples_lib_popup")) {
            std::filesystem::path exDir = FindExamplesDirectory();
            bool foundLib = false;
            if (std::filesystem::exists(exDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(exDir)) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (ext == ".lib") {
                        if (ImGui::MenuItem(entry.path().filename().string().c_str())) {
                            emu.LoadAcfhLibraryFile(entry.path().string());
                        }
                        foundLib = true;
                    }
                }
            }
            if (!foundLib) {
                ImGui::TextDisabled("No pre-compiled .lib files in examples/");
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(" Unload All Libraries ")) {
            emu.ClearAcfhLibraries();
        }

        ImGui::Separator();

        if (ImGui::BeginTabBar("AcfhTabs")) {
            // Tab 1: Loaded Libraries
            if (ImGui::BeginTabItem("Loaded Libraries (.lib)")) {
                const auto& libs = emu.GetLoadedLibraries();
                if (libs.empty()) {
                    ImGui::TextDisabled("No dynamic libraries currently loaded in memory (0x%08X arena).", kLibraryArenaBase);
                } else {
                    if (ImGui::BeginTable("LibsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Library Name");
                        ImGui::TableSetupColumn("Base Address");
                        ImGui::TableSetupColumn("Code Size");
                        ImGui::TableSetupColumn("Total Mapped");
                        ImGui::TableSetupColumn("Exports");
                        ImGui::TableSetupColumn("Relocs");
                        ImGui::TableSetupColumn("Actions");
                        ImGui::TableHeadersRow();

                        for (size_t i = 0; i < libs.size(); ++i) {
                            const auto& lib = libs[i];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", lib.fileName.c_str());

                            ImGui::TableNextColumn();
                            ImGui::Text("0x%08X", lib.baseAddress);

                            ImGui::TableNextColumn();
                            ImGui::Text("%u B", lib.header.code_size);

                            ImGui::TableNextColumn();
                            ImGui::Text("%u B", lib.totalMappedSize);

                            ImGui::TableNextColumn();
                            ImGui::Text("%zu funcs", lib.exports.size());

                            ImGui::TableNextColumn();
                            ImGui::Text("%zu", lib.relocations.size());

                            ImGui::TableNextColumn();
                            ImGui::PushID(static_cast<int>(i));
                            if (ImGui::SmallButton("View Code")) {
                                state.disasmBaseAddress = lib.baseAddress;
                                snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
                                state.disasmFollowEip = false;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Unload")) {
                                emu.UnloadAcfhLibrary(lib.fileName);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }

                    // Detailed Exports accordion
                    for (size_t i = 0; i < libs.size(); ++i) {
                        const auto& lib = libs[i];
                        std::string treeLabel = lib.fileName + " — Exported Functions (" + std::to_string(lib.exports.size()) + ")";
                        if (ImGui::TreeNode(treeLabel.c_str())) {
                            if (ImGui::BeginTable("ExportsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                                ImGui::TableSetupColumn("Function Name");
                                ImGui::TableSetupColumn("Code Offset");
                                ImGui::TableSetupColumn("Runtime Address");
                                ImGui::TableSetupColumn("Param Size");
                                ImGui::TableSetupColumn("Action");
                                ImGui::TableHeadersRow();

                                for (size_t j = 0; j < lib.exports.size(); ++j) {
                                    const auto& exp = lib.exports[j];
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%s", exp.name.c_str());

                                    ImGui::TableNextColumn();
                                    ImGui::Text("+0x%04X", exp.codeOffset);

                                    ImGui::TableNextColumn();
                                    ImGui::Text("0x%08X", exp.runtimeAddress);

                                    ImGui::TableNextColumn();
                                    ImGui::Text("%u B", exp.paramSize);

                                    ImGui::TableNextColumn();
                                    ImGui::PushID(static_cast<int>(i * 1000 + j));
                                    if (ImGui::SmallButton("Disassemble")) {
                                        state.disasmBaseAddress = exp.runtimeAddress;
                                        snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
                                        state.disasmFollowEip = false;
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::EndTable();
                            }
                            ImGui::TreePop();
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            // Tab 2: Active Executable ACFH Header & Sections
            if (ImGui::BeginTabItem("Active Executable (.exe)")) {
                const auto& mainMod = emu.GetMainModule();
                if (!mainMod.has_value()) {
                    ImGui::TextDisabled("No ACFH executable loaded (running flat raw binary or uninitialized).");
                } else {
                    const auto& m = *mainMod;
                    ImGui::Text("File: %s", m.fileName.c_str());
                    ImGui::SameLine(350);
                    ImGui::Text("Format Version: %u.%u", m.header.version >> 16, m.header.version & 0xFFFF);

                    ImGui::Text("Entry Point: 0x%08X", (m.header.entry_point_offset != 0xFFFFFFFF) ? m.header.entry_point_offset : 0);
                    ImGui::SameLine(350);
                    ImGui::Text("Flags: 0x%08X [ %s%s%s%s%s]",
                        m.header.flags,
                        (m.header.flags & AC_FLAG_EXECUTABLE) ? "EXEC " : "",
                        (m.header.flags & AC_FLAG_DYNAMIC) ? "DYNAMIC " : "",
                        (m.header.flags & AC_FLAG_HAS_RELOCS) ? "RELOCS " : "",
                        (m.header.flags & AC_FLAG_HAS_FUNCS) ? "FUNCS " : "",
                        (m.header.flags & AC_FLAG_HAS_IMPORTS) ? "IMPORTS " : "");

                    ImGui::Separator();
                    ImGui::Text("ACFH Section Table:");
                    if (ImGui::BeginTable("SectionsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Section");
                        ImGui::TableSetupColumn("File Offset");
                        ImGui::TableSetupColumn("Memory Offset");
                        ImGui::TableSetupColumn("Size");
                        ImGui::TableSetupColumn("Description");
                        ImGui::TableHeadersRow();

                        auto RenderSecRow = [](const char* name, uint32_t fileOff, uint32_t memOff, uint32_t size, const char* desc) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%s", name);
                            ImGui::TableNextColumn(); ImGui::Text("0x%08X", fileOff);
                            ImGui::TableNextColumn(); ImGui::Text("0x%08X", memOff);
                            ImGui::TableNextColumn(); ImGui::Text("%u B", size);
                            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", desc);
                        };

                        RenderSecRow(".code", m.header.code_offset, m.header.code_offset, m.header.code_size, "Executable x86 instructions");
                        RenderSecRow(".data", m.header.data_offset, m.header.data_offset, m.header.data_size, "Initialized global/static data");
                        RenderSecRow(".rodata", m.header.rodata_offset, m.header.rodata_offset, m.header.rodata_size, "Read-only strings and constants");
                        RenderSecRow(".bss", m.header.bss_offset, m.header.bss_offset, m.header.bss_size, "Zero-initialized unmapped variables");
                        RenderSecRow("relocs", m.header.reloc_offset, 0, m.header.reloc_size, "Base relocation table");
                        RenderSecRow("funcs", m.header.func_table_offset, 0, m.header.func_table_size, "Exported function symbol table");
                        RenderSecRow("imports", m.header.import_table_offset, 0, m.header.import_table_size, "Dynamic library import table");

                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }

            // Tab 3: Import Table
            if (ImGui::BeginTabItem("Import Table")) {
                const auto& mainMod = emu.GetMainModule();
                if (!mainMod.has_value() || mainMod->imports.empty()) {
                    ImGui::TextDisabled("No imported functions defined in this executable.");
                } else {
                    if (ImGui::BeginTable("ImportsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Idx");
                        ImGui::TableSetupColumn("Library");
                        ImGui::TableSetupColumn("Function");
                        ImGui::TableSetupColumn("Code Patch Offset");
                        ImGui::TableSetupColumn("Target Address");
                        ImGui::TableSetupColumn("Status");
                        ImGui::TableHeadersRow();

                        for (size_t i = 0; i < mainMod->imports.size(); ++i) {
                            const auto& imp = mainMod->imports[i];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%03zu", i);

                            ImGui::TableNextColumn();
                            ImGui::Text("%s", imp.libName.c_str());

                            ImGui::TableNextColumn();
                            ImGui::Text("%s", imp.funcName.c_str());

                            ImGui::TableNextColumn();
                            ImGui::Text("+0x%04X (RAM: 0x%08X)", imp.codePatchOffset, imp.runtimePatchAddress);

                            ImGui::TableNextColumn();
                            if (imp.resolved) {
                                ImGui::Text("0x%08X", imp.resolvedAddress);
                            } else {
                                ImGui::TextDisabled("Unresolved");
                            }

                            ImGui::TableNextColumn();
                            if (imp.resolved) {
                                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Linked / Patched");
                            } else {
                                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Unresolved");
                            }
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
}

} // namespace em836
