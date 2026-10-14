/**
 * @file vga.hpp
 * @brief Dual-mode VGA Subsystem (Text Mode 80x25 and Graphics 640x480 Framebuffer).
 *
 * This header defines the video output subsystem of em836, providing dual-mode display emulation:
 *
 * 1. Standard IBM PC VGA Text Mode (80 columns x 25 rows):
 *    - Located at physical memory address 0x000B8000 (spanning 4,000 bytes).
 *    - Memory layout consists of 2 bytes per cell:
 *        Byte 0: ASCII character glyph code (0x00 - 0xFF).
 *        Byte 1: Attribute byte containing foreground (bits 0-3) and background (bits 4-7) color indices.
 *    - Decoded and rendered using an embedded 8x16 IBM PC bitmap font to produce crisp 640x400 visual output.
 *
 * 2. Flat Linear Graphics Mode (640x480, 16 Colors):
 *    - Located at physical memory address 0x000A0000 (spanning 307,200 bytes).
 *    - Memory layout is 1 byte per pixel, where values 0-15 represent color palette indices.
 *    - Coordinates: (0,0) at top-left to (639, 479) at bottom-right.
 *    - Address calculation: Address = 0x000A0000 + (y * 640) + x.
 *    - Accelerated drawing primitives: Accelerated pixel plotting, Bresenham line drawing,
 *      and solid rectangle filling for guest applications.
 *
 * 3. SDL3 Texture Rendering Pipeline:
 *    - Generates a 32-bit RGBA streaming texture for Dear ImGui viewport display.
 *    - Updates smoothly at 60 FPS without tearing.
 */

#pragma once

#include "config.hpp"
#include <cstdint>
#include <vector>
#include <mutex>
#include <array>

// Forward declarations for SDL3 types
struct SDL_Renderer;
struct SDL_Texture;

namespace em836 {

// Memory Map Constants
constexpr uint32_t VGA_TEXT_RAM_BASE       = 0x000B8000; ///< Physical address of 80x25 text mode buffer
constexpr uint32_t VGA_TEXT_RAM_SIZE       = 4000;       ///< 80 cols * 25 rows * 2 bytes = 4000 bytes
constexpr uint32_t VGA_TEXT_COLS           = 80;         ///< 80 character columns
constexpr uint32_t VGA_TEXT_ROWS           = 25;         ///< 25 character rows
constexpr uint32_t VGA_FONT_GLYPH_WIDTH    = 8;          ///< Width of each font character in pixels
constexpr uint32_t VGA_FONT_GLYPH_HEIGHT   = 16;         ///< Height of each font character in pixels

constexpr uint32_t VGA_GFX_RAM_BASE        = 0x000A0000; ///< Physical address of linear 640x480 framebuffer
constexpr uint32_t VGA_GFX_WIDTH           = 640;        ///< Framebuffer width in pixels
constexpr uint32_t VGA_GFX_HEIGHT          = 480;        ///< Framebuffer height in pixels
constexpr uint32_t VGA_GFX_RAM_SIZE        = VGA_GFX_WIDTH * VGA_GFX_HEIGHT; ///< 307,200 bytes

/**
 * @struct ColorRGBA
 * @brief Represents a 32-bit unpacked RGBA color.
 */
struct ColorRGBA {
    uint8_t r; ///< Red channel (0 - 255)
    uint8_t g; ///< Green channel (0 - 255)
    uint8_t b; ///< Blue channel (0 - 255)
    uint8_t a; ///< Alpha channel (0 - 255)

    constexpr uint32_t ToUint32() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               (static_cast<uint32_t>(r));
    }
};

/**
 * @brief The classic IBM PC 16-color CGA/EGA/VGA color palette.
 */
constexpr std::array<ColorRGBA, 16> IBM_VGA_PALETTE = {{
    {0x00, 0x00, 0x00, 0xFF}, // 0: Black
    {0x00, 0x00, 0xAA, 0xFF}, // 1: Blue
    {0x00, 0xAA, 0x00, 0xFF}, // 2: Green
    {0x00, 0xAA, 0xAA, 0xFF}, // 3: Cyan
    {0xAA, 0x00, 0x00, 0xFF}, // 4: Red
    {0xAA, 0x00, 0xAA, 0xFF}, // 5: Magenta
    {0xAA, 0x55, 0x00, 0xFF}, // 6: Brown
    {0xAA, 0xAA, 0xAA, 0xFF}, // 7: Light Gray
    {0x55, 0x55, 0x55, 0xFF}, // 8: Dark Gray
    {0x55, 0x55, 0xFF, 0xFF}, // 9: Bright Blue
    {0x55, 0xFF, 0x55, 0xFF}, // 10: Bright Green
    {0x55, 0xFF, 0xFF, 0xFF}, // 11: Bright Cyan
    {0xFF, 0x55, 0x55, 0xFF}, // 12: Bright Red
    {0xFF, 0x55, 0xFF, 0xFF}, // 13: Bright Magenta
    {0xFF, 0xFF, 0x55, 0xFF}, // 14: Bright Yellow
    {0xFF, 0xFF, 0xFF, 0xFF}  // 15: Bright White
}};

