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

    // Invalidate Unicorn Translation Block (TB) and TLB caches so new code is cleanly translated
    uc_ctl_flush_tb(m_uc);
    uc_ctl_flush_tlb(m_uc);

    // Reset status to Paused
    m_status = EmulatorStatus::Paused;
    m_cachedState.status = EmulatorStatus::Paused;
    m_cachedState.lastErrorMessage.clear();

    // Zero out physical RAM
    if (m_ram) {
        std::memset(m_ram, 0, m_ramSize);
    }

    // Determine initial entry address
    uint32_t eip = m_config.defaultEntryAddress;

    // Restore cached binary if loaded
    if (!m_loadedBinary.empty() && m_ram) {
        if (m_loadedLoadAddress + m_loadedBinary.size() <= m_ramSize) {
            std::memcpy(m_ram + m_loadedLoadAddress, m_loadedBinary.data(), m_loadedBinary.size());
            eip = m_loadedEntryPoint;
        }
    }

    // Restore and relocate dynamic libraries in 0x00100000+ arena
    RemapLoadedLibraries();

    // Relink imports if ACFH main executable is present
    if (m_mainModule.has_value()) {
        RelinkImports();
    }

    // Flush TB and TLB again after RAM update
    uc_ctl_flush_tb(m_uc);
    uc_ctl_flush_tlb(m_uc);

    // Zero out general purpose registers
    uint32_t zero = 0;
    uc_reg_write(m_uc, UC_X86_REG_EAX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_EBX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_ECX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_EDX, &zero);
    uc_reg_write(m_uc, UC_X86_REG_ESI, &zero);
    uc_reg_write(m_uc, UC_X86_REG_EDI, &zero);

    // Set initial EIP and ESP according to config (use 0xFFFC in 16-bit Real Mode so SP != 0)
    uint32_t esp = (m_cpuBits == 16) ? 0x0000FFFC : m_config.defaultStackAddress;
    uint32_t eflags = 0x00000002;

    uc_reg_write(m_uc, UC_X86_REG_EIP, &eip);
    uc_reg_write(m_uc, UC_X86_REG_ESP, &esp);
    uc_reg_write(m_uc, UC_X86_REG_EBP, &esp);
    uc_reg_write(m_uc, UC_X86_REG_EFLAGS, &eflags);

    // Segment registers: In 32-bit Protected Mode (UC_MODE_32), Unicorn establishes
    // flat 4GB descriptors with D/B=1 (32-bit default operand size). Writing 0 to CS or data
    // segments invalidates the hidden descriptor cache and forces 16-bit decoding.
    if (m_cpuBits == 16) {
        uint16_t segZero = 0;
        uc_reg_write(m_uc, UC_X86_REG_CS, &segZero);
        uc_reg_write(m_uc, UC_X86_REG_DS, &segZero);
        uc_reg_write(m_uc, UC_X86_REG_ES, &segZero);
        uc_reg_write(m_uc, UC_X86_REG_FS, &segZero);
        uc_reg_write(m_uc, UC_X86_REG_GS, &segZero);
        uc_reg_write(m_uc, UC_X86_REG_SS, &segZero);
    }

    m_cachedState.instructionCount = 0;
    m_vga.ClearScreen(m_ram, m_ramSize, 0);
    m_syscalls.Reset();
    RefreshStateCache();
}

void Emulator::Start() {
    if (m_status == EmulatorStatus::Stopped || m_status == EmulatorStatus::Error) return;
    if (m_status == EmulatorStatus::Halted) {
        Reset();
    }
    m_workerActive = true;
    m_status = EmulatorStatus::Running;
}

