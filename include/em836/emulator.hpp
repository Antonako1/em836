/**
 * @file emulator.hpp
 * @brief Core i386 Emulator Engine wrapping Unicorn, Capstone, and Subsystems.
 *
 * This header defines the central virtual machine orchestrator of em836.
 * It coordinates:
 * 1. CPU Core Emulation via Unicorn Engine:
 *    - Supports 32-bit Protected Mode (UC_MODE_32) and 16-bit Real Mode (UC_MODE_16).
 *    - Maps 16 MB guest physical RAM with configurable sizing.
 *    - Installs code hooks (breakpoints), interrupt hooks (custom INT 0x88 and legacy),
 *      and port I/O hooks.
 * 2. Background Worker Execution Loop:
 *    - Runs emulation on a dedicated background thread in small cycle/time quanta.
 *    - Honors clock speed throttling (from 10 Hz up to Unlimited).
 *    - Provides atomic pause/run/step signaling so the SDL3/ImGui UI maintains 60 FPS.
 * 3. Real-time Disassembly via Capstone Engine:
 *    - Decodes x86 instructions on-the-fly around EIP with opcode byte listings.
 * 4. Subsystem Coordination:
 *    - Integrates VgaSubsystem (Text 80x25 and Graphics 640x480), ConsoleSubsystem,
 *      and SyscallDispatcher.
 */

#pragma once

#include "config.hpp"
#include "vga.hpp"
#include "console.hpp"
#include "syscalls.hpp"
#include "astrac.hpp"

#include <unicorn/unicorn.h>
#include <capstone/capstone.h>

#include <string>
#include <vector>
#include <optional>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace em836 {

constexpr uint32_t kLibraryArenaBase  = 0x00100000; ///< 1 MB base address for dynamic library arena
constexpr uint32_t kLibraryArenaLimit = 0x001FFFFF; ///< 2 MB limit for dynamic library arena

/**
 * @enum EmulatorStatus
 * @brief High-level lifecycle status of the virtual machine.
 */
enum class EmulatorStatus {
    Stopped,    ///< VM is uninitialized or shut down.
    Paused,     ///< VM is loaded and ready, but execution is halted.
    Running,    ///< VM is actively executing instructions in the worker thread.
    Stepping,   ///< VM is executing exactly one instruction before returning to Paused.
    Halted,     ///< Guest software invoked SYS_EXIT or executed HLT.
    Error       ///< Fatal CPU or memory emulation fault encountered.
};

/**
 * @struct CpuState
 * @brief Complete snapshot of x86 CPU registers and execution metrics.
 */
struct CpuState {
    // General Purpose Registers (32-bit)
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    uint32_t esi = 0;
    uint32_t edi = 0;
    uint32_t esp = 0x00800000; ///< Stack Top (8 MB default)
    uint32_t ebp = 0x00800000;

    // Instruction Pointer & Flags
    uint32_t eip = 0x00000000; ///< Entry address
    uint32_t eflags = 0x00000002; ///< Standard reset value (Bit 1 is always 1)

    // Segment Registers
    uint16_t cs = 0;
    uint16_t ds = 0;
    uint16_t es = 0;
    uint16_t fs = 0;
    uint16_t gs = 0;
    uint16_t ss = 0;

    // Metrics & Status
    uint64_t instructionCount = 0;
    EmulatorStatus status = EmulatorStatus::Stopped;
    std::string lastErrorMessage = "";

    // EFLAGS Bit Helpers
    bool FlagCF() const { return (eflags & (1 << 0)) != 0; }
    bool FlagPF() const { return (eflags & (1 << 2)) != 0; }
    bool FlagAF() const { return (eflags & (1 << 4)) != 0; }
    bool FlagZF() const { return (eflags & (1 << 6)) != 0; }
    bool FlagSF() const { return (eflags & (1 << 7)) != 0; }
    bool FlagTF() const { return (eflags & (1 << 8)) != 0; }
    bool FlagIF() const { return (eflags & (1 << 9)) != 0; }
    bool FlagDF() const { return (eflags & (1 << 10)) != 0; }
    bool FlagOF() const { return (eflags & (1 << 11)) != 0; }
};

