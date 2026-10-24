/**
 * @file gui_editor.cpp
 * @brief In-GUI Code Editor with Line Numbers, Sample Templates, and AstraC Integration.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <vector>

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

bool GuiManager::LoadFileIntoEditor(GuiState& state, const std::string& filePath, Emulator* emu) {
    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::weakly_canonical(std::filesystem::absolute(filePath, ec), ec);
    std::string resolvedPath = ec ? filePath : absPath.string();

    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        if (emu) emu->GetConsole().WriteText("[Editor Error] Failed to open: " + resolvedPath + "\n");
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    state.editorText = content;
    state.editorFilePath = resolvedPath;
    state.editorModified = false;

    std::filesystem::path p(resolvedPath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    state.editorLanguage = (ext == ".ac") ? 1 : 0;

    std::string stemLower = p.stem().string();
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), [](unsigned char c) { return std::tolower(c); });
    bool is16Bit = (stemLower.rfind("16bit", 0) == 0) || (content.find(".use16") != std::string::npos);

    // Auto-detect target format
    if (stemLower.rfind("_lib") != std::string::npos || stemLower.find("lib_") != std::string::npos) {
        state.editorTargetFormat = 2; // ACFH .lib
    } else if (stemLower.rfind("_exe") != std::string::npos || stemLower.find("exe_") != std::string::npos ||
               content.find("#import") != std::string::npos || content.find(".import") != std::string::npos) {
        state.editorTargetFormat = 1; // ACFH .exe
    } else {
        state.editorTargetFormat = 0; // Raw .bin
    }

    if (emu) {
        uint32_t targetBits = is16Bit ? 16 : 32;
        if (emu->GetCpuMode() != targetBits) {
            emu->SetCpuMode(targetBits);
        }
        emu->GetConsole().WriteText("[Editor] Loaded: " + resolvedPath + (is16Bit ? " (16-Bit Mode)\n" : " (32-Bit Mode)\n"));
    }
    return true;
}

bool GuiManager::SaveEditorToFile(GuiState& state, const std::string& filePath, Emulator* emu) {
    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::weakly_canonical(std::filesystem::absolute(filePath, ec), ec);
    std::string resolvedPath = ec ? filePath : absPath.string();

    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        if (emu) emu->GetConsole().WriteText("[Editor Error] Failed to save to: " + resolvedPath + "\n");
        return false;
    }

    file << state.editorText;
    state.editorFilePath = resolvedPath;
    state.editorModified = false;
    if (emu) emu->GetConsole().WriteText("[Editor] Saved changes to: " + resolvedPath + "\n");
    return true;
}

static void ExecuteCompileAndRun(Emulator& emu, GuiState& state, bool runAfterCompile) {
    bool isAstraC = (state.editorLanguage == 1);
    std::string toolName = isAstraC ? "Astra C Compiler" : "AstraC Assembler";

    // If an example or source file is active on disk, save editor buffer directly to it
    if (!state.editorFilePath.empty()) {
        GuiManager::SaveEditorToFile(state, state.editorFilePath, &emu);
    }

    CompileOptions opts;
    opts.format = static_cast<TargetFormat>(state.editorTargetFormat);
    opts.emitOffsetTable = state.acfhOptOffsetTable;
    opts.emitFuncTable = state.acfhOptFuncTable;
    opts.emitImportTable = state.acfhOptImportTable;

    emu.GetConsole().AppendBuildLog("[Build] Invoking " + toolName + "...\n");

    CompilationResult res;
    if (!state.editorFilePath.empty()) {
        res = AstraCToolchain::CompileFile(state.editorFilePath, emu.GetConfig().astracPath, opts);
    } else {
        res = AstraCToolchain::CompileBuffer(state.editorText, isAstraC, emu.GetConfig().astracPath, opts);
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

    if (runAfterCompile && state.editorTargetFormat != 2) {
        emu.Start();
    }
}

void GuiManager::RenderEditorWindow(Emulator& emu, GuiState& state) {
    if (!state.showEditor) return;

    if (ImGui::Begin("Code Editor", &state.showEditor)) {
        // Toolbar controls
        const char* langItems[] = { "Assembly (.as)", "Astra C (.ac)" };
        ImGui::SetNextItemWidth(125);
        ImGui::Combo("##editor_lang", &state.editorLanguage, langItems, IM_ARRAYSIZE(langItems));

        ImGui::SameLine();
        const char* targetItems[] = { "Raw (.bin)", "ACFH Executable (.exe)", "ACFH Library (.lib)" };
        ImGui::SetNextItemWidth(170);
        ImGui::Combo("##editor_target_format", &state.editorTargetFormat, targetItems, IM_ARRAYSIZE(targetItems));

        if (state.editorTargetFormat != 0) {
            ImGui::SameLine();
            if (ImGui::Button(" Tables... ")) {
                ImGui::OpenPopup("acfh_tables_popup");
            }
            if (ImGui::BeginPopup("acfh_tables_popup")) {
                ImGui::TextDisabled("ACFH Header Tables");
                ImGui::Separator();
                ImGui::Checkbox("Relocation Table (ot)", &state.acfhOptOffsetTable);
                ImGui::Checkbox("Function Export Table (ft)", &state.acfhOptFuncTable);
                ImGui::Checkbox("Import Table (it)", &state.acfhOptImportTable);
                ImGui::EndPopup();
            }
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        const char* runBtnLabel = (state.editorTargetFormat == 2) ? " Compile & Load .lib " : " Compile & Run ";
        if (ImGui::Button(runBtnLabel)) {
            ExecuteCompileAndRun(emu, state, state.editorTargetFormat != 2);
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
                            LoadFileIntoEditor(state, entry.path().string(), &emu);
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
        if (ImGui::Button(" Save ")) {
            if (!state.editorFilePath.empty()) {
                SaveEditorToFile(state, state.editorFilePath, &emu);
            } else {
                std::filesystem::path scratchPath = FindExamplesDirectory() / "scratch.as";
                SaveEditorToFile(state, scratchPath.string(), &emu);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(" Reload ")) {
            if (!state.editorFilePath.empty()) {
                LoadFileIntoEditor(state, state.editorFilePath, &emu);
            }
        }

        ImGui::SameLine();
        std::string displayFile = state.editorFilePath.empty()
            ? "Unsaved Buffer"
            : std::filesystem::path(state.editorFilePath).filename().string();
        ImGui::TextDisabled("[%s%s]", displayFile.c_str(), state.editorModified ? " *" : "");
        if (!state.editorFilePath.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", state.editorFilePath.c_str());
        }

        ImGui::Separator();

        // -------------------------------------------------------------
        // Multiline Text Editor Region
        // -------------------------------------------------------------
        // Resize buffer dynamically if text grows
        if (state.editorText.capacity() < state.editorText.size() + 4096) {
            state.editorText.reserve(state.editorText.size() + 8192);
        }

        // Count lines for line number gutter
        size_t lineCount = 1;
        for (char c : state.editorText) {
            if (c == '\n') lineCount++;
        }

        // Layout: Line numbers gutter on left, text editor on right
        float gutterWidth = 35.0f;
        if (lineCount >= 100) gutterWidth = 45.0f;
        if (lineCount >= 1000) gutterWidth = 55.0f;

        ImVec2 contentSize = ImGui::GetContentRegionAvail();

        // Gutter
        ImGui::BeginChild("gutter_region", ImVec2(gutterWidth, contentSize.y), false, ImGuiWindowFlags_NoScrollbar);
        for (size_t l = 1; l <= lineCount; ++l) {
            ImGui::TextDisabled("%3zu", l);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Text input field
        ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
        std::vector<char> editBuf(state.editorText.begin(), state.editorText.end());
        editBuf.resize(std::max(editBuf.size() + 4096, size_t(32768)));

        if (ImGui::InputTextMultiline("##editor_multiline", editBuf.data(), editBuf.size(),
            ImVec2(contentSize.x - gutterWidth - 10, contentSize.y), editFlags)) {
            state.editorText = editBuf.data();
            state.editorModified = true;
        }
    }
    ImGui::End();
}

} // namespace em836