void Emulator::Pause() {
    m_workerActive = false;
    if (m_uc) {
        uc_emu_stop(m_uc);
    }
    for (int i = 0; i < 50 && m_workerRunningQuantum.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_status = EmulatorStatus::Paused;
    m_cachedState.status = EmulatorStatus::Paused;
}

void Emulator::Halt() {
    m_workerActive = false;
    if (m_uc) {
        uc_emu_stop(m_uc);
    }
    for (int i = 0; i < 50 && m_workerRunningQuantum.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_status = EmulatorStatus::Halted;
    m_cachedState.status = EmulatorStatus::Halted;
}

void Emulator::Step() {
    if (m_status == EmulatorStatus::Stopped || m_status == EmulatorStatus::Error) return;
    if (m_status == EmulatorStatus::Halted) {
        Reset();
    }
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

    // Reinitialize Unicorn completely to clear all TCG translation blocks,
    // TLBs, CPU registers, and internal JIT cache!
    ReinitUnicorn(m_cpuBits);

    // Cache the binary payload and metadata for reliable resets
    m_loadedBinary.assign(data, data + size);
    m_loadedLoadAddress = loadAddress;
    m_loadedEntryPoint = entryPoint;
    m_codeBaseAddress = loadAddress;
    m_codeSize = static_cast<uint32_t>(size);

    Reset();
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

        // Runaway boundary protection: if current EIP is outside valid code regions of main program and loaded libraries
        if (m_codeSize > 0 && !IsAddressInExecutableCode(currentEip)) {
            m_workerActive = false;
            m_status = EmulatorStatus::Halted;
            m_cachedState.status = EmulatorStatus::Halted;
            m_console.WriteText("[VM] Reached end of program (EIP: 0x" + std::to_string(currentEip) + "). Execution halted.\n");
            continue;
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

        m_workerRunningQuantum = true;
        uc_err err = uc_emu_start(m_uc, currentEip, m_ramSize, 0, count);
        m_workerRunningQuantum = false;

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            RefreshStateCache();
        }

        if (m_status == EmulatorStatus::Halted) {
            m_workerActive = false;
            m_cachedState.status = EmulatorStatus::Halted;
        } else if (isStepping) {
            m_workerActive = false;
            m_status = EmulatorStatus::Paused;
            m_cachedState.status = EmulatorStatus::Paused;
        } else if (err != UC_ERR_OK) {
            m_workerActive = false;
            if (m_status != EmulatorStatus::Halted) {
                m_status = EmulatorStatus::Paused;
                m_cachedState.status = EmulatorStatus::Paused;
            }
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
        return;
    }

    // Runaway protection: if code executes outside loaded binaries into uninitialized memory
    if (self->m_codeSize > 0 && !self->IsAddressInExecutableCode(addr)) {
        uint8_t op[2] = {0, 0};
        if (self->ReadMemory(addr, op, 2) && op[0] == 0x00 && op[1] == 0x00) {
            uc_emu_stop(uc);
            self->m_workerActive = false;
            self->m_status = EmulatorStatus::Halted;
            self->m_cachedState.status = EmulatorStatus::Halted;
            self->m_console.WriteText("[VM] Reached end of program code (uninitialized memory at 0x" + std::to_string(addr) + "). Execution halted.\n");
            return;
        }
    }

    // Check for HLT instruction (0xF4)
    uint8_t op = 0;
    if (self->ReadMemory(addr, &op, 1) && op == 0xF4) {
        uc_emu_stop(uc);
        self->m_workerActive = false;
        self->m_status = EmulatorStatus::Halted;
        self->m_cachedState.status = EmulatorStatus::Halted;
        self->m_console.WriteText("[VM] CPU HLT instruction executed at 0x" + std::to_string(addr) + ".\n");
        return;
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

// -----------------------------------------------------------------------------
// ACFH Dynamic Linking & Library Management
// -----------------------------------------------------------------------------

static bool MatchSymbolName(const std::string& a, const std::string& b) {
    if (a == b) return true;
    std::string s1 = (a.rfind('_', 0) == 0) ? a.substr(1) : a;
    std::string s2 = (b.rfind('_', 0) == 0) ? b.substr(1) : b;
    if (s1.size() != s2.size()) return false;
    for (size_t i = 0; i < s1.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s1[i])) != std::tolower(static_cast<unsigned char>(s2[i]))) {
            return false;
        }
    }
    return true;
}

static bool MatchLibName(const std::string& a, const std::string& b) {
    if (a == b) return true;
    std::filesystem::path p1(a);
    std::filesystem::path p2(b);
    std::string s1 = p1.stem().string();
    std::string s2 = p2.stem().string();
    if (s1.size() != s2.size()) return false;
    for (size_t i = 0; i < s1.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s1[i])) != std::tolower(static_cast<unsigned char>(s2[i]))) {
            return false;
        }
    }
    return true;
}

