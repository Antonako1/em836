/**
 * @file emulator.cpp
 * @brief Core i386 Emulator Engine wrapping Unicorn and Capstone.
 */

#include "em836/emulator.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace em836 {

Emulator::Emulator() {
    m_cachedState.status = EmulatorStatus::Stopped;
}

Emulator::~Emulator() {
    Shutdown();
}
#ifdef _WIN32
#include <psapi.h>
static const char *ProtToStr( DWORD prot )
{
    switch ( prot ) {
        case PAGE_NOACCESS: return "NOACCESS";
        case PAGE_READONLY: return "READONLY";
        case PAGE_READWRITE: return "READWRITE";
        case PAGE_WRITECOPY: return "WRITECOPY";
        case PAGE_EXECUTE: return "EXECUTE";
        case PAGE_EXECUTE_READ: return "EXECUTE_READ";
        case PAGE_EXECUTE_READWRITE: return "EXECUTE_READWRITE";
        case PAGE_EXECUTE_WRITECOPY: return "EXECUTE_WRITECOPY";
        default:
            if ( prot & PAGE_GUARD ) return "GUARD";
            return "UNKNOWN";
    }
}
static const char *StateToStr( DWORD state )
{
    switch ( state ) {
        case MEM_COMMIT: return "COMMIT";
        case MEM_RESERVE: return "RESERVE";
        case MEM_FREE: return "FREE";
        default: return "UNKNOWN";
    }
}
static void DumpMemoryInfo( void *addr )
{
    MEMORY_BASIC_INFORMATION mbi;
    if ( VirtualQuery( addr, &mbi, sizeof( mbi ) ) == sizeof( mbi ) ) {
        std::cout << "[meminfo] addr=" << addr
            << " Base=" << mbi.BaseAddress
            << " RegionSize=" << mbi.RegionSize
            << " State=" << StateToStr( mbi.State )
            << " Protect=" << ProtToStr( mbi.Protect )
            << " Type=" << mbi.Type << "\n";
    }
    else {
        std::cout << "[meminfo] VirtualQuery failed for " << addr << " err=" << GetLastError() << "\n";
    }
}
static void ListLoadedModules()
{
    HMODULE mods[1024];
    DWORD cbNeeded;
    if ( EnumProcessModules( GetCurrentProcess(), mods, sizeof( mods ), &cbNeeded ) ) {
        size_t count = cbNeeded / sizeof( HMODULE );
        for ( size_t i = 0; i < count; ++i ) {
            char path[MAX_PATH] = { 0 };
            if ( GetModuleFileNameA( mods[i], path, MAX_PATH ) ) {
                std::cout << "[mod] " << path << "\n";
            }
        }
    }
    else {
        std::cout << "[mod] EnumProcessModules failed err=" << GetLastError() << "\n";
    }
}
#endif

