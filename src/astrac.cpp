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
            return p;
        }
    }

    // Candidate search paths
    const std::vector<std::filesystem::path> candidates = {
        "../AstraC/build/Release/AstraC.exe",
        "../../AstraC/build/Release/AstraC.exe",
        "C:/Users/anton/source/repos/AstraC/build/Release/AstraC.exe",
        "./AstraC.exe",
        "build/Release/AstraC.exe"
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand)) {
            return std::filesystem::canonical(cand);
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

    std::string ext = inPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    std::string subCommand = "asm";
    if (ext == ".ac") {
        subCommand = "comp";
    }

    // Determine target binary output path
    std::filesystem::path binaryPath = inPath;
    binaryPath.replace_extension(".bin");

    // Remove existing stale binary if present
    if (std::filesystem::exists(binaryPath)) {
        std::error_code ec;
        std::filesystem::remove(binaryPath, ec);
    }

    std::string cmd = "\"" + astracExe.string() + "\" " + subCommand + " \"" + inPath.string() + "\"";
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
    } else {
        // Also check if AstraC emitted .BIN with uppercase
        std::filesystem::path upperBin = inPath;
        upperBin.replace_extension(".BIN");
        if (std::filesystem::exists(upperBin)) {
            result.success = true;
            result.binaryPath = upperBin.string();
        } else {
            result.success = false;
            result.errorMessage = "AstraC failed to generate binary output.";
        }
    }

    return result;
}

CompilationResult AstraCToolchain::CompileBuffer(const std::string& sourceCode, bool isAstraC, const std::string& customAstraCPath) {
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::filesystem::path tempSource = tempDir / (isAstraC ? "em836_buffer.ac" : "em836_buffer.as");

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

    // Reset CPU registers and memory before loading new payload
    emu.Reset();

    uint32_t loadAddress = 0x00000000;
    uint32_t entryPoint = 0x00000000;

    AcFileHeader header;
    if (ParseAcfhHeader(buffer.data(), buffer.size(), header)) {
        emu.GetConsole().AppendBuildLog("[Loader] Detected ACFH Executable Header.\n");
        entryPoint = header.entry_point;
        loadAddress = header.entry_point;

        // Load code segment
        if (header.code_offset + header.code_size <= buffer.size()) {
            emu.LoadBinary(buffer.data() + header.code_offset, header.code_size, loadAddress, entryPoint);
        } else {
            // Fallback: load full payload
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
