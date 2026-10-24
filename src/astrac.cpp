/**
 * @file astrac.cpp
 * @brief Implementation of External AstraC Toolchain Integration and File Ingestion Pipeline.
 */

#include "em836/astrac.hpp"
#include "em836/emulator.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace em836 {

std::filesystem::path AstraCToolchain::FindAstraC(const std::string& customPath) {
    // 1. User custom path
    if (!customPath.empty()) {
        std::filesystem::path p(customPath);
        if (std::filesystem::exists(p)) {
            return std::filesystem::weakly_canonical(std::filesystem::absolute(p));
        }
    }

    // Candidate search paths
    const std::vector<std::filesystem::path> candidates = {
        "../AstraC/build/Release/AstraC.exe",
        "../../AstraC/build/Release/AstraC.exe",
        "../../../AstraC/build/Release/AstraC.exe",
        "../../../../AstraC/build/Release/AstraC.exe",
        "C:/Users/anton/source/repos/AstraC/build/Release/AstraC.exe",
        "./AstraC.exe",
        "build/Release/AstraC.exe"
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand)) {
            return std::filesystem::weakly_canonical(std::filesystem::absolute(cand));
        }
    }

    return "";
}

bool AstraCToolchain::ParseAcfhHeader(const uint8_t* buffer, size_t size, AcFileHeader& outHeader) {
    if (!buffer || size < sizeof(AcFileHeader)) {
        return false;
    }

    if (std::memcmp(buffer, "ACFH", 4) == 0) {
        std::memcpy(&outHeader, buffer, sizeof(AcFileHeader));
        return true;
    }

    return false;
}

bool AstraCToolchain::ParseAcfhModule(const uint8_t* buffer, size_t size, AcfhParsedModule& outModule) {
    if (!buffer || size < sizeof(AcFileHeader)) {
        return false;
    }
    if (std::memcmp(buffer, "ACFH", 4) != 0) {
        return false;
    }

    outModule = AcfhParsedModule{};
    std::memcpy(&outModule.header, buffer, sizeof(AcFileHeader));
    outModule.isLibrary = (outModule.header.flags & AC_FLAG_DYNAMIC) != 0;
    outModule.rawBuffer.assign(buffer, buffer + size);

    // 1. Relocation Table
    if (outModule.header.reloc_offset != 0xFFFFFFFF && outModule.header.reloc_size > 0) {
        if (outModule.header.reloc_offset + outModule.header.reloc_size <= size) {
            size_t count = outModule.header.reloc_size / sizeof(uint32_t);
            outModule.relocations.resize(count);
            std::memcpy(outModule.relocations.data(), buffer + outModule.header.reloc_offset, count * sizeof(uint32_t));
        }
    }

    // 2. Function Export Table
    if (outModule.header.func_table_offset != 0xFFFFFFFF && outModule.header.func_table_size >= sizeof(AcFuncTableHdr)) {
        if (outModule.header.func_table_offset + outModule.header.func_table_size <= size) {
            const uint8_t* ftPtr = buffer + outModule.header.func_table_offset;
            AcFuncTableHdr fHdr;
            std::memcpy(&fHdr, ftPtr, sizeof(AcFuncTableHdr));

            size_t entriesBytes = fHdr.entry_count * sizeof(AcFuncEntry);
            if (sizeof(AcFuncTableHdr) + entriesBytes + fHdr.string_table_size <= outModule.header.func_table_size) {
                const AcFuncEntry* entries = reinterpret_cast<const AcFuncEntry*>(ftPtr + sizeof(AcFuncTableHdr));
                const char* strtab = reinterpret_cast<const char*>(ftPtr + sizeof(AcFuncTableHdr) + entriesBytes);

                for (uint32_t i = 0; i < fHdr.entry_count; ++i) {
                    AcfhExportSymbol exp;
                    if (entries[i].name_offset < fHdr.string_table_size) {
                        exp.name = strtab + entries[i].name_offset;
                    } else {
                        exp.name = "<unknown>";
                    }
                    exp.codeOffset = entries[i].address;
                    exp.runtimeAddress = entries[i].address;
                    exp.paramSize = entries[i].param_size;
                    exp.flags = entries[i].flags;
                    outModule.exports.push_back(exp);
                }
            }
        }
    }

    // 3. Import Table
    if (outModule.header.import_table_offset != 0xFFFFFFFF && outModule.header.import_table_size >= sizeof(AcImportTableHdr)) {
        if (outModule.header.import_table_offset + outModule.header.import_table_size <= size) {
            const uint8_t* itPtr = buffer + outModule.header.import_table_offset;
            AcImportTableHdr iHdr;
            std::memcpy(&iHdr, itPtr, sizeof(AcImportTableHdr));

            size_t entriesBytes = iHdr.entry_count * sizeof(AcImportEntry);
            if (sizeof(AcImportTableHdr) + entriesBytes + iHdr.string_table_size <= outModule.header.import_table_size) {
                const AcImportEntry* entries = reinterpret_cast<const AcImportEntry*>(itPtr + sizeof(AcImportTableHdr));
                const char* strtab = reinterpret_cast<const char*>(itPtr + sizeof(AcImportTableHdr) + entriesBytes);

                for (uint32_t i = 0; i < iHdr.entry_count; ++i) {
                    AcfhImportSymbol imp;
                    if (entries[i].lib_name_offset < iHdr.string_table_size) {
                        imp.libName = strtab + entries[i].lib_name_offset;
                    }
                    if (entries[i].func_name_offset < iHdr.string_table_size) {
                        imp.funcName = strtab + entries[i].func_name_offset;
                    }
                    imp.filePatchOffset = entries[i].patch_offset;
                    imp.codePatchOffset = (entries[i].patch_offset >= outModule.header.code_offset)
                        ? (entries[i].patch_offset - outModule.header.code_offset)
                        : entries[i].patch_offset;
                    imp.runtimePatchAddress = 0;
                    imp.resolvedAddress = 0;
                    imp.resolved = false;
                    outModule.imports.push_back(imp);
                }
            }
        }
    }

    return true;
}