bool Emulator::Initialize(const EmulatorConfig& config) {
    Shutdown();

    m_config = config;
    m_ramSize = config.memorySize;
    m_clockSpeedHz = config.clockSpeedHz;
    m_cpuBits = config.cpuModeBits;

    // Allocate continuous physical RAM buffer
    #ifdef _WIN32
    // Allocate continuous physical RAM buffer (page-aligned, committed)
        SYSTEM_INFO si;
        GetSystemInfo( &si );
        size_t pageSize = static_cast<size_t>(si.dwPageSize);
        size_t allocSize = ((m_ramSize + pageSize - 1) / pageSize) * pageSize;
        m_ram = static_cast<uint8_t *>(VirtualAlloc( nullptr, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE ));
        if ( !m_ram ) {
            m_cachedState.lastErrorMessage = "Failed to allocate physical RAM block via VirtualAlloc (" + std::to_string( m_ramSize ) + " bytes).";
            m_cachedState.status = EmulatorStatus::Error;
            return false;
        }
        // Zero memory explicitly
        std::memset( m_ram, 0, m_ramSize );
        // Note: store allocSize if you need to free exact size later (optional)
    #else
        m_ram = new (std::nothrow) uint8_t[m_ramSize];
        if ( !m_ram ) {
            m_cachedState.lastErrorMessage = "Failed to allocate physical RAM block (" + std::to_string( m_ramSize ) + " bytes).";
            m_cachedState.status = EmulatorStatus::Error;
            return false;
        }
        std::memset( m_ram, 0, m_ramSize );
    #endif

    // Initialize Capstone disassembler
    cs_mode csMode = (m_cpuBits == 16) ? CS_MODE_16 : CS_MODE_32;
    if (cs_open(CS_ARCH_X86, csMode, &m_capstone) != CS_ERR_OK) {
        m_cachedState.lastErrorMessage = "Failed to initialize Capstone disassembler engine.";
        m_cachedState.status = EmulatorStatus::Error;
        return false;
    }
    cs_option(m_capstone, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);

    // Initialize Unicorn CPU engine
    if (!ReinitUnicorn(m_cpuBits)) {
        return false;
    }

    // Set initial display mode
    m_vga.SetMode(config.defaultVideoMode);
    m_vga.ClearScreen(m_ram, m_ramSize, 0);

    // Set initial registers
    Reset();

    // Start background execution worker thread
    m_workerShouldRun = true;
    m_workerThread = std::thread(&Emulator::WorkerThreadLoop, this);

    m_status = EmulatorStatus::Paused;
    m_cachedState.status = EmulatorStatus::Paused;

    return true;
}

void Emulator::Shutdown() {
    // Terminate worker thread
    m_workerShouldRun = false;
    m_workerActive = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Close Unicorn engine
    if (m_uc) {
        uc_close(m_uc);
        m_uc = nullptr;
    }

    // Close Capstone
    if (m_capstone) {
        cs_close(&m_capstone);
        m_capstone = 0;
    }

    // Free guest RAM
    #ifdef _WIN32
        if ( m_ram ) {
            VirtualFree( m_ram, 0, MEM_RELEASE );
            m_ram = nullptr;
        }
    #else
        if ( m_ram ) {
            delete[] m_ram;
            m_ram = nullptr;
        }
    #endif

    m_vga.DestroyTexture();
    m_syscalls.Reset();
    m_status = EmulatorStatus::Stopped;
    m_cachedState.status = EmulatorStatus::Stopped;
}

