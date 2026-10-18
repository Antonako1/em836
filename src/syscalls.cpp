/**
 * @file syscalls.cpp
 * @brief Implementation of Unified System Call ABI (INT 0x88) and Hardware Interrupt Dispatcher.
 */

#include "em836/syscalls.hpp"
#include "em836/emulator.hpp"
#include <SDL3/SDL.h>
#include <unicorn/unicorn.h>
#include <iostream>
#include <vector>

namespace em836 {

SyscallDispatcher::SyscallDispatcher() {
    Reset();
}

SyscallDispatcher::~SyscallDispatcher() {
    Reset();
}

void SyscallDispatcher::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_openFiles) {
        if (pair.second) {
            std::fclose(pair.second);
        }
    }
    m_openFiles.clear();
    m_nextFileHandle = 3;
    m_currentBreak = 0x00200000; // Reset heap break to 2 MB
}

std::string SyscallDispatcher::ReadGuestString(Emulator& emu, uint32_t address, size_t maxLen) {
    std::string result;
    result.reserve(std::min(maxLen, size_t(256)));

    uint8_t ch = 0;
    for (size_t i = 0; i < maxLen; ++i) {
        if (!emu.ReadMemory(address + static_cast<uint32_t>(i), &ch, 1) || ch == '\0') {
            break;
        }
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

void SyscallDispatcher::HandleInterrupt(Emulator& emu, uint32_t intno) {
    switch (intno) {
        case INTR_CUSTOM_SYSCALL:
            HandleCustomSyscall(emu);
            break;
        case INTR_BIOS_VIDEO:
            HandleBiosVideo(emu);
            break;
        case INTR_LINUX_SYSCALL:
            HandleLinuxSyscall(emu);
            break;
        case INTR_DOS_SERVICES:
            HandleDosServices(emu);
            break;
        default:
            // Unhandled interrupt: log to console
            emu.GetConsole().WriteText("[VM] Unhandled interrupt vector 0x" + std::to_string(intno) + "\n");
            break;
    }
}

void SyscallDispatcher::HandleCustomSyscall(Emulator& emu) {
    uint32_t eax = emu.ReadRegister(UC_X86_REG_EAX);
    uint32_t ebx = emu.ReadRegister(UC_X86_REG_EBX);
    uint32_t ecx = emu.ReadRegister(UC_X86_REG_ECX);
    uint32_t edx = emu.ReadRegister(UC_X86_REG_EDX);
    uint32_t esi = emu.ReadRegister(UC_X86_REG_ESI);
    uint32_t edi = emu.ReadRegister(UC_X86_REG_EDI);

    switch (eax) {
        // -------------------------------------------------------------
        // Process Management
        // -------------------------------------------------------------
        case SYS_EXIT: {
            emu.Halt();
            emu.GetConsole().WriteText("[VM] Process exited with status code " + std::to_string(ebx) + ".\n");
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        case SYS_YIELD: {
            std::this_thread::yield();
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        case SYS_GET_TICKS: {
            uint64_t ticks = SDL_GetTicks();
            emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(ticks));
            break;
        }

        // -------------------------------------------------------------
        // Console I/O
        // -------------------------------------------------------------
        case SYS_PRINT_CHAR: {
            char ch = static_cast<char>(ebx & 0xFF);
            emu.GetConsole().WriteChar(ch);
            emu.WriteRegister(UC_X86_REG_EAX, 1);
            break;
        }

        case SYS_PRINT_STR: {
            size_t maxLen = (ecx > 0) ? ecx : 65536;
            std::string text = ReadGuestString(emu, ebx, maxLen);
            emu.GetConsole().WriteText(text);
            emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(text.length()));
            break;
        }

        case SYS_READ_CHAR: {
            char ch = 0;
            if (emu.GetConsole().DequeueInputChar(ch)) {
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(static_cast<uint8_t>(ch)));
            } else {
                // Non-blocking empty or wait
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            }
            break;
        }

        // -------------------------------------------------------------
        // Host File I/O
        // -------------------------------------------------------------
        case SYS_OPEN: {
            std::string path = ReadGuestString(emu, ebx, 1024);
            const char* mode = "rb";
            if (ecx == 1) mode = "wb";
            else if (ecx == 2) mode = "ab";

            FILE* fp = nullptr;
            fopen_s(&fp, path.c_str(), mode);
            if (fp) {
                std::lock_guard<std::mutex> lock(m_mutex);
                uint32_t handle = m_nextFileHandle++;
                m_openFiles[handle] = fp;
                emu.WriteRegister(UC_X86_REG_EAX, handle);
            } else {
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            }
            break;
        }

        case SYS_READ: {
            uint32_t handle = ebx;
            uint32_t bufferAddr = ecx;
            uint32_t count = edx;

            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_openFiles.find(handle);
            if (it != m_openFiles.end() && it->second) {
                std::vector<uint8_t> temp(count);
                size_t readBytes = std::fread(temp.data(), 1, count, it->second);
                if (readBytes > 0) {
                    emu.WriteMemory(bufferAddr, temp.data(), readBytes);
                }
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(readBytes));
            } else {
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            }
            break;
        }

        case SYS_WRITE: {
            uint32_t handle = ebx;
            uint32_t bufferAddr = ecx;
            uint32_t count = edx;

            if (handle == 1 || handle == 2) {
                // stdout / stderr writes are mirrored to the Console output
                std::vector<uint8_t> temp(count);
                if (emu.ReadMemory(bufferAddr, temp.data(), count)) {
                    std::string str(temp.begin(), temp.end());
                    emu.GetConsole().WriteText(str);
                    emu.WriteRegister(UC_X86_REG_EAX, count);
                } else {
                    emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
                }
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_openFiles.find(handle);
                if (it != m_openFiles.end() && it->second) {
                    std::vector<uint8_t> temp(count);
                    if (emu.ReadMemory(bufferAddr, temp.data(), count)) {
                        size_t written = std::fwrite(temp.data(), 1, count, it->second);
                        emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(written));
                    } else {
                        emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
                    }
                } else {
                    emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
                }
            }
            break;
        }

        case SYS_CLOSE: {
            uint32_t handle = ebx;
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_openFiles.find(handle);
            if (it != m_openFiles.end()) {
                if (it->second) {
                    std::fclose(it->second);
                }
                m_openFiles.erase(it);
                emu.WriteRegister(UC_X86_REG_EAX, 0);
            } else {
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            }
            break;
        }

        case SYS_SEEK: {
            uint32_t handle = ebx;
            long offset = static_cast<long>(ecx);
            int origin = SEEK_SET;
            if (edx == 1) origin = SEEK_CUR;
            else if (edx == 2) origin = SEEK_END;

            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_openFiles.find(handle);
            if (it != m_openFiles.end() && it->second) {
                std::fseek(it->second, offset, origin);
                long newPos = std::ftell(it->second);
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(newPos));
            } else {
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            }
            break;
        }

        // -------------------------------------------------------------
        // Memory Management
        // -------------------------------------------------------------
        case SYS_SBRK: {
            std::lock_guard<std::mutex> lock(m_mutex);
            uint32_t oldBreak = m_currentBreak;
            if (ebx == 0) {
                // Query current break
                emu.WriteRegister(UC_X86_REG_EAX, oldBreak);
            } else if (m_currentBreak + ebx <= m_maxHeapBreak) {
                m_currentBreak += ebx;
                emu.WriteRegister(UC_X86_REG_EAX, oldBreak);
            } else {
                // Out of heap memory
                emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            }
            break;
        }

        // -------------------------------------------------------------
        // Accelerated Graphics
        // -------------------------------------------------------------
        case SYS_GFX_SET_MODE: {
            VideoMode mode = (ebx == 1) ? VideoMode::Graphics640x480 : VideoMode::Text80x25;
            emu.GetVga().SetMode(mode);
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        case SYS_GFX_CLEAR: {
            emu.GetVga().ClearScreen(emu.GetRamBuffer(), emu.GetRamSize(), static_cast<uint8_t>(ebx));
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        case SYS_GFX_PLOT: {
            emu.GetVga().PlotPixel(
                emu.GetRamBuffer(),
                emu.GetRamSize(),
                static_cast<int>(ebx),
                static_cast<int>(ecx),
                static_cast<uint8_t>(edx)
            );
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        case SYS_GFX_LINE: {
            emu.GetVga().DrawLine(
                emu.GetRamBuffer(),
                emu.GetRamSize(),
                static_cast<int>(ebx),
                static_cast<int>(ecx),
                static_cast<int>(edx),
                static_cast<int>(esi),
                static_cast<uint8_t>(edi)
            );
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        case SYS_GFX_FILL_RECT: {
            emu.GetVga().FillRect(
                emu.GetRamBuffer(),
                emu.GetRamSize(),
                static_cast<int>(ebx),
                static_cast<int>(ecx),
                static_cast<int>(edx),
                static_cast<int>(esi),
                static_cast<uint8_t>(edi)
            );
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;
        }

        default:
            emu.GetConsole().WriteText("[VM] Unknown custom syscall 0x" + std::to_string(eax) + "\n");
            emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            break;
    }
}

void SyscallDispatcher::HandleBiosVideo(Emulator& emu) {
    uint32_t eax = emu.ReadRegister(UC_X86_REG_EAX);
    uint8_t ah = static_cast<uint8_t>((eax >> 8) & 0xFF);
    uint8_t al = static_cast<uint8_t>(eax & 0xFF);

    if (ah == 0x0E) {
        // Teletype Character Output (INT 0x10, AH = 0x0E)
        emu.GetConsole().WriteChar(static_cast<char>(al));
    } else if (ah == 0x00) {
        // Set Video Mode
        if (al == 0x12) {
            // Mode 12h: 640x480 16-color graphics
            emu.GetVga().SetMode(VideoMode::Graphics640x480);
        } else if (al == 0x03) {
            // Mode 03h: 80x25 16-color text
            emu.GetVga().SetMode(VideoMode::Text80x25);
        }
    }
}

void SyscallDispatcher::HandleLinuxSyscall(Emulator& emu) {
    uint32_t eax = emu.ReadRegister(UC_X86_REG_EAX);
    uint32_t ebx = emu.ReadRegister(UC_X86_REG_EBX);
    uint32_t ecx = emu.ReadRegister(UC_X86_REG_ECX);
    uint32_t edx = emu.ReadRegister(UC_X86_REG_EDX);

    switch (eax) {
        case 1: // sys_exit
            emu.Halt();
            emu.GetConsole().WriteText("[VM] Linux sys_exit(" + std::to_string(ebx) + ")\n");
            emu.WriteRegister(UC_X86_REG_EAX, 0);
            break;

        case 4: { // sys_write
            if (ebx == 1 || ebx == 2) { // stdout or stderr
                std::vector<uint8_t> temp(edx);
                if (emu.ReadMemory(ecx, temp.data(), edx)) {
                    std::string str(temp.begin(), temp.end());
                    emu.GetConsole().WriteText(str);
                    emu.WriteRegister(UC_X86_REG_EAX, edx);
                } else {
                    emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
                }
            }
            break;
        }

        default:
            emu.GetConsole().WriteText("[VM] Unhandled Linux syscall 0x" + std::to_string(eax) + "\n");
            emu.WriteRegister(UC_X86_REG_EAX, static_cast<uint32_t>(-1));
            break;
    }
}

void SyscallDispatcher::HandleDosServices(Emulator& emu) {
    uint32_t eax = emu.ReadRegister(UC_X86_REG_EAX);
    uint32_t edx = emu.ReadRegister(UC_X86_REG_EDX);
    uint8_t ah = static_cast<uint8_t>((eax >> 8) & 0xFF);

    if (ah == 0x02) {
        // Output Character (DL)
        char ch = static_cast<char>(edx & 0xFF);
        emu.GetConsole().WriteChar(ch);
    } else if (ah == 0x09) {
        // Output '$'-terminated string at EDX
        std::string str;
        uint8_t ch = 0;
        for (uint32_t i = 0; i < 4096; ++i) {
            if (!emu.ReadMemory(edx + i, &ch, 1) || ch == '$') break;
            str.push_back(static_cast<char>(ch));
        }
        emu.GetConsole().WriteText(str);
    } else if (ah == 0x4C) {
        // Terminate Process
        emu.Halt();
        emu.GetConsole().WriteText("[VM] DOS process terminated.\n");
    }
}

void SyscallDispatcher::HandlePortIO(Emulator& emu, uint32_t port, uint32_t size, uint32_t* value, bool isWrite) {
    if (isWrite) {
        uint32_t byteVal = *value & 0xFF;
        if (port == PORT_COM1_DATA || port == PORT_BOCHS_DEBUG) {
            emu.GetConsole().WriteChar(static_cast<char>(byteVal));
        }
    } else {
        if (port == PORT_COM1_DATA) {
            *value = 0x00; // Ready / no line status errors
        }
    }
}

} // namespace em836
