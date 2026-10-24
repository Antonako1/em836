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

            ImGui::Separator();

            if (ImGui::MenuItem("Step Into", "F11", false, !isRunning)) {
                emu.StepInto();
            }

            if (ImGui::MenuItem("Step Over", "F10", false, !isRunning)) {
                emu.StepOver();
            }

            if (ImGui::MenuItem("Step Out", "Shift+F11", false, !isRunning)) {
                emu.StepOut();
            }

            ImGui::Separator();

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
            ImGui::MenuItem("Breakpoints & Watchpoints", nullptr, &state.showBreakpoints);
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

void GuiManager::RefreshDsmModules(Emulator& emu, GuiState& state) {
    state.dsmModules.clear();
    const std::string& astracPath = emu.GetConfig().astracPath;

    // 1. Main Executable Module
    std::string mainFile = emu.GetConfig().lastLoadedFile;
    if (mainFile.empty() && !state.editorFilePath.empty()) {
        mainFile = state.editorFilePath;
    }

    if (!mainFile.empty() && std::filesystem::exists(mainFile)) {
        DsmModuleView mainModView;
        mainModView.filePath = mainFile;
        mainModView.moduleName = std::filesystem::path(mainFile).filename().string();
        mainModView.isLibrary = false;
        mainModView.baseAddress = 0x00000000;

        const auto& mainMod = emu.GetMainModule();
        if (mainMod.has_value()) {
            mainModView.codeOffset = mainMod->header.code_offset;
            mainModView.codeSize = mainMod->header.code_size;
            if (!mainMod->fileName.empty()) {
                mainModView.moduleName = mainMod->fileName;
            }
        }

        DisassemblyResult res = AstraCToolchain::DisassembleFile(mainFile, astracPath, state.dsmBits, state.dsmOrg, true);
        if (res.success) {
            mainModView.lines = res.lines;
        }
        state.dsmModules.push_back(std::move(mainModView));
    }

    // 2. Loaded Dynamic Libraries
    const auto& libs = emu.GetLoadedLibraries();
    for (const auto& lib : libs) {
        DsmModuleView libModView;
        libModView.isLibrary = true;
        libModView.baseAddress = lib.baseAddress;
        libModView.codeOffset = lib.header.code_offset;
        libModView.codeSize = lib.header.code_size;
        libModView.moduleName = lib.fileName;
        libModView.filePath = lib.filePath;

        if (libModView.filePath.empty() || !std::filesystem::exists(libModView.filePath)) {
            std::filesystem::path search1 = std::filesystem::path(mainFile).parent_path() / libModView.moduleName;
            std::filesystem::path search2 = FindExamplesDirectory() / libModView.moduleName;
            if (std::filesystem::exists(search1)) {
                libModView.filePath = search1.string();
            } else if (std::filesystem::exists(search2)) {
                libModView.filePath = search2.string();
            }
        }

        if (!libModView.filePath.empty() && std::filesystem::exists(libModView.filePath)) {
            DisassemblyResult res = AstraCToolchain::DisassembleFile(libModView.filePath, astracPath, 32, 0, true);
            if (res.success) {
                libModView.lines = res.lines;
            }
        }

        state.dsmModules.push_back(std::move(libModView));
    }

    if (state.selectedDsmModuleIndex >= static_cast<int>(state.dsmModules.size())) {
        state.selectedDsmModuleIndex = 0;
    }

    if (!state.dsmModules.empty()) {
        state.dsmLines = state.dsmModules[state.selectedDsmModuleIndex].lines;
        state.dsmLoadedPath = state.dsmModules[state.selectedDsmModuleIndex].filePath;
    }
}

