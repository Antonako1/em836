/**
 * @file console.cpp
 * @brief Implementation of Virtual Console Terminal, Guest Input Stream, and Emulator CLI.
 */

#include "em836/console.hpp"
#include "em836/emulator.hpp"
#include "em836/astrac.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace em836 {

ConsoleSubsystem::ConsoleSubsystem() {
    m_outputText = "em836 Interactive Virtual Terminal Ready.\n"
                   "Type 'help' for emulator commands, or type text to send to guest input stream.\n\n";
}

void ConsoleSubsystem::WriteChar(char ch) {
    if (ch == '\0') return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputText += ch;
    if (m_outputText.length() > m_maxOutputLength) {
        m_outputText.erase(0, m_outputText.length() - (m_maxOutputLength / 2));
    }
    if (m_autoScroll) {
        m_scrollToBottom = true;
    }
}

void ConsoleSubsystem::WriteText(const std::string& text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (char ch : text) {
        if (ch != '\0') {
            m_outputText += ch;
        }
    }
    if (m_outputText.length() > m_maxOutputLength) {
        m_outputText.erase(0, m_outputText.length() - (m_maxOutputLength / 2));
    }
    if (m_autoScroll) {
        m_scrollToBottom = true;
    }
}

void ConsoleSubsystem::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputText.clear();
}

std::string ConsoleSubsystem::GetOutputText() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_outputText;
}

void ConsoleSubsystem::EnqueueInputChar(char ch) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputQueue.push(ch);
}

void ConsoleSubsystem::EnqueueInputString(const std::string& str) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (char c : str) {
        m_inputQueue.push(c);
    }
}

bool ConsoleSubsystem::DequeueInputChar(char& outChar) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_inputQueue.empty()) {
        return false;
    }
    outChar = m_inputQueue.front();
    m_inputQueue.pop();
    return true;
}

bool ConsoleSubsystem::HasInputChar() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_inputQueue.empty();
}

void ConsoleSubsystem::AppendBuildLog(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (char ch : text) {
        if (ch != '\0') {
            m_buildLogText += ch;
        }
    }
}

void ConsoleSubsystem::ClearBuildLog() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buildLogText.clear();
}

std::string ConsoleSubsystem::GetBuildLog() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buildLogText;
}

void ConsoleSubsystem::ProcessInputLine(const std::string& line, Emulator& emu) {
    if (line.empty()) {
        return;
    }

    // Echo input line to terminal output
    WriteText("> " + line + "\n");

    // Try executing as emulator CLI command first
    if (ExecuteCommand(line, emu)) {
        return;
    }

    // Otherwise, forward string to the guest application's input stream
    EnqueueInputString(line + "\n");
}

