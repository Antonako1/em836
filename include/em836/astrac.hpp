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
 * @brief ACFH Binary Capability Flags (matching AstraC AC_FH.h)
 */
constexpr uint32_t AC_FLAG_NONE        = 0;
constexpr uint32_t AC_FLAG_EXECUTABLE  = 1 << 0; ///< ACFH Executable (.exe)
constexpr uint32_t AC_FLAG_DYNAMIC     = 1 << 1; ///< ACFH Dynamic Library (.lib)
constexpr uint32_t AC_FLAG_HAS_RELOCS  = 1 << 2; ///< Binary includes relocation table
constexpr uint32_t AC_FLAG_HAS_FUNCS   = 1 << 3; ///< Binary includes function export table
constexpr uint32_t AC_FLAG_HAS_IMPORTS = 1 << 4; ///< Binary includes import table

/**
 * @brief ACFH Function Attributes and Calling Convention
 */
constexpr uint16_t AC_FUNC_FLAG_CDECL    = 0 << 0; ///< cdecl ABI (caller cleans stack)
constexpr uint16_t AC_FUNC_FLAG_STDCALL  = 1 << 0; ///< stdcall ABI (callee cleans stack)
constexpr uint16_t AC_FUNC_FLAG_VARIADIC = 1 << 1; ///< Variadic function (...)

/**
 * @struct AcFileHeader
 * @brief ACFH 112-byte binary header produced by AstraC executables and libraries.
 * @see https://antonako.net/astrac/docs/fileheader.php
 */
#pragma pack(push, 1)
struct AcFileHeader {
    char magic[4];                  ///< "ACFH" magic signature
    uint32_t version;               ///< Format version (1.0 = 0x00010000)
    uint32_t flags;                 ///< Binary capability flags bitmask
    uint32_t entry_point_offset;    ///< Binary-relative offset to entry point (_start / _main), or 0xFFFFFFFF
    uint32_t code_offset;           ///< File offset to .code section (typically 112)
    uint32_t data_offset;           ///< File offset to .data section
    uint32_t rodata_offset;         ///< File offset to .rodata section
    uint32_t bss_offset;            ///< File offset to BSS section descriptor
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

/**
 * @struct AcFuncTableHdr
 * @brief Header preceding exported function records in ACFH binaries.
 */
struct AcFuncTableHdr {
    uint32_t entry_count;           ///< Number of exported function entries
    uint32_t string_table_size;     ///< Size of function string table in bytes
};

/**
 * @struct AcFuncEntry
 * @brief Individual exported function record (12 bytes).
 */
struct AcFuncEntry {
    uint32_t address;               ///< Function code-relative offset
    uint32_t name_offset;           ///< Byte offset into function string table pool
    uint16_t param_size;            ///< Total parameter size in bytes (e.g. 8 for 2 dwords)
    uint16_t flags;                 ///< Calling convention and function attributes
};

/**
 * @struct AcImportTableHdr
 * @brief Header preceding imported function records in ACFH binaries.
 */
struct AcImportTableHdr {
    uint32_t entry_count;           ///< Number of imported function entries
    uint32_t string_table_size;     ///< Size of import string table pool in bytes
};

/**
 * @struct AcImportEntry
 * @brief Individual imported function record (12 bytes).
 */
struct AcImportEntry {
    uint32_t lib_name_offset;       ///< Offset in string pool for library name (e.g. "math_lib.lib")
    uint32_t func_name_offset;      ///< Offset in string pool for function name (e.g. "lib_add")
    uint32_t patch_offset;          ///< File offset in code section to be patched with 32-bit rel/abs address
};
#pragma pack(pop)

static_assert(sizeof(AcFileHeader) == 112, "AcFileHeader must be exactly 112 bytes");
static_assert(sizeof(AcFuncTableHdr) == 8, "AcFuncTableHdr must be exactly 8 bytes");
static_assert(sizeof(AcFuncEntry) == 12, "AcFuncEntry must be exactly 12 bytes");
static_assert(sizeof(AcImportTableHdr) == 8, "AcImportTableHdr must be exactly 8 bytes");
static_assert(sizeof(AcImportEntry) == 12, "AcImportEntry must be exactly 12 bytes");

/**
 * @enum TargetFormat
 * @brief Binary output format requested from AstraC toolchain.
 */
enum class TargetFormat {
    RawBin = 0,     ///< Flat raw binary image (.bin)
    AcfhExe = 1,    ///< ACFH Executable (.exe)
    AcfhLib = 2     ///< ACFH Dynamic Library (.lib)
};

/**
 * @struct CompileOptions
 * @brief Compilation and table generation options passed to AstraC.
 */
struct CompileOptions {
    TargetFormat format = TargetFormat::RawBin;
    bool emitOffsetTable = true;    ///< Emit Relocation / Offset Table (ot)
    bool emitFuncTable = true;      ///< Emit Function Export Table (ft)
    bool emitImportTable = true;    ///< Emit Import Table (it)
};

/**
 * @struct AcfhExportSymbol
 * @brief Parsed function export entry with resolved names and addresses.
 */
struct AcfhExportSymbol {
    std::string name;
    uint32_t codeOffset = 0;
    uint32_t runtimeAddress = 0;
    uint16_t paramSize = 0;
    uint16_t flags = 0;
};

/**
 * @struct AcfhImportSymbol
 * @brief Parsed function import entry with patch location and resolution status.
 */
struct AcfhImportSymbol {
    std::string libName;
    std::string funcName;
    uint32_t filePatchOffset = 0;
    uint32_t codePatchOffset = 0;
    uint32_t runtimePatchAddress = 0;
    uint32_t resolvedAddress = 0;
    bool resolved = false;
};

/**
 * @struct AcfhParsedModule
 * @brief In-memory parsed representation of an ACFH executable or dynamic library.
 */
struct AcfhParsedModule {
    AcFileHeader header{};
    std::string filePath;
    std::string fileName;
    bool isLibrary = false;
    uint32_t baseAddress = 0x00000000;
    uint32_t totalMappedSize = 0;
    std::vector<uint32_t> relocations;
    std::vector<AcfhExportSymbol> exports;
    std::vector<AcfhImportSymbol> imports;
    std::vector<uint8_t> rawBuffer;
};

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
 * @struct DisassemblyResult
 * @brief Detailed report returned after invoking AstraC disassembler.
 */
struct DisassemblyResult {
    bool success = false;
    int exitCode = -1;
    std::string dsmPath = "";
    std::string dsmContent = "";
    std::vector<std::string> lines;
    std::string outputLog = "";
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
     */
    static std::filesystem::path FindAstraC(const std::string& customPath = "");