/**
 * @struct DisassemblyItem
 * @brief Representation of a single disassembled x86 instruction for the UI.
 */
struct DisassemblyItem {
    uint32_t address = 0;
    std::string mnemonic = "";
    std::string opStr = "";
    std::vector<uint8_t> bytes;
    bool isCurrentEip = false;
    bool hasBreakpoint = false;
};

/**
 * @struct BreakpointItem
 * @brief Representation of an execution breakpoint.
 */
struct BreakpointItem {
    uint32_t address = 0;
    bool enabled = true;
    uint64_t hitCount = 0;
    std::string moduleName = "";
    std::string symbol = "";
};

/**
 * @enum WatchCondition
 * @brief Trigger condition for memory watchpoints.
 */
enum class WatchCondition {
    OnChange = 0,   ///< Triggers when value at address changes
    OnWrite = 1,    ///< Triggers on any write to the memory address
    OnEqual = 2     ///< Triggers when value at address equals targetValue
};

/**
 * @struct WatchpointItem
 * @brief Memory watchpoint record tracking address, size, trigger condition, and values.
 */
struct WatchpointItem {
    uint32_t id = 0;
    uint32_t address = 0;
    uint32_t size = 4;                      ///< 1, 2, or 4 bytes
    WatchCondition condition = WatchCondition::OnChange;
    uint32_t targetValue = 0;               ///< For OnEqual condition
    uint32_t currentValue = 0;              ///< Last observed value in guest RAM
    uint32_t previousValue = 0;             ///< Value before last change
    uint64_t hitCount = 0;
    bool enabled = true;
    std::string name = "";                  ///< Optional variable name or label
};

/**
 * @class Emulator
 * @brief Central controller for i386 CPU emulation, hardware state, and background threading.
 */
class Emulator {
public:
    Emulator();
    ~Emulator();

    // Prevent copy/move
    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;

    /**
     * @brief Initializes the Unicorn CPU, Capstone disassembler, and allocates guest RAM.
     * @param config Emulator configuration parameters.
     * @return True if initialized successfully, false on error.
     */
    bool Initialize(const EmulatorConfig& config);

    /**
     * @brief Shuts down execution worker thread and cleans up Unicorn/Capstone resources.
     */
    void Shutdown();

    /**
     * @brief Resets CPU registers, memory content, and instruction counter to default state.
     */
    void Reset();

    // -------------------------------------------------------------
    // Execution Controls
    // -------------------------------------------------------------

    /**
     * @brief Begins or resumes continuous asynchronous CPU execution.
     */
    void Start();

    /**
     * @brief Pauses ongoing CPU execution.
     */
    void Pause();

    /**
     * @brief Halts CPU execution when guest invokes SYS_EXIT, executes HLT, or reaches end of code.
     */
    void Halt();

    /**
     * @brief Executes exactly one instruction and pauses.
     */
    void Step();

    /**
     * @brief Executes exactly one instruction, entering function calls (Step Into).
     */
    void StepInto();

    /**
     * @brief Executes past function calls without entering them (Step Over).
     * If the current instruction is not a CALL, behaves like Step Into.
     */
    void StepOver();

    /**
     * @brief Continues execution until the current subroutine returns (Step Out).
     * Stops at the return address in the caller.
     */
    void StepOut();

    bool IsStepping() const { return m_status == EmulatorStatus::Stepping || m_stepOverActive || m_stepOutActive; }
    bool IsStepOverActive() const { return m_stepOverActive; }
    bool IsStepOutActive() const { return m_stepOutActive; }

    /**
     * @brief Sets execution clock speed throttling in Hertz (0 = Unlimited).
     */
    void SetClockSpeed(uint32_t hz);

    /**
     * @brief Gets current execution clock speed throttling in Hertz.
     */
    uint32_t GetClockSpeed() const;

    /**
     * @brief Switches CPU architecture mode (32-bit Protected Mode vs 16-bit Real Mode).
     */
    void SetCpuMode(uint32_t bits);
    uint32_t GetCpuMode() const;

    // -------------------------------------------------------------
    // Breakpoints
    // -------------------------------------------------------------

