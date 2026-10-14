/**
 * @file config.hpp
 * @brief Persistent configuration manager for the em836 emulator.
 *
 * This header defines the configuration data model and persistence mechanism
 * for em836. Settings such as toolchain paths, initial memory allocation sizes,
 * CPU clock execution throttling speeds, default video modes, and window
 * preferences are saved to and loaded from a JSON configuration file (config.json)
 * stored in the application directory.
 *
 * Architecture:
 * - EmulationConfig: Plain Old Data (POD) representation of emulator runtime settings.
 * - ConfigManager: Static / utility class offering robust JSON serialization and deserialization
 *   with fallback default values if the configuration file is missing or corrupted.
 */

#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

namespace em836 {

/**
 * @enum VideoMode
 * @brief Represents the active display hardware mode of the em836 virtual machine.
 */
enum class VideoMode : uint32_t {
    Text80x25 = 0,      ///< Standard IBM PC VGA 80x25 text mode (Buffer at 0x000B8000)
    Graphics640x480 = 1 ///< Linear 640x480 16-color graphics framebuffer (Buffer at 0x000A0000)
};

/**
 * @struct EmulatorConfig
 * @brief Holds user-configurable settings that dictate the emulator's initial state and behavior.
 */
struct EmulatorConfig {
    // ---------------------------------------------------------
    // Toolchain settings
    // ---------------------------------------------------------
    /// @brief File system path to the AstraC executable (AstraC.exe).
    /// If empty, the automated discovery algorithm searches standard sibling paths and PATH.
    std::string astracPath = "../AstraC/build/Release/AstraC.exe";

    // ---------------------------------------------------------
    // Hardware & Emulation Parameters
    // ---------------------------------------------------------
    /// @brief Total physical guest RAM to map into the Unicorn address space in bytes (Default: 16 MB = 0x01000000).
    uint32_t memorySize = 16 * 1024 * 1024;

    /// @brief Target CPU clock speed in Hertz for throttled execution.
    /// A value of 0 indicates 'Unlimited' (maximum host processor speed).
    /// Common throttling values: 10, 100, 1000 (1 kHz), 10000 (10 kHz), 100000 (100 kHz), 1000000 (1 MHz).
    uint32_t clockSpeedHz = 0;

    /// @brief Default initial display mode upon reset or boot.
    VideoMode defaultVideoMode = VideoMode::Text80x25;

    /// @brief Default initial execution mode (32 for 32-bit Protected Mode, 16 for 16-bit Real Mode).
    uint32_t cpuModeBits = 32;

    /// @brief Default initial base entry point address (EIP) for raw binary payloads (Default: 0x00000000).
    uint32_t defaultEntryAddress = 0x00000000;

    /// @brief Default initial stack pointer address (ESP) (Default: 0x00800000 = 8 MB).
    uint32_t defaultStackAddress = 0x00800000;

    // ---------------------------------------------------------
    // Recent Session State
    // ---------------------------------------------------------
    /// @brief Most recently opened or assembled source/binary file path.
    std::string lastLoadedFile = "";

    /// @brief Last working directory used in file pickers.
    std::string lastWorkingDirectory = ".";
};

/**
 * @class ConfigManager
 * @brief Handles saving and loading EmulatorConfig instances to/from JSON files on disk.
 */
class ConfigManager {
public:
    /**
     * @brief Retrieves the default path to the configuration file (typically "config.json" in current directory).
     * @return std::filesystem::path to the configuration file.
     */
    static std::filesystem::path GetDefaultConfigPath();

    /**
     * @brief Loads configuration from the specified path or falls back to sane defaults.
     * @param path Path to the JSON config file. If omitted, uses GetDefaultConfigPath().
     * @return EmulatorConfig populated with loaded or default settings.
     */
    static EmulatorConfig Load(const std::filesystem::path& path = GetDefaultConfigPath());

    /**
     * @brief Saves the given configuration to disk as formatted JSON.
     * @param config The configuration to persist.
     * @param path Target path for the JSON config file. If omitted, uses GetDefaultConfigPath().
     * @return True if written successfully, false otherwise.
     */
    static bool Save(const EmulatorConfig& config, const std::filesystem::path& path = GetDefaultConfigPath());
};

} // namespace em836