#ifdef _WIN32
static bool RunProcessWithOutput(const std::string& cmdLine, const std::string& workingDir, std::string& outOutput, int& outExitCode) {
    HANDLE hChildStdOutRead = NULL;
    HANDLE hChildStdOutWrite = NULL;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &saAttr, 0)) {
        return false;
    }

    if (!SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hChildStdOutRead);
        CloseHandle(hChildStdOutWrite);
        return false;
    }

    PROCESS_INFORMATION piProcInfo;
    STARTUPINFOA siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
    siStartInfo.cb = sizeof(STARTUPINFOA);
    siStartInfo.hStdError = hChildStdOutWrite;
    siStartInfo.hStdOutput = hChildStdOutWrite;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back('\0');

    const char* workDirPtr = workingDir.empty() ? NULL : workingDir.c_str();

    BOOL bSuccess = CreateProcessA(
        NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        workDirPtr,
        &siStartInfo,
        &piProcInfo
    );

    // Close write end of pipe in parent so ReadFile unblocks when child terminates
    CloseHandle(hChildStdOutWrite);

    if (!bSuccess) {
        CloseHandle(hChildStdOutRead);
        outOutput = "Failed to launch process: " + cmdLine;
        outExitCode = -1;
        return false;
    }

    // Read output from child process
    DWORD dwRead;
    CHAR chBuf[4096];
    outOutput.clear();

    while (ReadFile(hChildStdOutRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead != 0) {
        chBuf[dwRead] = '\0';
        outOutput.append(chBuf, dwRead);
    }

    WaitForSingleObject(piProcInfo.hProcess, 10000); // 10 second timeout

    DWORD exitCode = 0;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    outExitCode = static_cast<int>(exitCode);

    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);
    CloseHandle(hChildStdOutRead);

    return true;
}
#endif