    void ToggleBreakpoint(uint32_t address);
    void AddBreakpoint(uint32_t address, const std::string& symbol = "");
    void SetBreakpointEnabled(uint32_t address, bool enabled);
    void RemoveBreakpoint(uint32_t address);
    void ClearBreakpoints();
    bool HasBreakpoint(uint32_t address) const;
    std::vector<BreakpointItem> GetBreakpointsList() const;

    // -------------------------------------------------------------
    // Watchpoints (Data Breakpoints)
    // -------------------------------------------------------------

    uint32_t AddWatchpoint(uint32_t address, uint32_t size = 4, WatchCondition cond = WatchCondition::OnChange, uint32_t targetVal = 0, const std::string& name = "");
    void SetWatchpointEnabled(uint32_t id, bool enabled);
    void RemoveWatchpoint(uint32_t id);
    void ClearWatchpoints();
    std::vector<WatchpointItem> GetWatchpointsList() const;

    // -------------------------------------------------------------
    // Binary & Memory Loading
    // -------------------------------------------------------------

    /**
     * @brief Copies raw binary payload into guest RAM and configures initial EIP.
     * @param data Pointer to binary bytes.
     * @param size Size in bytes.
     * @param loadAddress Address in guest RAM to place binary (e.g. 0x00000000).
     * @param entryPoint Initial EIP value.
     */
    bool LoadBinary(const uint8_t* data, size_t size, uint32_t loadAddress, uint32_t entryPoint);

    /**
     * @brief Reads a binary file from disk and loads it into guest RAM.
     */
    bool LoadBinaryFile(const std::string& filePath, uint32_t loadAddress, uint32_t entryPoint);

    // -------------------------------------------------------------
    // Memory and Register Access
    // -------------------------------------------------------------

    bool ReadMemory(uint32_t address, void* buffer, size_t size) const;
    bool WriteMemory(uint32_t address, const void* buffer, size_t size);

    uint32_t ReadRegister(int ucRegId);
    void WriteRegister(int ucRegId, uint32_t value);

    /**
     * @brief Returns a thread-safe snapshot of the current CPU state.
     */
    CpuState GetCpuStateSnapshot();

    /**
     * @brief Sets user overrides on CPU registers (e.g. from UI inspector).
     */
    void ApplyCpuState(const CpuState& state);

    /**
     * @brief Disassembles instructions starting at a given address.
     * @param startAddress Starting physical address.
     * @param count Number of instructions to decode.
     * @return Vector of DisassemblyItem records.
     */
    std::vector<DisassemblyItem> DisassembleRange(uint32_t startAddress, size_t count);

    // -------------------------------------------------------------
    // Subsystem Accessors
    // -------------------------------------------------------------
    VgaSubsystem& GetVga() { return m_vga; }
    ConsoleSubsystem& GetConsole() { return m_console; }
    SyscallDispatcher& GetSyscalls() { return m_syscalls; }
    const EmulatorConfig& GetConfig() const { return m_config; }
    EmulatorConfig& GetConfig() { return m_config; }

    uint8_t* GetRamBuffer() { return m_ram; }
    size_t GetRamSize() const { return m_ramSize; }
    uc_engine* GetUnicornEngine() { return m_uc; }

    uint32_t GetCodeBaseAddress() const { return m_codeBaseAddress; }
    uint32_t GetCodeSize() const { return m_codeSize; }

    // -------------------------------------------------------------
    // ACFH Executable & Dynamic Library Support
    // -------------------------------------------------------------
    /**
     * @brief Loads an ACFH executable into guest RAM at origin 0x00000000,
     * and automatically resolves and links imported libraries.
     */
    bool LoadAcfhExecutable(const std::string& filePath, const AcfhParsedModule& module);

    /**
     * @brief Loads and relocates an ACFH dynamic library into the library memory arena (0x00100000+).
     */
    bool LoadAcfhLibrary(const std::string& filePath, const AcfhParsedModule& module);

    /**
     * @brief Reads an ACFH .lib file from disk, parses it, and loads it into the library arena.
     */
    bool LoadAcfhLibraryFile(const std::string& filePath);