bool ConsoleSubsystem::ExecuteCommand(const std::string& cmdLine, Emulator& emu) {
    std::istringstream iss(cmdLine);
    std::string cmd;
    iss >> cmd;

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::tolower(c); });

    if (cmd == "help") {
        WriteText("=== em836 Virtual Terminal Commands ===\n"
                  "  help             - Display this command reference\n"
                  "  run / start      - Resume or start CPU execution\n"
                  "  pause / stop     - Pause CPU execution\n"
                  "  step             - Execute single instruction\n"
                  "  reset            - Reset CPU registers, memory, and peripherals\n"
                  "  cls / clear      - Clear terminal screen\n"
                  "  load <file>      - Load and assemble/compile source or binary\n"
                  "  loadlib <path>   - Load ACFH dynamic library (.lib) into memory\n"
                  "  unloadlib <name> - Unload dynamic library by name\n"
                  "  libs             - List loaded dynamic libraries and exported functions\n"
                  "  acfh / objdump   - Print ACFH header, exports, and import patch table\n"
                  "  speed <hz>       - Set clock throttling (0 = Unlimited, e.g. 1000 for 1kHz)\n"
                  "  mode <text|gfx>  - Switch VGA display mode\n"
                  "  mem <addr> [len] - Display memory hex dump at hexadecimal address\n"
                  "  reg              - Print current CPU register state\n"
                  "  bp <addr>        - Toggle breakpoint at hexadecimal address\n"
                  "  Any other text is forwarded directly to guest input stream (SYS_READ_CHAR).\n\n");
        return true;
    }

    if (cmd == "run" || cmd == "start") {
        emu.Start();
        WriteText("[CLI] CPU Execution started.\n");
        return true;
    }

    if (cmd == "pause" || cmd == "stop") {
        emu.Pause();
        WriteText("[CLI] CPU Execution paused.\n");
        return true;
    }

    if (cmd == "step") {
        emu.Step();
        WriteText("[CLI] Executed single step.\n");
        return true;
    }

    if (cmd == "reset") {
        emu.Reset();
        WriteText("[CLI] CPU and memory reset.\n");
        return true;
    }

    if (cmd == "cls" || cmd == "clear") {
        Clear();
        return true;
    }

    if (cmd == "load") {
        std::string filePath;
        iss >> filePath;
        if (filePath.empty()) {
            WriteText("[CLI] Error: Usage is 'load <path>'\n");
        } else {
            WriteText("[CLI] Loading: " + filePath + " ...\n");
            bool ok = AstraCToolchain::IngestFile(filePath, emu);
            if (ok) {
                WriteText("[CLI] Successfully loaded.\n");
            } else {
                WriteText("[CLI] Load failed. See AstraC Build Log for errors.\n");
            }
        }
        return true;
    }

    if (cmd == "loadlib") {
        std::string libPath;
        iss >> libPath;
        if (libPath.empty()) {
            WriteText("[CLI] Error: Usage is 'loadlib <path>'\n");
        } else {
            bool ok = emu.LoadAcfhLibraryFile(libPath);
            if (ok) {
                WriteText("[CLI] Library loaded successfully.\n");
            } else {
                WriteText("[CLI] Failed to load library.\n");
            }
        }
        return true;
    }

    if (cmd == "unloadlib") {
        std::string libName;
        iss >> libName;
        if (libName.empty()) {
            WriteText("[CLI] Error: Usage is 'unloadlib <name>'\n");
        } else {
            bool ok = emu.UnloadAcfhLibrary(libName);
            if (!ok) {
                WriteText("[CLI] Library not found: " + libName + "\n");
            }
        }
        return true;
    }

    if (cmd == "libs") {
        const auto& libs = emu.GetLoadedLibraries();
        if (libs.empty()) {
            WriteText("[CLI] No dynamic libraries loaded.\n");
        } else {
            std::ostringstream oss;
            oss << "=== Loaded ACFH Libraries (" << libs.size() << ") ===\n";
            for (size_t i = 0; i < libs.size(); ++i) {
                const auto& lib = libs[i];
                oss << "[" << i << "] " << lib.fileName << " @ 0x" << std::hex << std::uppercase << lib.baseAddress
                    << " (Code: " << std::dec << lib.header.code_size << " B, Mapped: " << lib.totalMappedSize
                    << " B, " << lib.exports.size() << " exports, " << lib.relocations.size() << " relocs)\n";
                for (const auto& exp : lib.exports) {
                    oss << "      - " << exp.name << " @ 0x" << std::hex << std::uppercase << exp.runtimeAddress << " (+" << exp.codeOffset << ")\n";
                }
            }
            WriteText(oss.str());
        }
        return true;
    }

    if (cmd == "acfh" || cmd == "objdump") {
        const auto& mainMod = emu.GetMainModule();
        if (!mainMod.has_value()) {
            WriteText("[CLI] No ACFH executable is currently loaded.\n");
        } else {
            const auto& m = *mainMod;
            std::ostringstream oss;
            oss << "=== ACFH Header: " << m.fileName << " ===\n"
                << "Version: 0x" << std::hex << m.header.version << "  Flags: 0x" << m.header.flags << "\n"
                << "Entry Point: 0x" << m.header.entry_point_offset << "\n"
                << "Sections:\n"
                << "  .code   offset 0x" << m.header.code_offset << "  size " << std::dec << m.header.code_size << " B\n"
                << "  .data   offset 0x" << std::hex << m.header.data_offset << "  size " << std::dec << m.header.data_size << " B\n"
                << "  .rodata offset 0x" << std::hex << m.header.rodata_offset << "  size " << std::dec << m.header.rodata_size << " B\n"
                << "  .bss    offset 0x" << std::hex << m.header.bss_offset << "  size " << std::dec << m.header.bss_size << " B\n"
                << "  relocs  offset 0x" << std::hex << m.header.reloc_offset << "  count " << std::dec << m.relocations.size() << "\n"
                << "  funcs   offset 0x" << std::hex << m.header.func_table_offset << "  count " << std::dec << m.exports.size() << "\n"
                << "  imports offset 0x" << std::hex << m.header.import_table_offset << "  count " << std::dec << m.imports.size() << "\n";
            if (!m.imports.empty()) {
                oss << "Imports:\n";
                for (size_t i = 0; i < m.imports.size(); ++i) {
                    const auto& imp = m.imports[i];
                    oss << "  [" << i << "] " << imp.libName << " -> " << imp.funcName
                        << " @ patch 0x" << std::hex << imp.runtimePatchAddress << " (";
                    if (imp.resolved) {
                        oss << "Resolved: 0x" << imp.resolvedAddress << ")\n";
                    } else {
                        oss << "UNRESOLVED)\n";
                    }
                }
            }
            WriteText(oss.str());
        }
        return true;
    }

    if (cmd == "speed") {
        uint32_t hz = 0;
        if (iss >> hz) {
            emu.SetClockSpeed(hz);
            WriteText("[CLI] Clock speed set to " + (hz == 0 ? "Unlimited" : std::to_string(hz) + " Hz") + ".\n");
        } else {
            WriteText("[CLI] Error: Usage is 'speed <hz>' (0 for unlimited)\n");
        }
        return true;
    }

    if (cmd == "mode") {
        std::string modeStr;
        iss >> modeStr;
        std::transform(modeStr.begin(), modeStr.end(), modeStr.begin(), [](unsigned char c) { return std::tolower(c); });
        if (modeStr == "text") {
            emu.GetVga().SetMode(VideoMode::Text80x25);
            WriteText("[CLI] Switched display to VGA Text Mode (80x25 at 0xB8000).\n");
        } else if (modeStr == "gfx" || modeStr == "graphics") {
            emu.GetVga().SetMode(VideoMode::Graphics640x480);
            WriteText("[CLI] Switched display to VGA Graphics Mode (640x480 at 0xA0000).\n");
        } else {
            WriteText("[CLI] Error: Mode must be 'text' or 'gfx'\n");
        }
        return true;
    }

    if (cmd == "reg") {
        CpuState state = emu.GetCpuStateSnapshot();
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0')
            << "EAX: " << std::setw(8) << state.eax << "  EBX: " << std::setw(8) << state.ebx
            << "  ECX: " << std::setw(8) << state.ecx << "  EDX: " << std::setw(8) << state.edx << "\n"
            << "ESI: " << std::setw(8) << state.esi << "  EDI: " << std::setw(8) << state.edi
            << "  ESP: " << std::setw(8) << state.esp << "  EBP: " << std::setw(8) << state.ebp << "\n"
            << "EIP: " << std::setw(8) << state.eip << "  EFLAGS: " << std::setw(8) << state.eflags
            << " [" << (state.FlagCF() ? "CF " : "") << (state.FlagZF() ? "ZF " : "")
            << (state.FlagSF() ? "SF " : "") << (state.FlagOF() ? "OF " : "") << "]\n"
            << "Instructions Executed: " << std::dec << state.instructionCount << "\n";
        WriteText(oss.str());
        return true;
    }

    if (cmd == "mem") {
        std::string addrHex;
        size_t count = 64;
        iss >> addrHex;
        if (addrHex.empty()) {
            WriteText("[CLI] Error: Usage is 'mem <hex_addr> [count]'\n");
            return true;
        }
        try {
            uint32_t addr = static_cast<uint32_t>(std::stoul(addrHex, nullptr, 16));
            if (iss >> count) {}
            count = std::min(count, size_t(256));

            std::vector<uint8_t> buf(count);
            if (emu.ReadMemory(addr, buf.data(), count)) {
                std::ostringstream oss;
                for (size_t i = 0; i < count; i += 16) {
                    oss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << (addr + i) << ": ";
                    for (size_t j = 0; j < 16; ++j) {
                        if (i + j < count) {
                            oss << std::setw(2) << static_cast<int>(buf[i + j]) << " ";
                        } else {
                            oss << "   ";
                        }
                    }
                    oss << " |";
                    for (size_t j = 0; j < 16 && i + j < count; ++j) {
                        uint8_t b = buf[i + j];
                        oss << (b >= 32 && b <= 126 ? static_cast<char>(b) : '.');
                    }
                    oss << "|\n";
                }
                WriteText(oss.str());
            } else {
                WriteText("[CLI] Failed to read memory at specified address.\n");
            }
        } catch (...) {
            WriteText("[CLI] Invalid hexadecimal address.\n");
        }
        return true;
    }

    if (cmd == "bp") {
        std::string addrHex;
        iss >> addrHex;
        if (addrHex.empty()) {
            WriteText("[CLI] Error: Usage is 'bp <hex_addr>'\n");
            return true;
        }
        try {
            uint32_t addr = static_cast<uint32_t>(std::stoul(addrHex, nullptr, 16));
            emu.ToggleBreakpoint(addr);
            WriteText("[CLI] Breakpoint toggled at 0x" + addrHex + ".\n");
        } catch (...) {
            WriteText("[CLI] Invalid address.\n");
        }
        return true;
    }

    // Not an emulator CLI command
    return false;
}

} // namespace em836