bool Emulator::ReinitUnicorn(uint32_t bits) {
    if (m_uc) {
        uc_close(m_uc);
        m_uc = nullptr;
    }
    int test = sizeof( void * );
    uc_mode ucMode = (bits == 16) ? UC_MODE_16 : UC_MODE_32;
    uc_err err = uc_open(UC_ARCH_X86, ucMode, &m_uc);
    if (err != UC_ERR_OK) {
        m_cachedState.lastErrorMessage = "Failed to initialize Unicorn engine: " + std::string(uc_strerror(err));
        m_cachedState.status = EmulatorStatus::Error;
        return false;
    }
    assert( m_ramSize % 4096 == 0 );
    // Map physical RAM pointer into Unicorn address space starting at 0x00000000


    // Insert inside ReinitUnicorn() after uc_open(...) and before uc_mem_map_ptr(...)
#ifdef _WIN32
    // Diagnostic: print pointer size and Unicorn mode constants
    std::cout << "[diag] sizeof(void*)=" << sizeof( void * ) << "\n";
    std::cout << "[diag] UC_MODE_16=" << static_cast<uint64_t>(UC_MODE_16)
        << " UC_MODE_32=" << static_cast<uint64_t>(UC_MODE_32)
        << " computed ucMode=" << static_cast<uint64_t>(ucMode) << "\n";
    std::cout << "[diag] m_ram=" << reinterpret_cast<void *>(m_ram)
        << " m_ramSize=" << m_ramSize << "\n";

// VirtualQuery the m_ram region to confirm COMMIT/PROTECT
    MEMORY_BASIC_INFORMATION mbi;
    if ( VirtualQuery( m_ram, &mbi, sizeof( mbi ) ) == sizeof( mbi ) ) {
        std::cout << "[diag] m_ram State=" << mbi.State << " Protect=" << mbi.Protect
            << " RegionSize=" << mbi.RegionSize << " BaseAddress=" << mbi.BaseAddress << "\n";
    }
    else {
        std::cout << "[diag] VirtualQuery failed on m_ram\n";
    }

    // Attempt to locate the unicorn DLL module handle (common names)
    HMODULE hMod = GetModuleHandleA( "unicorn.dll" );
    if ( !hMod ) hMod = GetModuleHandleA( "libunicorn.dll" );
    if ( hMod ) {
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA( hMod, path, MAX_PATH );
        std::cout << "[diag] unicorn module loaded from: " << path << "\n";
    }
    else {
        std::cout << "[diag] unicorn module not found via GetModuleHandleA(\"unicorn.dll\")\n";
    }
#endif

    DumpMemoryInfo( m_ram ); // you already confirmed this is OK
    DumpMemoryInfo( reinterpret_cast<void *>(m_uc) ); // replace with the code_buffer address you saw
    ListLoadedModules(); // to find the exact unicorn module file name/path

    err = uc_mem_map_ptr(m_uc, 0x00000000, m_ramSize, UC_PROT_ALL, m_ram);
    if (err != UC_ERR_OK) {
        m_cachedState.lastErrorMessage = "Failed to map guest RAM into Unicorn: " + std::string(uc_strerror(err));
        m_cachedState.status = EmulatorStatus::Error;
        return false;
    }

    // Register execution hook for breakpoints and single-step observation
    uc_hook_add(m_uc, &m_codeHook, UC_HOOK_CODE, reinterpret_cast<void*>(HookCodeCallback), this, 1, 0);

    // Register software interrupt hook for custom INT 0x88 and legacy BIOS/DOS/Linux calls
    uc_hook_add(m_uc, &m_intrHook, UC_HOOK_INTR, reinterpret_cast<void*>(HookIntrCallback), this, 1, 0);

    // Register port I/O hooks for COM1 serial port and Bochs debug port
    uc_hook_add(m_uc, &m_portInHook, UC_HOOK_INSN, reinterpret_cast<void*>(HookPortInCallback), this, 1, 0, UC_X86_INS_IN);
    uc_hook_add(m_uc, &m_portOutHook, UC_HOOK_INSN, reinterpret_cast<void*>(HookPortOutCallback), this, 1, 0, UC_X86_INS_OUT);

    return true;
}

void Emulator::Reset() {
    Pause();

    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_uc) return;

    // Zero out general purpose registers
    uint32_t zero = 0;
    uc_reg_write(m_uc, UC_X86_REG_EAX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_EBX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_ECX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_EDX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_ESI, &zero);
    uc_reg_write(m_uc, UC_X86_REG_EDI, &zero);

    // Set initial EIP and ESP according to config
    uint32_t eip = m_config.defaultEntryAddress;
    uint32_t esp = m_config.defaultStackAddress;
    uint32_t eflags = 0x00000002;

    uc_reg_write(m_uc, UC_X86_REG_EIP, &eip);
    uc_reg_write(m_uc, UC_X86_REG_ESP, &esp);
    uc_reg_write(m_uc, UC_X86_REG_EBP, &esp);
    uc_reg_write(m_uc, UC_X86_REG_EFLAGS, &eflags);

    // Set segment registers
    uint16_t segZero = 0;
    uc_reg_write(m_uc, UC_X86_REG_CS, &segZero);
    uc_reg_write(m_uc, UC_X86_REG_DS, &segZero);
    uc_reg_write(m_uc, UC_X86_REG_ES, &segZero);
    uc_reg_write(m_uc, UC_X86_REG_FS, &segZero);
    uc_reg_write(m_uc, UC_X86_REG_GS, &segZero);
    uc_reg_write(m_uc, UC_X86_REG_SS, &segZero);

    m_cachedState.instructionCount = 0;
    m_syscalls.Reset();
    RefreshStateCache();
}

