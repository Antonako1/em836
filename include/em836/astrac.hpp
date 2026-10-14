/**
 * @file astrac.hpp
 * @brief External AstraC Toolchain Integration and File Ingestion Pipeline.
 *
 * This header defines the build automation and file ingestion interface for em836.
 *
 * AstraC is an x86 assembler, disassembler, and C-like compiler. In em836:
 * - Assembly files (.as, .asm) are assembled via: `AstraC.exe asm <input>`
 * - Astra C source files (.ac) are compiled via: `AstraC.exe comp <input>`
 * - Both produce `.bin` binary outputs that are loaded directly into em836's virtual RAM.
 * - Raw binaries (.bin) are loaded directly.
 * - ACFH executables (.exe) are parsed according to the 112-byte header specification
 *   to determine the exact entry point and code segment layout.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

namespace em836 {

// Forward declaration
class Emulator;

/**
 * @struct AcFileHeader
 * @brief ACFH 112-byte binary header produced by AstraC executables.
 */
#pragma pack(push, 1)
struct AcFileHeader {
    char magic[4];             ///< Must be "ACFH"
    uint16_t version_major;    ///< Version major
    uint16_t version_minor;    ///< Version minor
    uint32_t flags;            ///< Header flags
    uint32_t entry_point;      ///< Execution entry point address/offset
    uint32_t code_offset;      ///< File offset of code segment
    uint32_t code_size;        ///< Size in bytes of code segment
    uint32_t data_offset;      ///< File offset of data segment
    uint32_t data_size;        ///< Size in bytes of data segment
    uint32_t bss_size;         ///< Size in bytes of uninitialized BSS
    uint8_t reserved[76];      ///< Reserved padding up to 112 bytes
};
#pragma pack(pop)

static_assert(sizeof(AcFileHeader) == 112, "AcFileHeader must be exactly 112 bytes");

/**
 * @struct CompilationResult
 * @brief Detailed report returned after invoking AstraC.
 */
struct CompilationResult {
    bool success = false;
    int exitCode = -1;
    std::string outputLog = "";
    std::string binaryPath = "";
    uint32_t entryPoint = 0x00000000;
    uint32_t loadAddress = 0x00000000;
    std::string errorMessage = "";
};

/**
 * @class AstraCToolchain
 * @brief Manages discovering, invoking, and ingesting AstraC source and binary files.
 */
class AstraCToolchain {
public:
    /**
     * @brief Locates the AstraC executable on the host system.
     * Searches in order:
     * 1. User custom path in settings
     * 2. Sibling paths (../AstraC/build/Release/AstraC.exe, etc.)
     * 3. Repos directory (C:/Users/anton/source/repos/AstraC/build/Release/AstraC.exe)
     * 4. Current directory
     * 5. System PATH
     * @param customPath User-configured path (may be empty).
     * @return Path to AstraC executable, or empty path if not found.
     */
    static std::filesystem::path FindAstraC(const std::string& customPath = "");

    /**
     * @brief Compiles or assembles a source file (.as, .asm, .ac) using AstraC.
     * @param inputFilePath Absolute or relative path to the source file.
     * @param customAstraCPath Optional user override for the AstraC executable path.
     * @return CompilationResult containing exit status, build logs, and binary location.
     */
    static CompilationResult CompileFile(const std::string& inputFilePath, const std::string& customAstraCPath = "");

    /**
     * @brief Writes a memory buffer of source code to a temporary file and compiles it via AstraC.
     * @param sourceCode Source text.
     * @param isAstraC True for Astra C (.ac), false for Assembly (.as).
     * @param customAstraCPath Optional override for AstraC path.
     * @return CompilationResult.
     */
    static CompilationResult CompileBuffer(const std::string& sourceCode, bool isAstraC, const std::string& customAstraCPath = "");

    /**
     * @brief High-level pipeline: Ingests any supported file (.as, .ac, .bin, .exe),
     * compiles if necessary, halts existing VM execution, resets state, and loads the binary.
     * @param filePath Path to file dropped or opened.
     * @param emu Active Emulator engine.
     * @param customAstraCPath Optional AstraC path.
     * @return True if ingested and loaded successfully, false otherwise.
     */
    static bool IngestFile(const std::string& filePath, Emulator& emu, const std::string& customAstraCPath = "");

    /**
     * @brief Checks if a binary buffer begins with the 112-byte "ACFH" header.
     * If valid, extracts entry point and segment parameters.
     * @param buffer Raw binary file contents.
     * @param size Size in bytes.
     * @param outHeader Destination struct receiving parsed header.
     * @return True if buffer is an ACFH executable.
     */
    static bool ParseAcfhHeader(const uint8_t* buffer, size_t size, AcFileHeader& outHeader);
};

} // namespace em836