/**
 * @class VgaSubsystem
 * @brief Encapsulates the video hardware display, color translation, font rendering,
 *        accelerated software rasterization, and SDL3 texture management.
 */
class VgaSubsystem {
public:
    VgaSubsystem();
    ~VgaSubsystem();

    // Prevent copy/move to guarantee thread safety and single texture ownership
    VgaSubsystem(const VgaSubsystem&) = delete;
    VgaSubsystem& operator=(const VgaSubsystem&) = delete;

    /**
     * @brief Gets current active video mode (Text 80x25 or Graphics 640x480).
     */
    VideoMode GetMode() const;

    /**
     * @brief Sets the active video mode.
     * @param mode Target VideoMode enum.
     */
    void SetMode(VideoMode mode);

    /**
     * @brief Renders and updates the SDL3 hardware texture from current physical RAM.
     * @param renderer Pointer to active SDL_Renderer.
     * @param ram Pointer to base of guest physical RAM.
     * @param ramSize Size of guest physical RAM in bytes.
     */
    void Update(SDL_Renderer* renderer, const uint8_t* ram, size_t ramSize);

    /**
     * @brief Returns the internal SDL_Texture for display inside Dear ImGui.
     * @return SDL_Texture* pointer, or nullptr if not yet created.
     */
    SDL_Texture* GetTexture() const;

    /**
     * @brief Returns display width in pixels for the current mode.
     */
    int GetWidth() const;

    /**
     * @brief Returns display height in pixels for the current mode.
     */
    int GetHeight() const;

    // -------------------------------------------------------------
    // Accelerated Drawing Primitives (Modifies RAM directly)
    // -------------------------------------------------------------

    /**
     * @brief Plots a single pixel directly into RAM at (x, y) with the specified palette color.
     * @param ram Pointer to base of guest physical RAM.
     * @param ramSize Size of physical RAM.
     * @param x X coordinate (0 to 639).
     * @param y Y coordinate (0 to 479).
     * @param color Palette index (0 to 15).
     */
    void PlotPixel(uint8_t* ram, size_t ramSize, int x, int y, uint8_t color);

    /**
     * @brief Draws a line using Bresenham's line algorithm into RAM.
     * @param ram Pointer to base of guest physical RAM.
     * @param ramSize Size of physical RAM.
     * @param x0 Starting X coordinate.
     * @param y0 Starting Y coordinate.
     * @param x1 Ending X coordinate.
     * @param y1 Ending Y coordinate.
     * @param color Palette index (0 to 15).
     */
    void DrawLine(uint8_t* ram, size_t ramSize, int x0, int y0, int x1, int y1, uint8_t color);

    /**
     * @brief Fills a solid rectangle into RAM.
     * @param ram Pointer to base of guest physical RAM.
     * @param ramSize Size of physical RAM.
     * @param x Top-left X coordinate.
     * @param y Top-left Y coordinate.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param color Palette index (0 to 15).
     */
    void FillRect(uint8_t* ram, size_t ramSize, int x, int y, int w, int h, uint8_t color);

    /**
     * @brief Clears the active screen buffer with a specified color.
     *        In Text Mode: fills with spaces and the color as background.
     *        In Graphics Mode: fills 640x480 buffer with the color index.
     * @param ram Pointer to base of guest physical RAM.
     * @param ramSize Size of physical RAM.
     * @param color Palette index (0 to 15).
     */
    void ClearScreen(uint8_t* ram, size_t ramSize, uint8_t color);

    /**
     * @brief Resets video subsystem and frees SDL textures.
     */
    void DestroyTexture();

private:
    VideoMode m_mode = VideoMode::Text80x25;
    mutable std::mutex m_mutex;

    SDL_Texture* m_texture = nullptr;
    std::vector<uint32_t> m_pixelBuffer; ///< Local 32-bit RGBA staging buffer (640x480)

    void RenderTextMode(const uint8_t* ram, size_t ramSize);
    void RenderGraphicsMode(const uint8_t* ram, size_t ramSize);
};

} // namespace em836