bool Emulator::IsAddressInExecutableCode(uint32_t addr) const {
    if (m_codeSize > 0 && addr >= m_codeBaseAddress && addr < m_codeBaseAddress + m_codeSize) {
        return true;
    }
    for (const auto& lib : m_loadedLibraries) {
        uint32_t codeStart = lib.baseAddress + lib.header.code_offset;
        if (lib.header.code_size > 0 && addr >= codeStart && addr < codeStart + lib.header.code_size) {
            return true;
        }
    }
    return false;
}

bool Emulator::LoadAcfhExecutable(const std::string& filePath, const AcfhParsedModule& module) {
    Pause();

    m_mainModule = module;
    m_mainModule->filePath = filePath;
    m_mainModule->fileName = std::filesystem::path(filePath).filename().string();
    m_mainModule->baseAddress = 0x00000000;

    uint32_t payloadSize = static_cast<uint32_t>(module.rawBuffer.size());
    uint32_t entryPoint = module.header.code_offset;
    if (module.header.entry_point_offset != 0xFFFFFFFF) {
        entryPoint = module.header.code_offset + module.header.entry_point_offset;
    }

    // Load full ACFH binary (header + code + data + rodata) into RAM starting at 0x00000000.
    // This perfectly aligns .code at code_offset (0x70) and .rodata at rodata_offset (0x318),
    // matching AstraC's `org = sizeof(AC_FILE_HEADER) = 0x70` addressing.
    const uint8_t* payloadPtr = module.rawBuffer.data();
    if (!LoadBinary(payloadPtr, payloadSize, 0x00000000, entryPoint)) {
        return false;
    }

    // Auto-resolve imported libraries from disk if not already loaded in memory
    std::filesystem::path exeDir = std::filesystem::path(filePath).parent_path();
    for (const auto& imp : m_mainModule->imports) {
        bool alreadyLoaded = false;
        for (const auto& lib : m_loadedLibraries) {
            if (MatchLibName(lib.fileName, imp.libName)) {
                alreadyLoaded = true;
                break;
            }
        }
        if (alreadyLoaded) continue;

        // Search candidate paths on disk
        std::vector<std::filesystem::path> candidates = {
            exeDir / imp.libName,
            std::filesystem::path("examples") / imp.libName,
            imp.libName
        };

        bool foundAndLoaded = false;
        for (const auto& cand : candidates) {
            if (std::filesystem::exists(cand)) {
                if (LoadAcfhLibraryFile(cand.string())) {
                    foundAndLoaded = true;
                    break;
                }
            }
        }

        // If not found as .lib, check if source .ac exists and compile it into a .lib
        if (!foundAndLoaded) {
            std::filesystem::path libStem = std::filesystem::path(imp.libName).stem();
            std::vector<std::filesystem::path> srcCandidates = {
                exeDir / (libStem.string() + ".ac"),
                std::filesystem::path("examples") / (libStem.string() + ".ac"),
                exeDir / (libStem.string() + ".as"),
                std::filesystem::path("examples") / (libStem.string() + ".as")
            };
            for (const auto& srcCand : srcCandidates) {
                if (std::filesystem::exists(srcCand)) {
                    CompileOptions opts;
                    opts.format = TargetFormat::AcfhLib;
                    CompilationResult cRes = AstraCToolchain::CompileFile(srcCand.string(), m_config.astracPath, opts);
                    if (cRes.success) {
                        LoadAcfhLibraryFile(cRes.binaryPath);
                        foundAndLoaded = true;
                        break;
                    }
                }
            }
        }
    }

    // Re-link imports after resolving libraries
    RelinkImports();

    m_config.lastLoadedFile = filePath;
    ConfigManager::Save(m_config);

    m_console.WriteText("[System] Loaded ACFH executable '" + m_mainModule->fileName +
        "'. Entry point: 0x" + std::to_string(entryPoint) + ", Imports: " +
        std::to_string(m_mainModule->imports.size()) + "\n");
    return true;
}