    /**
     * @brief Compiles or assembles a source file (.as, .asm, .ac) using AstraC.
     * @param inputFilePath Absolute or relative path to the source file.
     * @param customAstraCPath Optional user override for the AstraC executable path.
     * @param options Target binary format and table options.
     * @return CompilationResult containing exit status, build logs, and binary location.
     */
    static CompilationResult CompileFile(const std::string& inputFilePath,
                                        const std::string& customAstraCPath = "",
                                        const CompileOptions& options = {});

    /**
     * @brief Disassembles a binary or ACFH file using AstraC.exe disasm.
     * @param inputFilePath Absolute or relative path to the binary file (.bin, .exe, .lib).
     * @param customAstraCPath Optional user override for AstraC path.
     * @param bits 16 or 32 (forces instruction decoding mode).
     * @param org Origin address offset.
     * @param verbose Include verbose symbol listings.
     * @return DisassemblyResult containing exit status, DSM file path, content, and parsed lines.
     */
    static DisassemblyResult DisassembleFile(const std::string& inputFilePath,
                                           const std::string& customAstraCPath = "",
                                           int bits = 32,
                                           uint32_t org = 0,
                                           bool verbose = true);

    /**
     * @brief Writes a memory buffer of source code to a temporary file and compiles it via AstraC.
     * @param sourceCode Source text.
     * @param isAstraC True for Astra C (.ac), false for Assembly (.as).
     * @param customAstraCPath Optional override for AstraC path.
     * @param options Target binary format and table options.
     * @return CompilationResult.
     */
    static CompilationResult CompileBuffer(const std::string& sourceCode,
                                          bool isAstraC,
                                          const std::string& customAstraCPath = "",
                                          const CompileOptions& options = {});

    /**
     * @brief High-level pipeline: Ingests any supported file (.as, .ac, .bin, .exe, .lib),
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
     * @return True if buffer is an ACFH executable or library.
     */
    static bool ParseAcfhHeader(const uint8_t* buffer, size_t size, AcFileHeader& outHeader);

    /**
     * @brief Comprehensively parses an ACFH binary module including all section offsets,
     * relocation records, exported functions, and import patch locations.
     * @param buffer Raw binary buffer.
     * @param size Size in bytes.
     * @param outModule Destination parsed module.
     * @return True if successfully parsed as a valid ACFH binary.
     */
    static bool ParseAcfhModule(const uint8_t* buffer, size_t size, AcfhParsedModule& outModule);
};

} // namespace em836
