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
 * @brief ACFH 108-byte binary header produced by AstraC executables and libraries.
 * @see https://antonako.net/astrac/docs/fileheader.php
 */
#pragma pack(push, 1)
struct AcFileHeader {
    char magic[4];                  ///< "ACFH" magic signature
    uint32_t version;               ///< Format version (1.0 = 0x00010000)
    uint32_t flags;                 ///< Binary capability flags bitmask
    uint32_t entry_point_offset;    ///< Binary-relative offset to entry point (_start / _main)
    uint32_t code_offset;           ///< File offset to .code section
    uint32_t data_offset;           ///< File offset to .data section
    uint32_t rodata_offset;         ///< File offset to .rodata section
    uint32_t bss_offset;            ///< File offset to BSS section
    uint32_t reloc_offset;          ///< File offset to Relocation Table
    uint32_t func_table_offset;     ///< File offset to Function Export Table
    uint32_t import_table_offset;   ///< File offset to Import Table
    uint32_t code_size;             ///< Size of .code section in bytes
    uint32_t data_size;             ///< Size of .data section in bytes
    uint32_t rodata_size;           ///< Size of .rodata section in bytes
    uint32_t bss_size;              ///< Uninitialized BSS size in bytes (memory-only)
    uint32_t reloc_size;            ///< Size of Relocation Table in bytes
    uint32_t func_table_size;       ///< Size of Function Export Table section in bytes
    uint32_t import_table_size;     ///< Size of Import Table section in bytes
    uint8_t reserved[40];           ///< Reserved bytes (112 bytes total)
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