bool Emulator::LoadAcfhLibrary(const std::string& filePath, const AcfhParsedModule& module) {
    Pause();

    AcfhParsedModule mod = module;
    mod.filePath = filePath;
    mod.fileName = std::filesystem::path(filePath).filename().string();
    mod.isLibrary = true;

    // Check if an existing library with same name is loaded; if so, replace it
    for (auto it = m_loadedLibraries.begin(); it != m_loadedLibraries.end(); ++it) {
        if (MatchLibName(it->fileName, mod.fileName)) {
            m_loadedLibraries.erase(it);
            break;
        }
    }

    // Calculate base address in library arena (0x00100000+)
    uint32_t nextBase = kLibraryArenaBase;
    for (const auto& existing : m_loadedLibraries) {
        uint32_t alignedSize = ((existing.totalMappedSize + 4095) / 4096) * 4096;
        uint32_t endAddr = existing.baseAddress + alignedSize;
        if (endAddr > nextBase) {
            nextBase = endAddr;
        }
    }

    mod.baseAddress = nextBase;
    mod.totalMappedSize = static_cast<uint32_t>(mod.rawBuffer.size()) + mod.header.bss_size;

    // Update runtime addresses of exported functions (codeOffset is relative to .code)
    for (auto& exp : mod.exports) {
        exp.runtimeAddress = mod.baseAddress + mod.header.code_offset + exp.codeOffset;
    }

    m_loadedLibraries.push_back(mod);

    // Map into guest physical RAM and apply relocations
    RemapLoadedLibraries();

    // Relink imports
    RelinkImports();

    m_console.WriteText("[Library] Loaded '" + mod.fileName + "' at 0x" +
        std::to_string(mod.baseAddress) + " (" + std::to_string(mod.exports.size()) + " exports).\n");
    return true;
}

bool Emulator::LoadAcfhLibraryFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        m_console.WriteText("[Library Error] Failed to open: " + filePath + "\n");
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) {
        m_console.WriteText("[Library Error] File is empty: " + filePath + "\n");
        return false;
    }

    AcfhParsedModule parsedMod;
    if (!AstraCToolchain::ParseAcfhModule(buffer.data(), buffer.size(), parsedMod)) {
        m_console.WriteText("[Library Error] Invalid ACFH binary: " + filePath + "\n");
        return false;
    }

    return LoadAcfhLibrary(filePath, parsedMod);
}

bool Emulator::UnloadAcfhLibrary(const std::string& libName) {
    Pause();
    for (auto it = m_loadedLibraries.begin(); it != m_loadedLibraries.end(); ++it) {
        if (MatchLibName(it->fileName, libName)) {
            // Zero out memory region
            if (m_ram && it->baseAddress + it->totalMappedSize <= m_ramSize) {
                std::memset(m_ram + it->baseAddress, 0, it->totalMappedSize);
            }
            m_loadedLibraries.erase(it);
            RemapLoadedLibraries();
            RelinkImports();
            m_console.WriteText("[Library] Unloaded: " + libName + "\n");
            return true;
        }
    }
    return false;
}

void Emulator::ClearAcfhLibraries() {
    Pause();
    for (const auto& lib : m_loadedLibraries) {
        if (m_ram && lib.baseAddress + lib.totalMappedSize <= m_ramSize) {
            std::memset(m_ram + lib.baseAddress, 0, lib.totalMappedSize);
        }
    }
    m_loadedLibraries.clear();
    RelinkImports();
}

