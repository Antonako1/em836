/**
 * @file gui_editor.cpp
 * @brief In-GUI Code Editor with Tabs, Syntax Highlighting, Sample Templates, and AstraC Integration.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include "em836/syntax.hpp"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace em836 {

// -----------------------------------------------------------------------------
// Examples and File Management Helpers
// -----------------------------------------------------------------------------

std::filesystem::path GuiManager::FindExamplesDirectory() {
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::current_path(ec);
    if (!ec) {
        std::filesystem::path probe = cur;
        for (int depth = 0; depth < 6; ++depth) {
            std::filesystem::path candidate = probe / "examples";
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
                return std::filesystem::weakly_canonical(std::filesystem::absolute(candidate, ec), ec);
            }
            if (!probe.has_parent_path() || probe.parent_path() == probe) {
                break;
            }
            probe = probe.parent_path();
        }
    }
    return std::filesystem::path("examples");
}

int GuiManager::CreateNewEditorTab(GuiState& state, int language) {
    EditorTab tab;
    tab.id = state.nextUntitledId++;
    tab.language = language;
    std::string ext = (language == 1) ? ".ac" : ".as";
    tab.title = "untitled-" + std::to_string(tab.id) + ext;
    tab.filePath = "";
    tab.modified = false;
    tab.targetFormat = 0;
    tab.viewMode = 0;

    if (language == 1) {
        tab.text = "// New Astra C source file\n\nu32 main() {\n    return 0;\n}\n";
    } else {
        tab.text = "; New Assembly source file\n.use32\n.org 0x00000000\n\nstart:\n    mov eax, 1\n    int 0x88\n";
    }

    state.editorTabs.push_back(std::move(tab));
    state.activeEditorTabIndex = static_cast<int>(state.editorTabs.size() - 1);

    // Sync legacy fields
    auto& active = state.editorTabs[state.activeEditorTabIndex];
    state.editorText = active.text;
    state.editorFilePath = active.filePath;
    state.editorLanguage = active.language;
    state.editorTargetFormat = active.targetFormat;
    state.editorModified = active.modified;

    return state.activeEditorTabIndex;
}

void GuiManager::CloseEditorTab(GuiState& state, int tabIndex) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(state.editorTabs.size())) {
        return;
    }

    state.editorTabs.erase(state.editorTabs.begin() + tabIndex);

    if (state.editorTabs.empty()) {
        CreateNewEditorTab(state, 0);
    } else {
        if (state.activeEditorTabIndex >= static_cast<int>(state.editorTabs.size())) {
            state.activeEditorTabIndex = static_cast<int>(state.editorTabs.size() - 1);
        }
        auto& active = state.editorTabs[state.activeEditorTabIndex];
        state.editorText = active.text;
        state.editorFilePath = active.filePath;
        state.editorLanguage = active.language;
        state.editorTargetFormat = active.targetFormat;
        state.editorModified = active.modified;
    }
}

int GuiManager::FindOrOpenEditorTab(GuiState& state, const std::string& filePath, Emulator* emu) {
    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::weakly_canonical(std::filesystem::absolute(filePath, ec), ec);
    std::string resolvedPath = ec ? filePath : absPath.string();

    // Check if already open
    for (size_t i = 0; i < state.editorTabs.size(); ++i) {
        if (state.editorTabs[i].filePath == resolvedPath) {
            state.activeEditorTabIndex = static_cast<int>(i);
            auto& active = state.editorTabs[i];
            state.editorText = active.text;
            state.editorFilePath = active.filePath;
            state.editorLanguage = active.language;
            state.editorTargetFormat = active.targetFormat;
            state.editorModified = active.modified;
            return static_cast<int>(i);
        }
    }

    // Read from disk
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        if (emu) emu->GetConsole().WriteText("[Editor Error] Failed to open: " + resolvedPath + "\n");
        return -1;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    EditorTab tab;
    tab.id = state.nextUntitledId++;
    tab.filePath = resolvedPath;
    tab.title = std::filesystem::path(resolvedPath).filename().string();
    tab.text = content;
    tab.modified = false;
    tab.viewMode = 0;

    std::filesystem::path p(resolvedPath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    tab.language = (ext == ".ac") ? 1 : 0;

    std::string stemLower = p.stem().string();
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), [](unsigned char c) { return std::tolower(c); });
    bool is16Bit = (stemLower.rfind("16bit", 0) == 0) || (content.find(".use16") != std::string::npos);

    // Auto-detect target format
    if (stemLower.rfind("_lib") != std::string::npos || stemLower.find("lib_") != std::string::npos) {
        tab.targetFormat = 2; // ACFH .lib
    } else if (stemLower.rfind("_exe") != std::string::npos || stemLower.find("exe_") != std::string::npos ||
               content.find("#import") != std::string::npos || content.find(".import") != std::string::npos) {
        tab.targetFormat = 1; // ACFH .exe
    } else {
        tab.targetFormat = 0; // Raw .bin
    }

    state.editorTabs.push_back(std::move(tab));
    state.activeEditorTabIndex = static_cast<int>(state.editorTabs.size() - 1);

    auto& active = state.editorTabs[state.activeEditorTabIndex];
    state.editorText = active.text;
    state.editorFilePath = active.filePath;
    state.editorLanguage = active.language;
    state.editorTargetFormat = active.targetFormat;
    state.editorModified = active.modified;

    if (emu) {
        uint32_t targetBits = is16Bit ? 16 : 32;
        if (emu->GetCpuMode() != targetBits) {
            emu->SetCpuMode(targetBits);
        }
        emu->GetConsole().WriteText("[Editor] Opened in tab: " + resolvedPath + (is16Bit ? " (16-Bit Mode)\n" : " (32-Bit Mode)\n"));
    }

    return state.activeEditorTabIndex;
}

bool GuiManager::SaveEditorTabToFile(GuiState& state, int tabIndex, const std::string& filePath, Emulator* emu) {
    int idx = (tabIndex < 0) ? state.activeEditorTabIndex : tabIndex;
    if (idx < 0 || idx >= static_cast<int>(state.editorTabs.size())) {
        return false;
    }

    auto& tab = state.editorTabs[idx];
    std::string targetPath = filePath.empty() ? tab.filePath : filePath;
    if (targetPath.empty()) {
        std::string ext = (tab.language == 1) ? ".ac" : ".as";
        targetPath = (FindExamplesDirectory() / ("untitled-" + std::to_string(tab.id) + ext)).string();
    }

    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::weakly_canonical(std::filesystem::absolute(targetPath, ec), ec);
    std::string resolvedPath = ec ? targetPath : absPath.string();

    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        if (emu) emu->GetConsole().WriteText("[Editor Error] Failed to save to: " + resolvedPath + "\n");
        return false;
    }

    file << tab.text;
    tab.filePath = resolvedPath;
    tab.title = std::filesystem::path(resolvedPath).filename().string();
    tab.modified = false;

    if (idx == state.activeEditorTabIndex) {
        state.editorFilePath = tab.filePath;
        state.editorModified = false;
    }

    if (emu) emu->GetConsole().WriteText("[Editor] Saved changes to: " + resolvedPath + "\n");
    return true;
}

bool GuiManager::LoadFileIntoEditor(GuiState& state, const std::string& filePath, Emulator* emu) {
    return (FindOrOpenEditorTab(state, filePath, emu) >= 0);
}

bool GuiManager::SaveEditorToFile(GuiState& state, const std::string& filePath, Emulator* emu) {
    return SaveEditorTabToFile(state, state.activeEditorTabIndex, filePath, emu);
}

static void ExecuteCompileAndRun(Emulator& emu, GuiState& state, bool runAfterCompile) {
    if (state.editorTabs.empty()) return;
    if (state.activeEditorTabIndex < 0 || state.activeEditorTabIndex >= static_cast<int>(state.editorTabs.size())) {
        state.activeEditorTabIndex = 0;
    }

    auto& tab = state.editorTabs[state.activeEditorTabIndex];
    bool isAstraC = (tab.language == 1);
    std::string toolName = isAstraC ? "Astra C Compiler" : "AstraC Assembler";

    // If an example or source file is active on disk, save editor buffer directly to it
    if (!tab.filePath.empty()) {
        GuiManager::SaveEditorTabToFile(state, state.activeEditorTabIndex, tab.filePath, &emu);
    }

    CompileOptions opts;
    opts.format = static_cast<TargetFormat>(tab.targetFormat);
    opts.emitOffsetTable = tab.acfhOptOffsetTable;
    opts.emitFuncTable = tab.acfhOptFuncTable;
    opts.emitImportTable = tab.acfhOptImportTable;

    emu.GetConsole().AppendBuildLog("[Build] Invoking " + toolName + "...\n");

    CompilationResult res;
    if (!tab.filePath.empty()) {
        res = AstraCToolchain::CompileFile(tab.filePath, emu.GetConfig().astracPath, opts);
    } else {
        res = AstraCToolchain::CompileBuffer(tab.text, isAstraC, emu.GetConfig().astracPath, opts);
    }

    emu.GetConsole().AppendBuildLog(res.outputLog + "\n");

    if (!res.success) {
        emu.GetConsole().AppendBuildLog("[Build Error] " + res.errorMessage + "\n");
        return;
    }

    emu.GetConsole().AppendBuildLog("[Build Success] Output binary: " + res.binaryPath + "\n");

    AstraCToolchain::IngestFile(res.binaryPath, emu, emu.GetConfig().astracPath);
    state.disasmBaseAddress = emu.GetCodeBaseAddress();
    snprintf(state.disasmAddressInput, sizeof(state.disasmAddressInput), "%08X", state.disasmBaseAddress);
    state.disasmFollowEip = true;

    if (runAfterCompile && tab.targetFormat != 2) {
        emu.Start();
    }
}

static void RenderHighlightedSource(const std::string& text, int language, float availWidth, float availHeight) {
    ImGui::BeginChild("syntax_highlight_region", ImVec2(availWidth, availHeight), true, ImGuiWindowFlags_HorizontalScrollbar);

    std::istringstream stream(text);
    std::string line;
    size_t lineNum = 1;
    bool inBlockComment = false;

    // Fixed-pitch monospace layout
    while (std::getline(stream, line)) {
        ImGui::TextDisabled("%3zu | ", lineNum++);
        ImGui::SameLine(0, 0);

        if (language == 1) {
            std::vector<SyntaxToken> tokens = SyntaxHighlighter::TokenizeAc(line, inBlockComment);
            SyntaxHighlighter::RenderColoredLine(tokens);
        } else {
            std::vector<SyntaxToken> tokens = SyntaxHighlighter::TokenizeAs(line);
            SyntaxHighlighter::RenderColoredLine(tokens);
        }
    }

    ImGui::EndChild();
}

void GuiManager::RenderEditorWindow(Emulator& emu, GuiState& state) {
    if (!state.showEditor) return;

    if (state.editorTabs.empty()) {
        CreateNewEditorTab(state, 0);
    }
    if (state.activeEditorTabIndex < 0 || state.activeEditorTabIndex >= static_cast<int>(state.editorTabs.size())) {
        state.activeEditorTabIndex = 0;
    }

    if (ImGui::Begin("Code Editor", &state.showEditor)) {
        // Tab Bar
        if (ImGui::BeginTabBar("editor_tab_bar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_TabListPopupButton)) {
            for (int i = 0; i < static_cast<int>(state.editorTabs.size()); ++i) {
                auto& curTab = state.editorTabs[i];
                std::string tabLabel = (curTab.title.empty() ? "untitled" : curTab.title) + (curTab.modified ? " *###tab_" : "###tab_") + std::to_string(curTab.id);
                bool tabOpen = true;
                ImGuiTabItemFlags tabFlags = (i == state.activeEditorTabIndex) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tabLabel.c_str(), &tabOpen, tabFlags)) {
                    state.activeEditorTabIndex = i;
                    ImGui::EndTabItem();
                }
                if (!tabOpen) {
                    CloseEditorTab(state, i);
                    break;
                }
            }

            // New Tab button in Tab Bar
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                CreateNewEditorTab(state, 0);
            }
            ImGui::EndTabBar();
        }

        if (state.editorTabs.empty()) {
            CreateNewEditorTab(state, 0);
        }
        if (state.activeEditorTabIndex < 0 || state.activeEditorTabIndex >= static_cast<int>(state.editorTabs.size())) {
            state.activeEditorTabIndex = 0;
        }

        EditorTab& activeTab = state.editorTabs[state.activeEditorTabIndex];

        // Keep legacy state updated
        state.editorText = activeTab.text;
        state.editorFilePath = activeTab.filePath;
        state.editorLanguage = activeTab.language;
        state.editorTargetFormat = activeTab.targetFormat;
        state.editorModified = activeTab.modified;

        // Toolbar controls
        const char* langItems[] = { "Assembly (.as)", "Astra C (.ac)" };
        ImGui::SetNextItemWidth(125);
        if (ImGui::Combo("##editor_lang", &activeTab.language, langItems, IM_ARRAYSIZE(langItems))) {
            state.editorLanguage = activeTab.language;
            if (activeTab.filePath.empty()) {
                activeTab.title = "untitled-" + std::to_string(activeTab.id) + (activeTab.language == 1 ? ".ac" : ".as");
            }
        }

        ImGui::SameLine();
        const char* targetItems[] = { "Raw (.bin)", "ACFH Executable (.exe)", "ACFH Library (.lib)" };
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("##editor_target_format", &activeTab.targetFormat, targetItems, IM_ARRAYSIZE(targetItems))) {
            state.editorTargetFormat = activeTab.targetFormat;
        }

        if (activeTab.targetFormat != 0) {
            ImGui::SameLine();
            if (ImGui::Button(" Tables... ")) {
                ImGui::OpenPopup("acfh_tables_popup");
            }
            if (ImGui::BeginPopup("acfh_tables_popup")) {
                ImGui::TextDisabled("ACFH Header Tables");
                ImGui::Separator();
                ImGui::Checkbox("Relocation Table (ot)", &activeTab.acfhOptOffsetTable);
                ImGui::Checkbox("Function Export Table (ft)", &activeTab.acfhOptFuncTable);
                ImGui::Checkbox("Import Table (it)", &activeTab.acfhOptImportTable);
                ImGui::EndPopup();
            }
        }

        ImGui::SameLine();
        const char* viewModeItems[] = { "Edit", "Highlighted", "Split" };
        ImGui::SetNextItemWidth(105);
        ImGui::Combo("##editor_view_mode", &activeTab.viewMode, viewModeItems, IM_ARRAYSIZE(viewModeItems));

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        const char* runBtnLabel = (activeTab.targetFormat == 2) ? " Compile & Load .lib " : " Compile & Run ";
        if (ImGui::Button(runBtnLabel)) {
            ExecuteCompileAndRun(emu, state, activeTab.targetFormat != 2);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button(" Compile Only ")) {
            ExecuteCompileAndRun(emu, state, false);
        }

        ImGui::SameLine();
        if (ImGui::Button(" Examples... ")) {
            ImGui::OpenPopup("examples_popup");
        }

        if (ImGui::BeginPopup("examples_popup")) {
            std::filesystem::path examplesDir = FindExamplesDirectory();
            bool foundAny = false;
            if (std::filesystem::exists(examplesDir) && std::filesystem::is_directory(examplesDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(examplesDir)) {
                    if (!entry.is_regular_file()) continue;
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (ext == ".as" || ext == ".asm" || ext == ".ac") {
                        // Skip compiler-generated .AS intermediate files when a matching .ac source file exists
                        if (ext == ".as") {
                            std::filesystem::path matchingAc = entry.path().parent_path() / (entry.path().stem().string() + ".ac");
                            if (std::filesystem::exists(matchingAc)) {
                                continue;
                            }
                        }
                        std::string fname = entry.path().filename().string();
                        if (ImGui::MenuItem(fname.c_str())) {
                            FindOrOpenEditorTab(state, entry.path().string(), &emu);
                        }
                        foundAny = true;
                    }
                }
            }
            if (!foundAny) {
                ImGui::TextDisabled("No example files found in examples/");
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(" + New ")) {
            CreateNewEditorTab(state, activeTab.language);
        }

        ImGui::SameLine();
        if (ImGui::Button(" Save ")) {
            SaveEditorTabToFile(state, state.activeEditorTabIndex, activeTab.filePath, &emu);
        }

        ImGui::SameLine();
        if (ImGui::Button(" Reload ")) {
            if (!activeTab.filePath.empty()) {
                LoadFileIntoEditor(state, activeTab.filePath, &emu);
            }
        }

        ImGui::SameLine();
        std::string displayFile = activeTab.filePath.empty()
            ? activeTab.title
            : std::filesystem::path(activeTab.filePath).filename().string();
        ImGui::TextDisabled("[%s%s]", displayFile.c_str(), activeTab.modified ? " *" : "");
        if (!activeTab.filePath.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", activeTab.filePath.c_str());
        }

        ImGui::Separator();

        // -------------------------------------------------------------
        // Main Editor Area Rendering (Edit, Highlighted, or Split)
        // -------------------------------------------------------------
        ImVec2 contentSize = ImGui::GetContentRegionAvail();

        auto renderEditableInput = [&](float width, float height) {
            // Count lines for line number gutter
            size_t lineCount = 1;
            for (char c : activeTab.text) {
                if (c == '\n') lineCount++;
            }

            float gutterWidth = 35.0f;
            if (lineCount >= 100) gutterWidth = 45.0f;
            if (lineCount >= 1000) gutterWidth = 55.0f;

            // Gutter
            ImGui::BeginChild("gutter_region", ImVec2(gutterWidth, height), false, ImGuiWindowFlags_NoScrollbar);
            for (size_t l = 1; l <= lineCount; ++l) {
                ImGui::TextDisabled("%3zu", l);
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Text input field
            ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
            std::vector<char> editBuf(activeTab.text.begin(), activeTab.text.end());
            editBuf.resize(std::max(editBuf.size() + 4096, size_t(32768)));

            if (ImGui::InputTextMultiline("##editor_multiline", editBuf.data(), editBuf.size(),
                ImVec2(width - gutterWidth - 10, height), editFlags)) {
                activeTab.text = editBuf.data();
                activeTab.modified = true;
                state.editorText = activeTab.text;
                state.editorModified = true;
            }
        };

        if (activeTab.viewMode == 0) {
            // 0: Full Editable View
            renderEditableInput(contentSize.x, contentSize.y);
        } else if (activeTab.viewMode == 1) {
            // 1: Full Highlighted View
            RenderHighlightedSource(activeTab.text, activeTab.language, contentSize.x, contentSize.y);
        } else {
            // 2: Split View (Edit on Left, Syntax Highlighted on Right)
            float halfWidth = (contentSize.x - 8.0f) * 0.5f;

            ImGui::BeginChild("split_edit_pane", ImVec2(halfWidth, contentSize.y), false);
            ImGui::TextDisabled("Source Editor (Input)");
            ImGui::Separator();
            renderEditableInput(halfWidth, contentSize.y - 25.0f);
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("split_preview_pane", ImVec2(halfWidth, contentSize.y), false);
            ImGui::TextDisabled("Syntax Highlight Preview");
            ImGui::Separator();
            RenderHighlightedSource(activeTab.text, activeTab.language, halfWidth, contentSize.y - 25.0f);
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

} // namespace em836
