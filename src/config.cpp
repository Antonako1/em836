/**
 * @file config.cpp
 * @brief Implementation of persistent configuration file serialization and loading.
 */

#include "em836/config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace em836 {

std::filesystem::path ConfigManager::GetDefaultConfigPath() {
    return std::filesystem::current_path() / "config.json";
}

// Lightweight JSON helper to trim strings
static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Lightweight JSON helper to parse simple string values
static bool ExtractJsonString(const std::string& json, const std::string& key, std::string& outValue) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;

    size_t colon = json.find(':', pos + needle.length());
    if (colon == std::string::npos) return false;

    size_t firstQuote = json.find('\"', colon + 1);
    if (firstQuote == std::string::npos) return false;

    size_t secondQuote = json.find('\"', firstQuote + 1);
    if (secondQuote == std::string::npos) return false;

    outValue = json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    return true;
}

// Lightweight JSON helper to parse integer values
static bool ExtractJsonNumber(const std::string& json, const std::string& key, uint32_t& outValue) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;

    size_t colon = json.find(':', pos + needle.length());
    if (colon == std::string::npos) return false;

    size_t start = json.find_first_of("0123456789", colon + 1);
    if (start == std::string::npos) return false;

    size_t end = json.find_first_not_of("0123456789", start);
    std::string numStr = json.substr(start, end == std::string::npos ? std::string::npos : end - start);
    
    try {
        outValue = static_cast<uint32_t>(std::stoul(numStr));
        return true;
    } catch (...) {
        return false;
    }
}

EmulatorConfig ConfigManager::Load(const std::filesystem::path& path) {
    EmulatorConfig config;

    if (!std::filesystem::exists(path)) {
        // Return default configuration if file does not exist yet
        return config;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    std::string astracPath;
    if (ExtractJsonString(json, "astracPath", astracPath)) {
        config.astracPath = astracPath;
    }

    uint32_t memSize = 0;
    if (ExtractJsonNumber(json, "memorySize", memSize) && memSize >= 1024 * 1024) {
        config.memorySize = memSize;
    }

    uint32_t clockHz = 0;
    if (ExtractJsonNumber(json, "clockSpeedHz", clockHz)) {
        config.clockSpeedHz = clockHz;
    }

    uint32_t vidMode = 0;
    if (ExtractJsonNumber(json, "defaultVideoMode", vidMode)) {
        config.defaultVideoMode = (vidMode == 1) ? VideoMode::Graphics640x480 : VideoMode::Text80x25;
    }

    uint32_t cpuMode = 0;
    if (ExtractJsonNumber(json, "cpuModeBits", cpuMode) && (cpuMode == 16 || cpuMode == 32)) {
        config.cpuModeBits = cpuMode;
    }

    uint32_t entryAddr = 0;
    if (ExtractJsonNumber(json, "defaultEntryAddress", entryAddr)) {
        config.defaultEntryAddress = entryAddr;
    }

    uint32_t stackAddr = 0;
    if (ExtractJsonNumber(json, "defaultStackAddress", stackAddr)) {
        config.defaultStackAddress = stackAddr;
    }

    std::string lastFile;
    if (ExtractJsonString(json, "lastLoadedFile", lastFile)) {
        config.lastLoadedFile = lastFile;
    }

    std::string lastDir;
    if (ExtractJsonString(json, "lastWorkingDirectory", lastDir)) {
        config.lastWorkingDirectory = lastDir;
    }

    return config;
}

bool ConfigManager::Save(const EmulatorConfig& config, const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"astracPath\": \"" << config.astracPath << "\",\n";
    file << "  \"memorySize\": " << config.memorySize << ",\n";
    file << "  \"clockSpeedHz\": " << config.clockSpeedHz << ",\n";
    file << "  \"defaultVideoMode\": " << (config.defaultVideoMode == VideoMode::Graphics640x480 ? 1 : 0) << ",\n";
    file << "  \"cpuModeBits\": " << config.cpuModeBits << ",\n";
    file << "  \"defaultEntryAddress\": " << config.defaultEntryAddress << ",\n";
    file << "  \"defaultStackAddress\": " << config.defaultStackAddress << ",\n";
    file << "  \"lastLoadedFile\": \"" << config.lastLoadedFile << "\",\n";
    file << "  \"lastWorkingDirectory\": \"" << config.lastWorkingDirectory << "\"\n";
    file << "}\n";

    return true;
}

} // namespace em836
