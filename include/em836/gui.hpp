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
 * @struct EditorTab
 * @brief Represents an open file or document buffer in the in-GUI code editor.
 */
struct EditorTab {
    int id = 0;                     ///< Unique tab identifier
    std::string title;              ///< Tab display label (e.g. "untitled-1.as" or "demo.ac")
    std::string filePath;           ///< Path to file on disk (or empty if unsaved)
    std::string text;               ///< Buffer contents
    bool modified = false;          ///< True if text has changed since last save/load
    int language = 0;               ///< 0 = Assembly (.as), 1 = Astra C (.ac)
    int targetFormat = 0;           ///< 0 = Raw .bin, 1 = ACFH .exe, 2 = ACFH .lib
    bool acfhOptOffsetTable = true;
    bool acfhOptFuncTable = true;
    bool acfhOptImportTable = true;
    int viewMode = 0;               ///< 0 = Edit, 1 = Highlighted, 2 = Split View
};

/**
 * @struct DsmModuleView
 * @brief Represents disassembly listing and symbols for an executable or dynamic library module.
 */
struct DsmModuleView {
    std::string moduleName;         ///< Display name (e.g. "exe_import_demo.exe", "math_lib.lib")
    std::string filePath;           ///< Path to binary or .DSM file
    bool isLibrary = false;         ///< True if module is a dynamic library (.lib)
    uint32_t baseAddress = 0;       ///< Runtime base address (0x0 for exe, 0x00100000+ for libs)
    uint32_t codeOffset = 0;        ///< Offset to .code section in binary (e.g. 0x70)
    uint32_t codeSize = 0;          ///< Size of .code section in bytes
    std::vector<std::string> lines; ///< Disassembled text lines
};

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
    bool showBreakpoints = true;

    // Dialog & Modal flags
    bool showSettingsModal = false;
    bool showLoadOptionsModal = false;
    bool showAboutModal = false;

    // Layout management
    bool requestLayoutReset = false;

    // Disassembly view state
    uint32_t disasmBaseAddress = 0x00000000;
    bool disasmFollowEip = true;
    char disasmAddressInput[16] = "00000000";
    int disasmActiveTab = 0; ///< 0 = Live RAM (Capstone), 1 = AstraC Annotated (.DSM)

    // AstraC Disassembler (.DSM) State
    std::vector<DsmModuleView> dsmModules;
    int selectedDsmModuleIndex = 0;
    std::string dsmLoadedPath = "";
    std::string dsmContent = "";
    std::vector<std::string> dsmLines;
    int dsmBits = 32;
    uint32_t dsmOrg = 0;
    char dsmSearchFilter[128] = "";
    bool dsmSyncWithEip = true;
    int dsmSelectedLine = -1;

    // Memory Hex Viewer state
    char memoryAddressInput[16] = "00000000";
    uint32_t memoryCurrentAddress = 0x00000000;
    int memoryRowsToDisplay = 16;

    // Console Prompt state
    char consoleInputBuffer[512] = "";
    std::vector<std::string> commandHistory;
    int historyIndex = -1;

    // Code Editor state
    std::vector<EditorTab> editorTabs;
    int activeEditorTabIndex = 0;
    int nextUntitledId = 1;
    std::string editorText;
    int editorLanguage = 0; ///< 0 = Assembly (.as), 1 = Astra C (.ac)
    int editorTargetFormat = 0; ///< 0 = Raw .bin, 1 = ACFH .exe, 2 = ACFH .lib
    bool acfhOptOffsetTable = true;
    bool acfhOptFuncTable = true;
    bool acfhOptImportTable = true;
    std::string editorFilePath = "";
    bool editorModified = false;

    // ACFH Module & Library Inspector state
    bool showAcfhInspector = true;
    char loadLibPathInput[512] = "";

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
     * @brief Disassembles a file via AstraC and loads its .DSM content into GuiState.
     */
    static bool LoadDsmForFile(GuiState& state, const std::string& filePath, const std::string& customAstraCPath = "");

    /**
     * @brief Refreshes disassembly listings for the main executable and all loaded dynamic libraries.
     * @param emu Emulator instance containing loaded modules and library references.
     * @param state GuiState reference where dsmModules will be updated.
     */
    static void RefreshDsmModules(Emulator& emu, GuiState& state);

    /**
     * @brief Selects a module and scrolls to a given exported or local function symbol.
     * @param state GuiState reference.
     * @param moduleName Name of the target module (or empty to search across all).
     * @param symbolName Function symbol to locate.
     * @return True if located and selected, false otherwise.
     */
    static bool JumpToDsmSymbol(GuiState& state, const std::string& moduleName, const std::string& symbolName);

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
     * @brief Creates a new editor tab.
     * @param state GuiState reference.
     * @param language 0 = Assembly (.as), 1 = Astra C (.ac).
     * @return Index of the newly created tab.
     */
    static int CreateNewEditorTab(GuiState& state, int language = 0);

    /**
     * @brief Closes an editor tab by index.
     * @param state GuiState reference.
     * @param tabIndex Index of the tab to close.
     */
    static void CloseEditorTab(GuiState& state, int tabIndex);

    /**
     * @brief Finds an already opened tab for filePath or opens it into a new tab.
     * @param state GuiState reference.
     * @param filePath File path on disk.
     * @param emu Optional Emulator pointer for logging.
     * @return Tab index if opened, -1 on failure.
     */
    static int FindOrOpenEditorTab(GuiState& state, const std::string& filePath, Emulator* emu = nullptr);

    /**
     * @brief Saves the specified editor tab to disk.
     * @param state GuiState reference.
     * @param tabIndex Index of the tab to save (-1 for active tab).
     * @param filePath Target file path (or empty to use tab.filePath).
     * @param emu Optional Emulator pointer for console logging.
     * @return True if saved successfully, false otherwise.
     */
    static bool SaveEditorTabToFile(GuiState& state, int tabIndex = -1, const std::string& filePath = "", Emulator* emu = nullptr);

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
    static void RenderBreakpointsAndWatchesWindow(Emulator& emu, GuiState& state);
    static void RenderMemoryWindow(Emulator& emu, GuiState& state);
    static void RenderMemoryContent(Emulator& emu, GuiState& state);
    static void RenderVgaWindow(Emulator& emu, GuiState& state, SDL_Renderer* renderer);
    static void RenderConsoleWindow(Emulator& emu, GuiState& state);
    static void RenderEditorWindow(Emulator& emu, GuiState& state);
    static void RenderAcfhInspectorWindow(Emulator& emu, GuiState& state);
    static void RenderAcfhInspectorContent(Emulator& emu, GuiState& state);
    static void RenderMemoryAndAcfhWindow(Emulator& emu, GuiState& state);

    // Modal Dialogs
    static void RenderSettingsModal(Emulator& emu, GuiState& state);
    static void RenderLoadOptionsModal(Emulator& emu, GuiState& state);
    static void RenderFileOpenModal(Emulator& emu, GuiState& state);
    static void RenderAboutModal(GuiState& state);
};

} // namespace em836