bool GuiManager::JumpToDsmSymbol(GuiState& state, const std::string& moduleName, const std::string& symbolName) {
    if (state.dsmModules.empty()) return false;

    auto iequals = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    };

    int targetModIdx = -1;
    if (!moduleName.empty()) {
        for (size_t i = 0; i < state.dsmModules.size(); ++i) {
            if (iequals(state.dsmModules[i].moduleName, moduleName) ||
                iequals(std::filesystem::path(state.dsmModules[i].filePath).filename().string(), moduleName)) {
                targetModIdx = static_cast<int>(i);
                break;
            }
        }
    }

    if (targetModIdx < 0) {
        targetModIdx = state.selectedDsmModuleIndex;
    }

    if (targetModIdx < 0 || targetModIdx >= static_cast<int>(state.dsmModules.size())) {
        return false;
    }

    state.selectedDsmModuleIndex = targetModIdx;
    state.dsmLines = state.dsmModules[targetModIdx].lines;
    state.dsmLoadedPath = state.dsmModules[targetModIdx].filePath;

    std::string needle = symbolName;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::toupper(c); });

    for (size_t lineIdx = 0; lineIdx < state.dsmLines.size(); ++lineIdx) {
        std::string upperLine = state.dsmLines[lineIdx];
        std::transform(upperLine.begin(), upperLine.end(), upperLine.begin(), [](unsigned char c) { return std::toupper(c); });
        if (upperLine.find("<" + needle + ">") != std::string::npos || upperLine.find("<" + needle + ":") != std::string::npos) {
            state.dsmSelectedLine = static_cast<int>(lineIdx);
            return true;
        }
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

    RenderBreakpointsAndWatchesWindow(emu, state);

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

void GuiManager::RenderBreakpointsAndWatchesWindow(Emulator& emu, GuiState& state) {
    if (!state.showBreakpoints) return;

    if (ImGui::Begin("Breakpoints & Watchpoints", &state.showBreakpoints)) {
        if (ImGui::BeginTabBar("bp_watch_tab_bar")) {
            // =============================================================
            // Tab 1: Execution Breakpoints
            // =============================================================
            auto bpList = emu.GetBreakpointsList();
            std::string bpTabTitle = "Breakpoints (" + std::to_string(bpList.size()) + ")";
            if (ImGui::BeginTabItem(bpTabTitle.c_str())) {
                static char bpQuickAddr[16] = "00000000";
                ImGui::Text("Address:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                ImGui::InputText("##bp_quick_addr", bpQuickAddr, sizeof(bpQuickAddr), ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::SameLine();
                if (ImGui::Button(" + Add Breakpoint ")) {
                    uint32_t a = static_cast<uint32_t>(strtoul(bpQuickAddr, nullptr, 16));
                    emu.AddBreakpoint(a);
                }
                ImGui::SameLine();
                if (ImGui::Button(" Clear All Breakpoints ")) {
                    emu.ClearBreakpoints();
                }

                ImGui::Separator();

                if (bpList.empty()) {
                    ImGui::TextDisabled("No active breakpoints. Click gutter in DSM/Live Disassembly or enter an address above.");
                } else {
                    if (ImGui::BeginTable("BpTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                        ImGui::TableSetupColumn("Module / Symbol", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                        ImGui::TableHeadersRow();

                        for (size_t i = 0; i < bpList.size(); ++i) {
                            const auto& bp = bpList[i];
                            ImGui::TableNextRow();

                            // 1. Enabled Checkbox
                            ImGui::TableNextColumn();
                            bool en = bp.enabled;
                            std::string cbId = "##bp_en_" + std::to_string(bp.address);
                            if (ImGui::Checkbox(cbId.c_str(), &en)) {
                                emu.SetBreakpointEnabled(bp.address, en);
                            }

                            // 2. Address
                            ImGui::TableNextColumn();
                            ImGui::Text("0x%08X", bp.address);

                            // 3. Module & Symbol
                            ImGui::TableNextColumn();
                            std::string sym = bp.symbol.empty()
                                ? (bp.moduleName.empty() ? "-" : bp.moduleName)
                                : (bp.moduleName.empty() ? bp.symbol : (bp.moduleName + " <" + bp.symbol + ">"));
                            ImGui::Text("%s", sym.c_str());

                            // 4. Hit Count
                            ImGui::TableNextColumn();
                            ImGui::Text("%llu", bp.hitCount);

                            // 5. Actions
                            ImGui::TableNextColumn();
                            ImGui::PushID(static_cast<int>(i));
                            if (ImGui::SmallButton("Jump")) {
                                state.disasmBaseAddress = bp.address;
                                snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", bp.address);
                                state.disasmFollowEip = false;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Delete")) {
                                emu.RemoveBreakpoint(bp.address);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }

                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }

            // =============================================================
            // Tab 2: Memory Watchpoints (Stop on Value Change / Write)
            // =============================================================
            auto wpList = emu.GetWatchpointsList();
            std::string wpTabTitle = "Watchpoints (" + std::to_string(wpList.size()) + ")";
            if (ImGui::BeginTabItem(wpTabTitle.c_str())) {
                static char wpName[32] = "";
                static char wpAddr[16] = "00000000";
                static int wpSizeIdx = 2; // 0 = 1 byte, 1 = 2 bytes, 2 = 4 bytes
                static int wpCondIdx = 0; // 0 = OnChange, 1 = OnWrite, 2 = OnEqual
                static char wpTargetVal[16] = "00000000";

                ImGui::Text("Name:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                ImGui::InputText("##wp_name_input", wpName, sizeof(wpName));

                ImGui::SameLine();
                ImGui::Text("Addr:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(75);
                ImGui::InputText("##wp_addr_input", wpAddr, sizeof(wpAddr), ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::SameLine();
                const char* sizeItems[] = { "1 B", "2 B", "4 B" };
                ImGui::SetNextItemWidth(55);
                ImGui::Combo("##wp_size_combo", &wpSizeIdx, sizeItems, IM_ARRAYSIZE(sizeItems));

                ImGui::SameLine();
                const char* condItems[] = { "On Change", "On Write", "Equals" };
                ImGui::SetNextItemWidth(90);
                ImGui::Combo("##wp_cond_combo", &wpCondIdx, condItems, IM_ARRAYSIZE(condItems));

                if (wpCondIdx == 2) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70);
                    ImGui::InputText("##wp_val_input", wpTargetVal, sizeof(wpTargetVal), ImGuiInputTextFlags_CharsHexadecimal);
                }

                ImGui::SameLine();
                if (ImGui::Button(" + Add Watch ")) {
                    uint32_t addr = static_cast<uint32_t>(strtoul(wpAddr, nullptr, 16));
                    uint32_t sz = (wpSizeIdx == 0) ? 1 : ((wpSizeIdx == 1) ? 2 : 4);
                    WatchCondition cond = static_cast<WatchCondition>(wpCondIdx);
                    uint32_t targetVal = static_cast<uint32_t>(strtoul(wpTargetVal, nullptr, 16));
                    std::string label = (wpName[0] != '\0') ? wpName : "watch";
                    emu.AddWatchpoint(addr, sz, cond, targetVal, label);
                    wpName[0] = '\0';
                }

                ImGui::SameLine();
                if (ImGui::Button(" Clear All Watches ")) {
                    emu.ClearWatchpoints();
                }

                ImGui::Separator();

                if (wpList.empty()) {
                    ImGui::TextDisabled("No active memory watchpoints. Add a watch above to pause execution when a memory location or variable changes.");
                } else {
                    if (ImGui::BeginTable("WpTable", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("En", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 35.0f);
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                        ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                        ImGui::TableSetupColumn("Current Val", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                        ImGui::TableSetupColumn("Prev Val", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 45.0f);
                        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableHeadersRow();

                        for (size_t i = 0; i < wpList.size(); ++i) {
                            const auto& wp = wpList[i];
                            ImGui::TableNextRow();

                            // 1. Enabled Checkbox
                            ImGui::TableNextColumn();
                            bool en = wp.enabled;
                            std::string cbId = "##wp_en_" + std::to_string(wp.id);
                            if (ImGui::Checkbox(cbId.c_str(), &en)) {
                                emu.SetWatchpointEnabled(wp.id, en);
                            }

                            // 2. ID
                            ImGui::TableNextColumn();
                            ImGui::Text("#%u", wp.id);

                            // 3. Name
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", wp.name.c_str());

                            // 4. Address
                            ImGui::TableNextColumn();
                            ImGui::Text("0x%08X", wp.address);

                            // 5. Condition
                            ImGui::TableNextColumn();
                            if (wp.condition == WatchCondition::OnChange) {
                                ImGui::Text("On Change");
                            } else if (wp.condition == WatchCondition::OnWrite) {
                                ImGui::Text("On Write");
                            } else {
                                ImGui::Text("== 0x%08X", wp.targetValue);
                            }

                            // 6. Current Val
                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "0x%08X", wp.currentValue);

                            // 7. Previous Val
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("0x%08X", wp.previousValue);

                            // 8. Hit Count
                            ImGui::TableNextColumn();
                            ImGui::Text("%llu", wp.hitCount);

                            // 9. Actions
                            ImGui::TableNextColumn();
                            ImGui::PushID(static_cast<int>(wp.id));
                            if (ImGui::SmallButton("Hex")) {
                                state.memoryCurrentAddress = wp.address;
                                snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", wp.address);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Del")) {
                                emu.RemoveWatchpoint(wp.id);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }

                        ImGui::EndTable();
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