CompilationResult AstraCToolchain::CompileFile(const std::string& inputFilePath,
                                              const std::string& customAstraCPath,
                                              const CompileOptions& options) {
    CompilationResult result;
    std::filesystem::path astracExe = FindAstraC(customAstraCPath);

    if (astracExe.empty()) {
        result.success = false;
        result.errorMessage = "AstraC compiler binary not found. Please set AstraC path in Settings.";
        return result;
    }

    std::filesystem::path inPath(inputFilePath);
    if (!std::filesystem::exists(inPath)) {
        result.success = false;
        result.errorMessage = "Input file does not exist: " + inputFilePath;
        return result;
    }

    // Resolve to an absolute canonical path so setting workDir to inPath.parent_path()
    // never invalidates a relative input path argument passed to AstraC.exe.
    inPath = std::filesystem::weakly_canonical(std::filesystem::absolute(inPath));

    std::string ext = inPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    std::string stemLower = inPath.stem().string();
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), [](unsigned char c) { return std::tolower(c); });

    bool is16Bit = (stemLower.rfind("16bit", 0) == 0);
    if (!is16Bit) {
        std::ifstream srcCheck(inPath);
        if (srcCheck.is_open()) {
            std::string srcContent((std::istreambuf_iterator<char>(srcCheck)), std::istreambuf_iterator<char>());
            std::string srcLower = srcContent;
            std::transform(srcLower.begin(), srcLower.end(), srcLower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (srcLower.find(".use16") != std::string::npos) {
                is16Bit = true;
            }
        }
    }

    std::string subCommand = "asm";
    if (ext == ".ac") {
        subCommand = "comp";
    }

    // Determine target binary output paths
    std::filesystem::path binPathLower = inPath;
    binPathLower.replace_extension(".bin");
    std::filesystem::path binPathUpper = inPath;
    binPathUpper.replace_extension(".BIN");
    std::filesystem::path exePath = inPath;
    exePath.replace_extension(".exe");
    std::filesystem::path libPath = inPath;
    libPath.replace_extension(".lib");

    // Remove existing stale binaries if present
    std::error_code ec;
    if (std::filesystem::exists(binPathLower)) std::filesystem::remove(binPathLower, ec);
    if (std::filesystem::exists(binPathUpper)) std::filesystem::remove(binPathUpper, ec);
    if (options.format == TargetFormat::AcfhExe && std::filesystem::exists(exePath)) std::filesystem::remove(exePath, ec);
    if (options.format == TargetFormat::AcfhLib && std::filesystem::exists(libPath)) std::filesystem::remove(libPath, ec);

    std::string cmd = "\"" + astracExe.string() + "\" " + subCommand + " \"" + inPath.string() + "\"";
    if (is16Bit) {
        cmd += " arch i286 bits 16";
    }

    if (options.format == TargetFormat::AcfhExe) {
        cmd += " type exe";
        if (options.emitOffsetTable) cmd += " ot";
        if (options.emitFuncTable)   cmd += " ft";
        if (options.emitImportTable) cmd += " it";
    } else if (options.format == TargetFormat::AcfhLib) {
        cmd += " type lib";
        if (options.emitOffsetTable) cmd += " ot";
        if (options.emitFuncTable)   cmd += " ft";
        if (options.emitImportTable) cmd += " it";
    }

    std::string workDir = inPath.parent_path().string();

    int exitCode = -1;
    std::string output;
#ifdef _WIN32
    bool ok = RunProcessWithOutput(cmd, workDir, output, exitCode);
#else
    bool ok = false;
#endif

    result.outputLog = output;
    result.exitCode = exitCode;

    // Identify which raw file AstraC generated (.BIN or .bin)
    std::filesystem::path rawProduced;
    if (std::filesystem::exists(binPathUpper)) {
        rawProduced = binPathUpper;
    } else if (std::filesystem::exists(binPathLower)) {
        rawProduced = binPathLower;
    }

    if (ok && !rawProduced.empty()) {
        result.success = true;

        if (options.format == TargetFormat::AcfhExe) {
            std::filesystem::copy_file(rawProduced, exePath, std::filesystem::copy_options::overwrite_existing, ec);
            result.binaryPath = exePath.string();
        } else if (options.format == TargetFormat::AcfhLib) {
            std::filesystem::copy_file(rawProduced, libPath, std::filesystem::copy_options::overwrite_existing, ec);
            result.binaryPath = libPath.string();

            // Also copy .lib to examples/ if compiled elsewhere (or if in a subdirectory)
            // so any #import directives in example files can locate it easily
            std::filesystem::path examplesDir = "examples";
            if (std::filesystem::exists(examplesDir) && std::filesystem::is_directory(examplesDir)) {
                std::filesystem::path exampleLib = examplesDir / libPath.filename();
                if (exampleLib != libPath) {
                    std::filesystem::copy_file(libPath, exampleLib, std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        } else {
            result.binaryPath = rawProduced.string();
        }
    } else {
        result.success = false;
        result.errorMessage = "AstraC failed to generate binary output.";
    }

    return result;
}

DisassemblyResult AstraCToolchain::DisassembleFile(const std::string& inputFilePath,
                                                 const std::string& customAstraCPath,
                                                 int bits,
                                                 uint32_t org,
                                                 bool verbose) {
    DisassemblyResult result;
    std::filesystem::path astracExe = FindAstraC(customAstraCPath);
    if (astracExe.empty()) {
        result.success = false;
        result.errorMessage = "AstraC compiler binary not found. Please set AstraC path in Settings.";
        return result;
    }

    std::filesystem::path inPath(inputFilePath);
    if (!std::filesystem::exists(inPath)) {
        result.success = false;
        result.errorMessage = "Target file does not exist: " + inputFilePath;
        return result;
    }

    inPath = std::filesystem::weakly_canonical(std::filesystem::absolute(inPath));
    std::string workDir = inPath.parent_path().string();

    std::filesystem::path dsmPath = inPath;
    dsmPath.replace_extension(".DSM");

    // Remove existing stale .DSM if present
    std::error_code ec;
    if (std::filesystem::exists(dsmPath)) {
        std::filesystem::remove(dsmPath, ec);
    }

    // Build command line: AstraC.exe disasm <file> bits <16|32> [org <hex>] [verbose]
    std::string cmd = "\"" + astracExe.string() + "\" disasm \"" + inPath.string() + "\"";
    cmd += " bits " + std::to_string(bits == 16 ? 16 : 32);
    if (org > 0) {
        std::stringstream ss;
        ss << " org 0x" << std::hex << org;
        cmd += ss.str();
    }
    if (verbose) {
        cmd += " verbose";
    }

    int exitCode = -1;
    std::string output;
#ifdef _WIN32
    bool ok = RunProcessWithOutput(cmd, workDir, output, exitCode);
#else
    bool ok = false;
#endif

    result.outputLog = output;
    result.exitCode = exitCode;

    // Check if .DSM or .dsm was produced
    std::filesystem::path dsmLower = inPath;
    dsmLower.replace_extension(".dsm");

    std::filesystem::path foundDsm;
    if (std::filesystem::exists(dsmPath)) {
        foundDsm = dsmPath;
    } else if (std::filesystem::exists(dsmLower)) {
        foundDsm = dsmLower;
    }

    if (ok && !foundDsm.empty()) {
        result.success = true;
        result.dsmPath = foundDsm.string();

        std::ifstream dsmFile(foundDsm);
        if (dsmFile.is_open()) {
            std::string line;
            while (std::getline(dsmFile, line)) {
                result.lines.push_back(line);
                result.dsmContent += line + "\n";
            }
        }
    } else {
        result.success = false;
        result.errorMessage = "AstraC failed to disassemble file or generate .DSM output.";
    }

    return result;
}

CompilationResult AstraCToolchain::CompileBuffer(const std::string& sourceCode,
                                                bool isAstraC,
                                                const std::string& customAstraCPath,
                                                const CompileOptions& options) {
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::string srcLower = sourceCode;
    std::transform(srcLower.begin(), srcLower.end(), srcLower.begin(), [](unsigned char c) { return std::tolower(c); });
    bool is16Bit = (srcLower.find(".use16") != std::string::npos);

    std::string prefix = "em836_buffer";
    if (options.format == TargetFormat::AcfhLib) {
        prefix = "em836_lib";
    }
    std::string baseName = is16Bit ? ("16bit_" + prefix) : prefix;
    std::filesystem::path tempSource = tempDir / (baseName + (isAstraC ? ".ac" : ".as"));

    std::ofstream file(tempSource);
    if (!file.is_open()) {
        CompilationResult res;
        res.success = false;
        res.errorMessage = "Failed to write source buffer to temporary file: " + tempSource.string();
        return res;
    }

    file << sourceCode;
    file.close();

    return CompileFile(tempSource.string(), customAstraCPath, options);
}

bool AstraCToolchain::IngestFile(const std::string& filePath, Emulator& emu, const std::string& customAstraCPath) {
    std::filesystem::path p(filePath);
    if (!std::filesystem::exists(p)) {
        emu.GetConsole().AppendBuildLog("[Error] File does not exist: " + filePath + "\n");
        return false;
    }

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    std::string stemLower = p.stem().string();
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), [](unsigned char c) { return std::tolower(c); });
    bool is16BitFile = (stemLower.rfind("16bit", 0) == 0);

    emu.Pause();

    // Source compilation (.as, .asm, .ac)
    if (ext == ".as" || ext == ".asm" || ext == ".ac") {
        emu.GetConsole().AppendBuildLog("[AstraC] Building: " + p.filename().string() + " ...\n");

        CompileOptions opts;
        opts.format = TargetFormat::RawBin;
        if (stemLower.rfind("_lib") != std::string::npos || stemLower.find("lib_") != std::string::npos) {
            opts.format = TargetFormat::AcfhLib;
        } else if (stemLower.rfind("_exe") != std::string::npos || stemLower.find("exe_") != std::string::npos) {
            opts.format = TargetFormat::AcfhExe;
        } else if (ext == ".ac") {
            // Check if source contains #import
            std::ifstream testF(p);
            if (testF.is_open()) {
                std::string s((std::istreambuf_iterator<char>(testF)), std::istreambuf_iterator<char>());
                if (s.find("#import") != std::string::npos || s.find(".import") != std::string::npos) {
                    opts.format = TargetFormat::AcfhExe;
                }
            }
        }

        CompilationResult res = CompileFile(filePath, customAstraCPath, opts);
        emu.GetConsole().AppendBuildLog(res.outputLog + "\n");

        if (!res.success) {
            emu.GetConsole().AppendBuildLog("[AstraC] Build failed: " + res.errorMessage + "\n");
            return false;
        }

        emu.GetConsole().AppendBuildLog("[AstraC] Build succeeded. Output: " + res.binaryPath + "\n");
        return IngestFile(res.binaryPath, emu, customAstraCPath);
    }

    // Direct dynamic library ingestion (.lib)
    if (ext == ".lib") {
        return emu.LoadAcfhLibraryFile(filePath);
    }

    // Binary file ingestion (.bin, .exe)
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        emu.GetConsole().AppendBuildLog("[Error] Cannot open binary file: " + filePath + "\n");
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) {
        emu.GetConsole().AppendBuildLog("[Error] Binary file is empty.\n");
        return false;
    }

    // Switch CPU mode (16-bit Real Mode vs 32-bit Protected Mode) if needed before loading
    uint32_t targetBits = is16BitFile ? 16 : 32;
    if (emu.GetCpuMode() != targetBits) {
        emu.SetCpuMode(targetBits);
        emu.GetConsole().WriteText(std::string("[System] Switched CPU Mode to ") +
            (targetBits == 16 ? "16-bit Real Mode (i286).\n" : "32-bit Protected Mode (i386).\n"));
    }

    AcfhParsedModule parsedMod;
    if (ParseAcfhModule(buffer.data(), buffer.size(), parsedMod)) {
        if (parsedMod.isLibrary) {
            return emu.LoadAcfhLibrary(filePath, parsedMod);
        } else {
            return emu.LoadAcfhExecutable(filePath, parsedMod);
        }
    }

    // Raw flat binary (.bin)
    emu.Reset();
    uint32_t loadAddress = 0x00000000;
    uint32_t entryPoint = 0x00000000;
    emu.GetConsole().AppendBuildLog("[Loader] Loading raw binary at origin 0x00000000.\n");
    emu.LoadBinary(buffer.data(), buffer.size(), loadAddress, entryPoint);

    emu.GetConfig().lastLoadedFile = filePath;
    ConfigManager::Save(emu.GetConfig());

    emu.GetConsole().WriteText("[System] Loaded '" + p.filename().string() + "'. EIP set to 0x00000000.\n");
    return true;
}

} // namespace em836
