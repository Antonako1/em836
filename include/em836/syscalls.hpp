/**
 * @file syscalls.hpp
 * @brief Unified System Call ABI (INT 0x88) and Hardware Interrupt Dispatcher.
 *
 * This header defines the system call interface and hardware interrupt handling for em836.
 *
 * Calling Convention:
 * - Triggered via software interrupt: INT 0x88.
 * - EAX: System call identifier (SYS_* constant).
 * - EBX: 1st argument.
 * - ECX: 2nd argument.
 * - EDX: 3rd argument.
 * - ESI: 4th argument.
 * - EDI: 5th argument.
 * - Return Value: Returned in EAX (non-negative integer on success; negative value on error).
 *
 * Subsystems Provided:
 * 1. Process Control (SYS_EXIT, SYS_YIELD, SYS_GET_TICKS)
 * 2. Console I/O (SYS_PRINT_CHAR, SYS_PRINT_STR, SYS_READ_CHAR)
 * 3. Host File I/O (SYS_OPEN, SYS_READ, SYS_WRITE, SYS_CLOSE, SYS_SEEK)
 * 4. Dynamic Memory Management (SYS_SBRK)
 * 5. Accelerated Graphics Primitives (SYS_GFX_SET_MODE, SYS_GFX_CLEAR, SYS_GFX_PLOT, SYS_GFX_LINE, SYS_GFX_FILL_RECT)
 *
 * Backward Compatibility:
 * Also intercepts legacy INT 0x10 (BIOS video), INT 0x80 (Linux syscalls), INT 0x21 (DOS services),
 * and COM1 UART Port 0x03F8 / Bochs Port 0x00E9 I/O writes.
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <mutex>

namespace em836 {

// Forward declarations
class Emulator;

// -------------------------------------------------------------
// Interrupt Vector Numbers
// -------------------------------------------------------------
constexpr uint32_t INTR_CUSTOM_SYSCALL = 0x88; ///< Custom Unified Syscall Interrupt
constexpr uint32_t INTR_BIOS_VIDEO     = 0x10; ///< BIOS Video Services
constexpr uint32_t INTR_LINUX_SYSCALL  = 0x80; ///< Linux POSIX i386 Syscall
constexpr uint32_t INTR_DOS_SERVICES   = 0x21; ///< DOS Services

// -------------------------------------------------------------
// Hardware I/O Ports
// -------------------------------------------------------------
constexpr uint32_t PORT_COM1_DATA      = 0x03F8; ///< COM1 UART Serial Port Transmitter
constexpr uint32_t PORT_BOCHS_DEBUG    = 0x00E9; ///< Bochs / QEMU Port E9 Hack

// -------------------------------------------------------------
// Custom Syscall Identifiers (Passed in EAX)
// -------------------------------------------------------------
#ifdef SYS_EXIT
#undef SYS_EXIT
#endif
#ifdef SYS_OPEN
#undef SYS_OPEN
#endif
#ifdef SYS_READ
#undef SYS_READ
#endif
#ifdef SYS_WRITE
#undef SYS_WRITE
#endif
#ifdef SYS_CLOSE
#undef SYS_CLOSE
#endif
#ifdef SYS_SEEK
#undef SYS_SEEK
#endif

// Process Control
constexpr uint32_t SYS_EXIT            = 0x01; ///< Halts execution. EBX: Exit code.
constexpr uint32_t SYS_YIELD           = 0x02; ///< Yields remaining CPU timeslice.
constexpr uint32_t SYS_GET_TICKS       = 0x03; ///< Returns elapsed milliseconds in EAX.

// Console I/O
constexpr uint32_t SYS_PRINT_CHAR      = 0x10; ///< EBX: Character byte. Prints to virtual console.
constexpr uint32_t SYS_PRINT_STR       = 0x11; ///< EBX: String address, ECX: Max length (0 = null-terminated).
constexpr uint32_t SYS_READ_CHAR       = 0x12; ///< EBX: 1=block, 0=non-blocking. Returns ASCII char or -1 in EAX.

// File I/O
constexpr uint32_t SYS_OPEN            = 0x20; ///< EBX: Path string ptr, ECX: Flags (0=read, 1=write, 2=append). Returns handle.
constexpr uint32_t SYS_READ            = 0x21; ///< EBX: Handle, ECX: Buffer ptr, EDX: Count. Returns bytes read.
constexpr uint32_t SYS_WRITE           = 0x22; ///< EBX: Handle, ECX: Buffer ptr, EDX: Count. Returns bytes written.
constexpr uint32_t SYS_CLOSE           = 0x23; ///< EBX: Handle. Closes file. Returns 0 on success.
constexpr uint32_t SYS_SEEK            = 0x24; ///< EBX: Handle, ECX: Offset, EDX: Origin (0=set, 1=cur, 2=end). Returns new offset.

// Memory Management
constexpr uint32_t SYS_SBRK            = 0x30; ///< EBX: Increment in bytes. Returns previous heap break in EAX.

// Accelerated Graphics
constexpr uint32_t SYS_GFX_SET_MODE    = 0x40; ///< EBX: Mode (0=Text 80x25, 1=Graphics 640x480).
constexpr uint32_t SYS_GFX_CLEAR       = 0x41; ///< EBX: Color index (0-15).
constexpr uint32_t SYS_GFX_PLOT        = 0x42; ///< EBX: X, ECX: Y, EDX: Color (0-15).
constexpr uint32_t SYS_GFX_LINE        = 0x43; ///< EBX: X0, ECX: Y0, EDX: X1, ESI: Y1, EDI: Color (0-15).
constexpr uint32_t SYS_GFX_FILL_RECT   = 0x44; ///< EBX: X, ECX: Y, EDX: W, ESI: H, EDI: Color (0-15).

/**
 * @class SyscallDispatcher
 * @brief Implements the runtime handling of INT 0x88, legacy software interrupts, and I/O ports.
 */