void Emulator::Start() {
    if (m_status == EmulatorStatus::Stopped || m_status == EmulatorStatus::Error) return;
    m_workerActive = true;
    m_status = EmulatorStatus::Running;
}

void Emulator::Pause() {
    if (m_status == EmulatorStatus::Running || m_status == EmulatorStatus::Stepping) {
        m_workerActive = false;
        if (m_uc) {
            uc_emu_stop(m_uc);
        }
        m_status = EmulatorStatus::Paused;
    }
}

void Emulator::Step() {
    if (m_status == EmulatorStatus::Stopped || m_status == EmulatorStatus::Error) return;
    m_stepRequested = true;
    m_workerActive = true;
    m_status = EmulatorStatus::Stepping;
}

void Emulator::SetClockSpeed(uint32_t hz) {
    m_clockSpeedHz = hz;
    m_config.clockSpeedHz = hz;
}

uint32_t Emulator::GetClockSpeed() const {
    return m_clockSpeedHz;
}

void Emulator::SetCpuMode(uint32_t bits) {
    if (bits != 16 && bits != 32) return;
    Pause();
    m_cpuBits = bits;
    m_config.cpuModeBits = bits;

    // Update Capstone
    if (m_capstone) {
        cs_close(&m_capstone);
    }
    cs_mode csMode = (m_cpuBits == 16) ? CS_MODE_16 : CS_MODE_32;
    cs_open(CS_ARCH_X86, csMode, &m_capstone);
    cs_option(m_capstone, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);

    ReinitUnicorn(bits);
    Reset();
}

uint32_t Emulator::GetCpuMode() const {
    return m_cpuBits;
}

void Emulator::ToggleBreakpoint(uint32_t address) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_breakpoints.count(address)) {
        m_breakpoints.erase(address);
    } else {
        m_breakpoints.insert(address);
    }
}

void Emulator::AddBreakpoint(uint32_t address) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_breakpoints.insert(address);
}

void Emulator::RemoveBreakpoint(uint32_t address) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_breakpoints.erase(address);
}

void Emulator::ClearBreakpoints() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_breakpoints.clear();
}

bool Emulator::HasBreakpoint(uint32_t address) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_breakpoints.count(address) > 0;
}

bool Emulator::LoadBinary(const uint8_t* data, size_t size, uint32_t loadAddress, uint32_t entryPoint) {
    if (!data || size == 0 || !m_ram) return false;

    Pause();

    if (loadAddress + size > m_ramSize) {
        m_cachedState.lastErrorMessage = "Binary size exceeds available guest physical RAM.";
        return false;
    }

    // Write binary bytes directly into guest RAM
    std::memcpy(m_ram + loadAddress, data, size);

    // Configure EIP
    WriteRegister(UC_X86_REG_EIP, entryPoint);
    RefreshStateCache();
    return true;
}

bool Emulator::LoadBinaryFile(const std::string& filePath, uint32_t loadAddress, uint32_t entryPoint) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return LoadBinary(buffer.data(), buffer.size(), loadAddress, entryPoint);
}

bool Emulator::ReadMemory(uint32_t address, void* buffer, size_t size) const {
    if (!buffer || address + size > m_ramSize) return false;
    std::memcpy(buffer, m_ram + address, size);
    return true;
}

bool Emulator::WriteMemory(uint32_t address, const void* buffer, size_t size) {
    if (!buffer || address + size > m_ramSize) return false;
    std::memcpy(m_ram + address, buffer, size);
    return true;
}

