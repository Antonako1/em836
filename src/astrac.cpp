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

CompilationResult AstraCToolchain::CompileFile(const std::string& inputFilePath, const std::string& customAstraCPath) {
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

    // Determine target binary output path
    std::filesystem::path binaryPath = inPath;
    binaryPath.replace_extension(".bin");
    std::filesystem::path upperBin = inPath;
    upperBin.replace_extension(".BIN");

    // Remove existing stale binaries if present
    if (std::filesystem::exists(binaryPath)) {
        std::error_code ec;
        std::filesystem::remove(binaryPath, ec);
    }
    if (std::filesystem::exists(upperBin)) {
        std::error_code ec;
        std::filesystem::remove(upperBin, ec);
    }

    std::string cmd = "\"" + astracExe.string() + "\" " + subCommand + " \"" + inPath.string() + "\"";
    if (is16Bit) {
        cmd += " arch i286 bits 16";
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

    // Check if output binary was produced
    if (ok && std::filesystem::exists(binaryPath)) {
        result.success = true;
        result.binaryPath = binaryPath.string();
    } else if (ok && std::filesystem::exists(upperBin)) {
        result.success = true;
        result.binaryPath = upperBin.string();
    } else {
        result.success = false;
        result.errorMessage = "AstraC failed to generate binary output.";
    }

    return result;
}

CompilationResult AstraCToolchain::CompileBuffer(const std::string& sourceCode, bool isAstraC, const std::string& customAstraCPath) {
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::string srcLower = sourceCode;
    std::transform(srcLower.begin(), srcLower.end(), srcLower.begin(), [](unsigned char c) { return std::tolower(c); });
    bool is16Bit = (srcLower.find(".use16") != std::string::npos);

    std::string baseName = is16Bit ? "16bit_em836_buffer" : "em836_buffer";
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

    return CompileFile(tempSource.string(), customAstraCPath);
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

    if (ext == ".as" || ext == ".asm" || ext == ".ac") {
        emu.GetConsole().AppendBuildLog("[AstraC] Building: " + p.filename().string() + " ...\n");
        CompilationResult res = CompileFile(filePath, customAstraCPath);
        emu.GetConsole().AppendBuildLog(res.outputLog + "\n");

        if (!res.success) {
            emu.GetConsole().AppendBuildLog("[AstraC] Build failed: " + res.errorMessage + "\n");
            return false;
        }

        emu.GetConsole().AppendBuildLog("[AstraC] Build succeeded. Output: " + res.binaryPath + "\n");
        return IngestFile(res.binaryPath, emu, customAstraCPath);
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

    // Reset CPU registers and memory before loading new payload
    emu.Reset();

    uint32_t loadAddress = 0x00000000;
    uint32_t entryPoint = 0x00000000;

    AcFileHeader header;
    if (ParseAcfhHeader(buffer.data(), buffer.size(), header)) {
        emu.GetConsole().AppendBuildLog("[Loader] Detected 112-byte ACFH Executable Header.\n");
        loadAddress = 0x00000000;
        entryPoint = loadAddress + header.entry_point_offset;

        // Load contiguous payload starting at code_offset (.code + .data + .rodata)
        if (header.code_offset < buffer.size()) {
            size_t payloadSize = buffer.size() - header.code_offset;
            emu.LoadBinary(buffer.data() + header.code_offset, payloadSize, loadAddress, entryPoint);
        } else {
            // Fallback: load full payload after 108-byte ACFH header
            emu.LoadBinary(buffer.data() + sizeof(AcFileHeader), buffer.size() - sizeof(AcFileHeader), loadAddress, entryPoint);
        }
    } else {
        // Raw flat binary (.bin)
        emu.GetConsole().AppendBuildLog("[Loader] Loading raw binary at origin 0x00000000.\n");
        emu.LoadBinary(buffer.data(), buffer.size(), loadAddress, entryPoint);
    }

    emu.GetConfig().lastLoadedFile = filePath;
    ConfigManager::Save(emu.GetConfig());

    emu.GetConsole().WriteText("[System] Loaded '" + p.filename().string() + "'. EIP set to 0x" + std::to_string(entryPoint) + ".\n");
    return true;
}

} // namespace em836