class SyscallDispatcher {
public:
    SyscallDispatcher();
    ~SyscallDispatcher();

    // Prevent copies
    SyscallDispatcher(const SyscallDispatcher&) = delete;
    SyscallDispatcher& operator=(const SyscallDispatcher&) = delete;

    /**
     * @brief Resets file tables, heap allocations, and dispatcher state upon CPU reset.
     */
    void Reset();

    /**
     * @brief Dispatches a software interrupt invoked by the guest CPU.
     * @param emu Reference to the executing Emulator instance.
     * @param intno Interrupt vector number (e.g., 0x88, 0x10, 0x80, 0x21).
     */
    void HandleInterrupt(Emulator& emu, uint32_t intno);

    /**
     * @brief Dispatches hardware port I/O (IN / OUT instructions).
     * @param emu Reference to the executing Emulator instance.
     * @param port I/O port number (e.g. 0x03F8, 0x00E9).
     * @param size Size in bytes of transfer (1, 2, or 4).
     * @param value Pointer to data buffer (read into on IN, read from on OUT).
     * @param isWrite True if OUT instruction, false if IN instruction.
     */
    void HandlePortIO(Emulator& emu, uint32_t port, uint32_t size, uint32_t* value, bool isWrite);

private:
    std::mutex m_mutex;

    // Guest Heap tracking for SYS_SBRK
    uint32_t m_currentBreak = 0x00200000; ///< Starts at 2 MB by default
    const uint32_t m_maxHeapBreak = 0x00700000; ///< Maximum heap limit below stack

    // Host file handle table (guest fd -> host FILE*)
    uint32_t m_nextFileHandle = 3; ///< 0=stdin, 1=stdout, 2=stderr
    std::unordered_map<uint32_t, FILE*> m_openFiles;

    // Dispatcher sub-handlers
    void HandleCustomSyscall(Emulator& emu);
    void HandleBiosVideo(Emulator& emu);
    void HandleLinuxSyscall(Emulator& emu);
    void HandleDosServices(Emulator& emu);

    // Memory access helpers
    std::string ReadGuestString(Emulator& emu, uint32_t address, size_t maxLen = 4096);
};

} // namespace em836
