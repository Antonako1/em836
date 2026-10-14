/**
 * @file console.hpp
 * @brief Virtual Terminal Console, Guest Input Queue, and Emulator Command Interpreter.
 *
 * This header defines the Console subsystem for em836.
 * It provides:
 * 1. Output Terminal Buffer: Thread-safe circular logging buffer storing output emitted by
 *    guest programs via custom syscalls (SYS_PRINT_CHAR, SYS_PRINT_STR), legacy BIOS/DOS interrupts
 *    (INT 0x10 AH=0x0E, INT 0x21 AH=0x02/0x09), or COM1 UART port writes (OUT 0x3F8, AL).
 * 2. Guest Input Stream: Thread-safe FIFO character queue allowing user interactive input from
 *    the console prompt to be read by guest programs via SYS_READ_CHAR (INT 0x88, EAX=0x12).
 * 3. Command Interpreter (CLI): Evaluates commands entered by the user in the console input prompt
 *    (e.g., "help", "run", "pause", "step", "reset", "cls", "load <file>", "speed <hz>", "mode <text|gfx>").
 * 4. AstraC Build Log: Dedicated buffer capturing stdout/stderr from external AstraC compiler executions.
 */

#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>

namespace em836 {

// Forward declaration
class Emulator;

/**
 * @class ConsoleSubsystem
 * @brief Manages terminal logging, guest keystroke queuing, and interactive emulator CLI commands.
 */
class ConsoleSubsystem {
public:
    ConsoleSubsystem();
    ~ConsoleSubsystem() = default;

    // -------------------------------------------------------------
    // Terminal Output Handling
    // -------------------------------------------------------------

    /**
     * @brief Appends a single character to the virtual serial terminal buffer.
     * @param ch Character byte.
     */
    void WriteChar(char ch);

    /**
     * @brief Appends a formatted string to the terminal output buffer.
     * @param text Text string to append.
     */
    void WriteText(const std::string& text);

    /**
     * @brief Clears the terminal output buffer.
     */
    void Clear();

    /**
     * @brief Retrieves the full terminal output buffer text.
     * @return String copy of current terminal contents.
     */
    std::string GetOutputText() const;

    // -------------------------------------------------------------
    // Guest Input Handling
    // -------------------------------------------------------------

    /**
     * @brief Enqueues a character into the guest input stream (consumed by SYS_READ_CHAR).
     * @param ch ASCII character.
     */
    void EnqueueInputChar(char ch);

    /**
     * @brief Enqueues an entire string followed by newline into the guest input stream.
     * @param str Input string.
     */
    void EnqueueInputString(const std::string& str);

    /**
     * @brief Attempts to dequeue a character from the guest input stream.
     * @param outChar Reference where the dequeued character is stored.
     * @return True if a character was dequeued, false if queue is empty.
     */
    bool DequeueInputChar(char& outChar);

    /**
     * @brief Returns true if characters are pending in the guest input queue.
     */
    bool HasInputChar() const;

    // -------------------------------------------------------------
    // CLI Command Execution
    // -------------------------------------------------------------

    /**
     * @brief Processes an input line entered by the user in the console prompt.
     *        If the line begins with a recognized emulator command (e.g., 'help', 'run', 'step'),
     *        it executes the command on the emulator. Otherwise, it forwards the line to the guest input queue.
     * @param line Input text string from prompt.
     * @param emu Reference to the active Emulator engine.
     */
    void ProcessInputLine(const std::string& line, Emulator& emu);

    // -------------------------------------------------------------
    // AstraC Toolchain Build Log
    // -------------------------------------------------------------

    /**
     * @brief Appends compiler output to the AstraC Build Log buffer.
     * @param text Build messages, warnings, or error strings.
     */
    void AppendBuildLog(const std::string& text);

    /**
     * @brief Clears the AstraC Build Log buffer.
     */
    void ClearBuildLog();

    /**
     * @brief Gets current contents of the AstraC Build Log buffer.
     */
    std::string GetBuildLog() const;

    // -------------------------------------------------------------
    // UI Helpers
    // -------------------------------------------------------------
    bool GetAutoScroll() const { return m_autoScroll; }
    void SetAutoScroll(bool enable) { m_autoScroll = enable; }

private:
    mutable std::mutex m_mutex;

    // Terminal output storage
    std::string m_outputText;
    size_t m_maxOutputLength = 100000; // Limit buffer length to prevent runaway memory usage

    // Guest input queue
    std::queue<char> m_inputQueue;

    // AstraC Build Log storage
    std::string m_buildLogText;

    // Auto-scroll toggle for ImGui window
    bool m_autoScroll = true;

    // Internal command dispatch
    bool ExecuteCommand(const std::string& cmdLine, Emulator& emu);
};

} // namespace em836