uint32_t Emulator::ReadRegister(int ucRegId) {
    uint32_t val = 0;
    if (m_uc) {
        uc_reg_read(m_uc, ucRegId, &val);
    }
    return val;
}

void Emulator::WriteRegister(int ucRegId, uint32_t value) {
    if (m_uc) {
        uc_reg_write(m_uc, ucRegId, &value);
    }
}

CpuState Emulator::GetCpuStateSnapshot() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_cachedState;
}

void Emulator::ApplyCpuState(const CpuState& state) {
    Pause();
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_uc) return;

    uc_reg_write(m_uc, UC_X86_REG_EAX, &state.eax);
    uc_reg_write(m_uc, UC_X86_REG_EBX, &state.ebx);
    uc_reg_write(m_uc, UC_X86_REG_ECX, &state.ecx);
    uc_reg_write(m_uc, UC_X86_REG_EDX, &state.edx);
    uc_reg_write(m_uc, UC_X86_REG_ESI, &state.esi);
    uc_reg_write(m_uc, UC_X86_REG_EDI, &state.edi);
    uc_reg_write(m_uc, UC_X86_REG_ESP, &state.esp);
    uc_reg_write(m_uc, UC_X86_REG_EBP, &state.ebp);
    uc_reg_write(m_uc, UC_X86_REG_EIP, &state.eip);
    uc_reg_write(m_uc, UC_X86_REG_EFLAGS, &state.eflags);

    RefreshStateCache();
}

void Emulator::RefreshStateCache() {
    if (!m_uc) return;

    uc_reg_read(m_uc, UC_X86_REG_EAX, &m_cachedState.eax);
    uc_reg_read(m_uc, UC_X86_REG_EBX, &m_cachedState.ebx);
    uc_reg_read(m_uc, UC_X86_REG_ECX, &m_cachedState.ecx);
    uc_reg_read(m_uc, UC_X86_REG_EDX, &m_cachedState.edx);
    uc_reg_read(m_uc, UC_X86_REG_ESI, &m_cachedState.esi);
    uc_reg_read(m_uc, UC_X86_REG_EDI, &m_cachedState.edi);
    uc_reg_read(m_uc, UC_X86_REG_ESP, &m_cachedState.esp);
    uc_reg_read(m_uc, UC_X86_REG_EBP, &m_cachedState.ebp);
    uc_reg_read(m_uc, UC_X86_REG_EIP, &m_cachedState.eip);
    uc_reg_read(m_uc, UC_X86_REG_EFLAGS, &m_cachedState.eflags);

    uc_reg_read(m_uc, UC_X86_REG_CS, &m_cachedState.cs);
    uc_reg_read(m_uc, UC_X86_REG_DS, &m_cachedState.ds);
    uc_reg_read(m_uc, UC_X86_REG_ES, &m_cachedState.es);
    uc_reg_read(m_uc, UC_X86_REG_FS, &m_cachedState.fs);
    uc_reg_read(m_uc, UC_X86_REG_GS, &m_cachedState.gs);
    uc_reg_read(m_uc, UC_X86_REG_SS, &m_cachedState.ss);

    m_cachedState.status = m_status;
}

std::vector<DisassemblyItem> Emulator::DisassembleRange(uint32_t startAddress, size_t count) {
    std::vector<DisassemblyItem> items;
    if (!m_capstone || !m_ram || startAddress >= m_ramSize) {
        return items;
    }

    size_t availableBytes = std::min(m_ramSize - startAddress, count * 15);
    const uint8_t* codePtr = m_ram + startAddress;

    cs_insn* insn = nullptr;
    size_t decodedCount = cs_disasm(m_capstone, codePtr, availableBytes, startAddress, count, &insn);

    if (decodedCount > 0 && insn) {
        uint32_t currentEip = m_cachedState.eip;
        for (size_t i = 0; i < decodedCount; ++i) {
            DisassemblyItem item;
            item.address = static_cast<uint32_t>(insn[i].address);
            item.mnemonic = insn[i].mnemonic;
            item.opStr = insn[i].op_str;
            item.bytes.assign(insn[i].bytes, insn[i].bytes + insn[i].size);
            item.isCurrentEip = (item.address == currentEip);
            item.hasBreakpoint = HasBreakpoint(item.address);
            items.push_back(item);
        }
        cs_free(insn, decodedCount);
    }

    return items;
}

