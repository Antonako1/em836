/**
 * @file gui.hpp
 * @brief Dear ImGui User Interface Components and Docking Viewports.
 *
 * This header defines the GUI views, window rendering routines, and state model
 * for em836's interactive debugger interface.
 *
 * Windows:
 * 1. CPU Registers: Displays EAX-EDI, EIP, ESP, EBP, individual EFLAGS bit badges,
 *    and instruction counters.
 * 2. Execution Controls: Run, Pause, Step, Reset, Clock speed dropdown, architecture switch.
 * 3. Disassembly: Real-time Capstone decoding with current EIP marker (=>) and clickable breakpoints.
 * 4. Memory Hex Viewer: 16-byte scrollable memory dump with ASCII sidebar and jump targets.
 * 5. VGA Canvas: Dual-mode rendering of 80x25 text mode and 640x480 graphics framebuffer.
 * 6. Console Window: Dual-purpose terminal showing output, input prompt, and AstraC build logs.
 * 7. In-GUI Code Editor: Multiline editor with line numbers, sample loaders, and Compile & Run.
 * 8. Modals: Load Options Dialog, Settings Dialog, About em836.
 */

#pragma once

#include "emulator.hpp"
#include <string>
#include <vector>
#include <filesystem>

// Forward declarations
struct SDL_Renderer;

namespace em836 {

/**
 * @struct GuiState
 * @brief Encapsulates transient UI widget state, open dialog flags, and editor text buffers.
 */
struct GuiState {
    // Window visibility toggles
    bool showRegisters = true;
    bool showExecution = true;
    bool showDisassembly = true;
    bool showMemory = true;
    bool showVga = true;
    bool showConsole = true;
    bool showEditor = true;

    // Dialog & Modal flags
    bool showSettingsModal = false;
    bool showLoadOptionsModal = false;
    bool showAboutModal = false;

    // Disassembly view state
    uint32_t disasmBaseAddress = 0x00000000;
    bool disasmFollowEip = true;
    char disasmAddressInput[16] = "00000000";

    // Memory Hex Viewer state
    char memoryAddressInput[16] = "00000000";
    uint32_t memoryCurrentAddress = 0x00000000;
    int memoryRowsToDisplay = 16;

    // Console Prompt state
    char consoleInputBuffer[512] = "";
    std::vector<std::string> commandHistory;
    int historyIndex = -1;

    // Code Editor state
    std::string editorText;
    int editorLanguage = 0; ///< 0 = Assembly (.as), 1 = Astra C (.ac)
    std::string editorFilePath = "";
    bool editorModified = false;

    // Load Options Modal state
    char loadAddressInput[16] = "00000000";
    char loadEipInput[16] = "00000000";
    char loadEspInput[16] = "00800000";
    std::string pendingLoadFilePath = "";

    // File picker state
    char fileOpenPathInput[260] = "";
    bool showFileOpenModal = false;
};

/**
 * @class GuiManager
 * @brief Static rendering coordinator for all Dear ImGui debug windows.
 */
class GuiManager {
public:
    /**
     * @brief Initializes default sample code and styles for the GUI.
     * @param state GuiState reference to initialize.
     */
    static void Initialize(GuiState& state);

    /**
     * @brief Locates the examples/ directory by searching upward from the current working directory.
     * @return Canonical absolute path to the examples directory, or "examples" if not found.
     */
    static std::filesystem::path FindExamplesDirectory();

    /**
     * @brief Loads a source file from disk into the in-GUI editor.
     * @param state GuiState reference.
     * @param filePath Path to the file on disk.
     * @param emu Optional Emulator pointer for console logging.
     * @return True if loaded successfully, false otherwise.
     */
    static bool LoadFileIntoEditor(GuiState& state, const std::string& filePath, Emulator* emu = nullptr);

    /**
     * @brief Saves the current editor text buffer to disk.
     * @param state GuiState reference.
     * @param filePath Path to the target file on disk.
     * @param emu Optional Emulator pointer for console logging.
     * @return True if saved successfully, false otherwise.
     */
    static bool SaveEditorToFile(GuiState& state, const std::string& filePath, Emulator* emu = nullptr);

    /**
     * @brief Master render entry point called every frame inside the main loop.
     * @param emu Active Emulator engine instance.
     * @param state Active GUI state.
     * @param renderer Active SDL_Renderer.
     */
    static void RenderAll(Emulator& emu, GuiState& state, SDL_Renderer* renderer);

    // Individual Window Renderers
    static void RenderMainMenuBar(Emulator& emu, GuiState& state, SDL_Renderer* renderer);
    static void RenderRegistersWindow(Emulator& emu, GuiState& state);
    static void RenderExecutionWindow(Emulator& emu, GuiState& state);
    static void RenderDisassemblyWindow(Emulator& emu, GuiState& state);
    static void RenderMemoryWindow(Emulator& emu, GuiState& state);
    static void RenderVgaWindow(Emulator& emu, GuiState& state, SDL_Renderer* renderer);
    static void RenderConsoleWindow(Emulator& emu, GuiState& state);
    static void RenderEditorWindow(Emulator& emu, GuiState& state);

    // Modal Dialogs
    static void RenderSettingsModal(Emulator& emu, GuiState& state);
    static void RenderLoadOptionsModal(Emulator& emu, GuiState& state);
    static void RenderFileOpenModal(Emulator& emu, GuiState& state);
    static void RenderAboutModal(GuiState& state);
};

} // namespace em836