void Emulator::RemapLoadedLibraries() {
    if (!m_ram) return;

    for (auto& lib : m_loadedLibraries) {
        if (lib.baseAddress + lib.totalMappedSize > m_ramSize) continue;

        // Zero destination region (header + code + data + rodata + bss)
        std::memset(m_ram + lib.baseAddress, 0, lib.totalMappedSize);

        // Copy raw binary starting at offset 0 so code_offset and rodata_offset align with AstraC's org
        size_t copyBytes = std::min<size_t>(lib.rawBuffer.size(), lib.totalMappedSize);
        std::memcpy(m_ram + lib.baseAddress, lib.rawBuffer.data(), copyBytes);

        // Apply base relocations
        ApplyModuleRelocations(lib);
    }

    if (m_uc) {
        uc_ctl_flush_tb(m_uc);
        uc_ctl_flush_tlb(m_uc);
    }
}

void Emulator::ApplyModuleRelocations(AcfhParsedModule& mod) {
    if (mod.baseAddress == 0 || mod.relocations.empty() || !m_ram) return;

    for (uint32_t relocOff : mod.relocations) {
        if (relocOff < mod.header.code_size) {
            // Instruction inside .code (relocOff is code-relative)
            uint32_t targetInstr = mod.baseAddress + mod.header.code_offset + relocOff;
            if (targetInstr + 15 <= m_ramSize && m_capstone) {
                cs_insn* insn = nullptr;
                size_t count = cs_disasm(m_capstone, m_ram + targetInstr, 15, targetInstr, 1, &insn);
                if (count > 0 && insn) {
                    if (insn[0].size >= 4) {
                        uint32_t patchOffset = targetInstr + insn[0].size - 4;
                        if (patchOffset + 4 <= m_ramSize) {
                            uint32_t currentVal = 0;
                            std::memcpy(&currentVal, m_ram + patchOffset, 4);
                            currentVal += mod.baseAddress;
                            std::memcpy(m_ram + patchOffset, &currentVal, 4);
                        }
                    }
                    cs_free(insn, count);
                }
            }
        } else {
            // Data or rodata pointer (relocOff is code-relative offset into payload)
            uint32_t patchOffset = mod.baseAddress + mod.header.code_offset + relocOff;
            if (patchOffset + 4 <= m_ramSize) {
                uint32_t currentVal = 0;
                std::memcpy(&currentVal, m_ram + patchOffset, 4);
                currentVal += mod.baseAddress;
                std::memcpy(m_ram + patchOffset, &currentVal, 4);
            }
        }
    }
}

bool Emulator::RelinkImports() {
    if (!m_mainModule.has_value() || !m_ram) return false;

    for (auto& imp : m_mainModule->imports) {
        imp.resolved = false;
        imp.resolvedAddress = 0;
        imp.runtimePatchAddress = m_mainModule->baseAddress + m_mainModule->header.code_offset + imp.codePatchOffset;

        for (const auto& lib : m_loadedLibraries) {
            if (!MatchLibName(lib.fileName, imp.libName)) continue;

            for (const auto& exp : lib.exports) {
                if (MatchSymbolName(exp.name, imp.funcName)) {
                    imp.resolved = true;
                    imp.resolvedAddress = exp.runtimeAddress;

                    uint32_t patchAddr = imp.runtimePatchAddress;
                    if (patchAddr > 0 && patchAddr + 4 <= m_ramSize) {
                        uint8_t op = m_ram[patchAddr - 1];
                        if (op == 0xE8 || op == 0xE9) {
                            int32_t rel32 = static_cast<int32_t>(imp.resolvedAddress) - static_cast<int32_t>(patchAddr + 4);
                            std::memcpy(m_ram + patchAddr, &rel32, 4);
                        } else {
                            std::memcpy(m_ram + patchAddr, &imp.resolvedAddress, 4);
                        }
                    }
                    break;
                }
            }
            if (imp.resolved) break;
        }
    }

    if (m_uc) {
        uc_ctl_flush_tb(m_uc);
        uc_ctl_flush_tlb(m_uc);
    }

    return true;
}

} // namespace em836