void Emulator::WorkerThreadLoop() {
    while (m_workerShouldRun) {
        if (!m_workerActive) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        uint32_t currentEip = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (!m_uc) {
                m_workerActive = false;
                continue;
            }
            uc_reg_read(m_uc, UC_X86_REG_EIP, &currentEip);
        }

        // Determine instructions to execute in this time quantum
        size_t count = 1;
        bool isStepping = m_stepRequested.exchange(false);
        uint32_t speed = m_clockSpeedHz.load();

        if (isStepping) {
            count = 1;
        } else if (speed == 0) {
            // Unlimited speed: execute chunk of 25,000 instructions per quantum
            count = 25000;
        } else if (speed > 1000) {
            count = speed / 100; // Slice at ~100 Hz refresh
        } else {
            count = 1;
        }

        uc_err err = uc_emu_start(m_uc, currentEip, m_ramSize, 0, count);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            RefreshStateCache();
        }

        if (isStepping) {
            m_workerActive = false;
            m_status = EmulatorStatus::Paused;
            m_cachedState.status = EmulatorStatus::Paused;
        } else if (err != UC_ERR_OK) {
            m_workerActive = false;
            m_status = EmulatorStatus::Paused;
            m_cachedState.status = EmulatorStatus::Paused;
            if (err != UC_ERR_OK && err != UC_ERR_INSN_INVALID) {
                m_console.WriteText("[VM] Execution stopped: " + std::string(uc_strerror(err)) + "\n");
            }
        }

        // Throttling delay calculation
        if (speed > 0 && !isStepping) {
            int64_t sleepMicros = (1000000LL * count) / speed;
            if (sleepMicros > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleepMicros));
            }
        } else if (speed == 0) {
            // Give SDL3 main thread a brief 1ms window for smooth rendering
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// -----------------------------------------------------------------------------
// Unicorn Hooks Callbacks
// -----------------------------------------------------------------------------

void Emulator::HookCodeCallback(uc_engine* uc, uint64_t address, uint32_t size, void* user_data) {
    Emulator* self = static_cast<Emulator*>(user_data);
    uint32_t addr = static_cast<uint32_t>(address);

    self->m_cachedState.instructionCount++;

    // Check if breakpoint is hit
    if (self->HasBreakpoint(addr)) {
        uc_emu_stop(uc);
        self->m_workerActive = false;
        self->m_status = EmulatorStatus::Paused;
        self->m_cachedState.status = EmulatorStatus::Paused;
        self->m_console.WriteText("[VM] Breakpoint hit at 0x" + std::to_string(addr) + "\n");
    }
}

void Emulator::HookIntrCallback(uc_engine* uc, uint32_t intno, void* user_data) {
    Emulator* self = static_cast<Emulator*>(user_data);
    self->m_syscalls.HandleInterrupt(*self, intno);
}

uint32_t Emulator::HookPortInCallback(uc_engine* uc, uint32_t port, int size, void* user_data) {
    Emulator* self = static_cast<Emulator*>(user_data);
    uint32_t val = 0;
    self->m_syscalls.HandlePortIO(*self, port, static_cast<uint32_t>(size), &val, false);
    return val;
}

void Emulator::HookPortOutCallback(uc_engine* uc, uint32_t port, int size, uint32_t value, void* user_data) {
    Emulator* self = static_cast<Emulator*>(user_data);
    self->m_syscalls.HandlePortIO(*self, port, static_cast<uint32_t>(size), &value, true);
}

} // namespace em836