    /**
     * @brief Unloads an active dynamic library by name and updates import resolution.
     */
    bool UnloadAcfhLibrary(const std::string& libName);

    /**
     * @brief Unloads all dynamic libraries currently in memory.
     */
    void ClearAcfhLibraries();

    /**
     * @brief Resolves and patches import table call/jump references across modules.
     */
    bool RelinkImports();

    /**
     * @brief Retrieves the active main ACFH executable metadata, if loaded.
     */
    const std::optional<AcfhParsedModule>& GetMainModule() const { return m_mainModule; }

    /**
     * @brief Retrieves list of currently loaded ACFH dynamic libraries.
     */
    const std::vector<AcfhParsedModule>& GetLoadedLibraries() const { return m_loadedLibraries; }

    /**
     * @brief Checks if a physical memory address falls within valid code regions of
     * the main program or any loaded dynamic library.
     */
    bool IsAddressInExecutableCode(uint32_t addr) const;

private:
    EmulatorConfig m_config;
    EmulatorStatus m_status = EmulatorStatus::Stopped;

    // Loaded Binary Cache for Resets and Boundary Protection
    std::vector<uint8_t> m_loadedBinary;
    uint32_t m_loadedLoadAddress = 0;
    uint32_t m_loadedEntryPoint = 0;
    uint32_t m_codeBaseAddress = 0;
    uint32_t m_codeSize = 0;

    // ACFH Executable & Dynamic Library Storage
    std::optional<AcfhParsedModule> m_mainModule;
    std::vector<AcfhParsedModule> m_loadedLibraries;

    void ApplyModuleRelocations(AcfhParsedModule& mod);
    void RemapLoadedLibraries();

    // Unicorn Engine
    uc_engine* m_uc = nullptr;
    uc_hook m_codeHook = 0;
    uc_hook m_intrHook = 0;
    uc_hook m_portInHook = 0;
    uc_hook m_portOutHook = 0;
    uc_hook m_memWriteHook = 0;

    // Capstone Engine
    csh m_capstone = 0;

    // Guest Physical RAM
    uint8_t* m_ram = nullptr;
	size_t m_ramSize = 16 * 1024 * 1024; // Note: Must be 4kb aligned for Unicorn mapping

    // Execution Threading
    std::thread m_workerThread;
    std::atomic<bool> m_workerShouldRun{false};
    std::atomic<bool> m_workerActive{false};
    std::atomic<bool> m_workerRunningQuantum{false};
    std::atomic<bool> m_stepRequested{false};
    std::atomic<bool> m_stepOverActive{false};
    std::atomic<uint32_t> m_stepOverTarget{0};
    std::atomic<uint32_t> m_stepOverInitialEsp{0};
    std::atomic<bool> m_stepOutActive{false};
    std::atomic<bool> m_stepOutHitRet{false};
    std::atomic<uint32_t> m_stepOutInitialEsp{0};
    std::atomic<uint32_t> m_clockSpeedHz{0};
    std::atomic<uint32_t> m_cpuBits{32};

    mutable std::mutex m_stateMutex;
    CpuState m_cachedState;
    std::unordered_map<uint32_t, BreakpointItem> m_breakpoints;
    std::vector<WatchpointItem> m_watchpoints;
    uint32_t m_nextWatchpointId = 1;

    // Subsystems
    VgaSubsystem m_vga;
    ConsoleSubsystem m_console;
    SyscallDispatcher m_syscalls;

    // Internal execution helpers
    void WorkerThreadLoop();
    void RefreshStateCache();
    bool ReinitUnicorn(uint32_t bits);

    // Static Unicorn Callbacks
    static void HookCodeCallback(uc_engine* uc, uint64_t address, uint32_t size, void* user_data);
    static void HookIntrCallback(uc_engine* uc, uint32_t intno, void* user_data);
    static uint32_t HookPortInCallback(uc_engine* uc, uint32_t port, int size, void* user_data);
    static void HookPortOutCallback(uc_engine* uc, uint32_t port, int size, uint32_t value, void* user_data);
    static void HookMemWriteCallback(uc_engine* uc, uc_mem_type type, uint64_t address, int size, int64_t value, void* user_data);
};

} // namespace em836
