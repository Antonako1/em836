/**
 * @file gui_editor.cpp
 * @brief In-GUI Code Editor with Line Numbers, Sample Templates, and AstraC Integration.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>
#include <sstream>
#include <vector>

namespace em836 {

// -----------------------------------------------------------------------------
// Sample Assembly & Astra C Demonstration Programs
// -----------------------------------------------------------------------------

static const char* SAMPLE_GFX_DEMO =
"; =============================================================\n"
"; em836 Graphics 640x480 Demo (INT 0x88 Syscall ABI)\n"
"; Demonstrates mode setting, clear screen, rectangles, lines\n"
"; =============================================================\n"
"\n"
"main:\n"
"    ; 1. Set Video Mode to 640x480 Graphics (Mode 1)\n"
"    mov eax, 0x40           ; SYS_GFX_SET_MODE\n"
"    mov ebx, 1              ; Mode 1 = Graphics 640x480\n"
"    int 0x88\n"
"\n"
"    ; 2. Clear Screen with Navy Blue (Color 1)\n"
"    mov eax, 0x41           ; SYS_GFX_CLEAR\n"
"    mov ebx, 1              ; Color 1 (Blue)\n"
"    int 0x88\n"
"\n"
"    ; 3. Draw Red Header Rectangle\n"
"    mov eax, 0x44           ; SYS_GFX_FILL_RECT\n"
"    mov ebx, 20             ; X\n"
"    mov ecx, 20             ; Y\n"
"    mov edx, 600            ; Width\n"
"    mov esi, 60             ; Height\n"
"    mov edi, 4              ; Color 4 (Red)\n"
"    int 0x88\n"
"\n"
"    ; 4. Draw Green Center Box\n"
"    mov eax, 0x44           ; SYS_GFX_FILL_RECT\n"
"    mov ebx, 120            ; X\n"
"    mov ecx, 140            ; Y\n"
"    mov edx, 400            ; Width\n"
"    mov esi, 200            ; Height\n"
"    mov edi, 2              ; Color 2 (Green)\n"
"    int 0x88\n"
"\n"
"    ; 5. Draw Yellow Diagonal Line across box\n"
"    mov eax, 0x43           ; SYS_GFX_LINE\n"
"    mov ebx, 120            ; X0\n"
"    mov ecx, 140            ; Y0\n"
"    mov edx, 520            ; X1\n"
"    mov esi, 340            ; Y1\n"
"    mov edi, 14             ; Color 14 (Yellow)\n"
"    int 0x88\n"
"\n"
"    ; 6. Print status message to virtual terminal\n"
"    mov eax, 0x11           ; SYS_PRINT_STR\n"
"    mov ebx, msg            ; Address of string\n"
"    mov ecx, 0              ; Null-terminated\n"
"    int 0x88\n"
"\n"
"    ; 7. Exit program cleanly\n"
"    mov eax, 0x01           ; SYS_EXIT\n"
"    mov ebx, 0              ; Exit code 0\n"
"    int 0x88\n"
"\n"
"msg:\n"
"    db \"[Guest] Graphics demo completed successfully!\", 10, 0\n";

static const char* SAMPLE_VGA_TEXT_DEMO =
"; =============================================================\n"
"; em836 VGA Text Mode Demo (0xB8000 Direct Memory Writes)\n"
"; Writes colored characters directly to the 80x25 text buffer\n"
"; =============================================================\n"
"\n"
"main:\n"
"    ; Ensure Text Mode (80x25)\n"
"    mov eax, 0x40           ; SYS_GFX_SET_MODE\n"
"    mov ebx, 0              ; Mode 0 = Text 80x25\n"
"    int 0x88\n"
"\n"
"    mov edi, 0x000B8000     ; Base address of VGA Text buffer\n"
"\n"
"    ; Write 'H' (0x48) with Yellow on Blue attribute (0x1E)\n"
"    mov byte [edi], 0x48\n"
"    mov byte [edi + 1], 0x1E\n"
"\n"
"    ; Write 'E' (0x45)\n"
"    mov byte [edi + 2], 0x45\n"
"    mov byte [edi + 3], 0x1E\n"
"\n"
"    ; Write 'L' (0x4C)\n"
"    mov byte [edi + 4], 0x4C\n"
"    mov byte [edi + 5], 0x1E\n"
"\n"
"    ; Write 'L' (0x4C)\n"
"    mov byte [edi + 6], 0x4C\n"
"    mov byte [edi + 7], 0x1E\n"
"\n"
"    ; Write 'O' (0x4F)\n"
"    mov byte [edi + 8], 0x4F\n"
"    mov byte [edi + 9], 0x1E\n"
"\n"
"    ; Exit\n"
"    mov eax, 0x01           ; SYS_EXIT\n"
"    mov ebx, 0\n"
"    int 0x88\n";

static const char* SAMPLE_COM1_DEMO =
"; =============================================================\n"
"; em836 COM1 Serial Port I/O Demo\n"
"; Writes characters to UART Data Port (0x03F8)\n"
"; =============================================================\n"
"\n"
"main:\n"
"    mov dx, 0x03F8          ; COM1 UART Port\n"
"    mov al, 0x48            ; 'H'\n"
"    out dx, al\n"
"    mov al, 0x65            ; 'e'\n"
"    out dx, al\n"
"    mov al, 0x6C            ; 'l'\n"
"    out dx, al\n"
"    mov al, 0x6C            ; 'l'\n"
"    out dx, al\n"
"    mov al, 0x6F            ; 'o'\n"
"    out dx, al\n"
"    mov al, 0x21            ; '!'\n"
"    out dx, al\n"
"    mov al, 0x0A            ; Newline\n"
"    out dx, al\n"
"\n"
"    mov eax, 0x01           ; SYS_EXIT\n"
"    mov ebx, 0\n"
"    int 0x88\n";

static void ExecuteCompileAndRun(Emulator& emu, GuiState& state, bool runAfterCompile) {
    bool isAstraC = (state.editorLanguage == 1);
    std::string toolName = isAstraC ? "Astra C Compiler" : "AstraC Assembler";

    emu.GetConsole().AppendBuildLog("[Build] Invoking " + toolName + "...\n");
    CompilationResult res = AstraCToolchain::CompileBuffer(state.editorText, isAstraC, emu.GetConfig().astracPath);

    emu.GetConsole().AppendBuildLog(res.outputLog + "\n");

    if (!res.success) {
        emu.GetConsole().AppendBuildLog("[Build Error] " + res.errorMessage + "\n");
        return;
    }

    emu.GetConsole().AppendBuildLog("[Build Success] Output binary: " + res.binaryPath + "\n");

    AstraCToolchain::IngestFile(res.binaryPath, emu, emu.GetConfig().astracPath);
    if (runAfterCompile) {
        emu.Start();
    }
}

void GuiManager::RenderEditorWindow(Emulator& emu, GuiState& state) {
    if (!state.showEditor) return;

    if (ImGui::Begin("Code Editor", &state.showEditor)) {
        // Toolbar controls
        const char* langItems[] = { "Assembly (.as)", "Astra C (.ac)" };
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("##editor_lang", &state.editorLanguage, langItems, IM_ARRAYSIZE(langItems));

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(" Compile & Run ")) {
            ExecuteCompileAndRun(emu, state, true);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button(" Compile Only ")) {
            ExecuteCompileAndRun(emu, state, false);
        }

        ImGui::SameLine();
        if (ImGui::Button("Load Sample Code")) {
            ImGui::OpenPopup("sample_templates_popup");
        }

        if (ImGui::BeginPopup("sample_templates_popup")) {
            if (ImGui::MenuItem("VGA 640x480 Graphics Demo (INT 0x88)")) {
                state.editorText = SAMPLE_GFX_DEMO;
                state.editorLanguage = 0;
            }
            if (ImGui::MenuItem("VGA Text Mode Demo (0xB8000)")) {
                state.editorText = SAMPLE_VGA_TEXT_DEMO;
                state.editorLanguage = 0;
            }
            if (ImGui::MenuItem("COM1 Serial Port Demo (0x03F8)")) {
                state.editorText = SAMPLE_COM1_DEMO;
                state.editorLanguage = 0;
            }
            ImGui::EndPopup();
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
