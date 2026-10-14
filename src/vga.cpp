/**
 * @file vga.cpp
 * @brief Implementation of the Dual-Mode VGA Video Subsystem (Text 80x25 and Graphics 640x480).
 */

#include "em836/vga.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace em836 {

// -----------------------------------------------------------------------------
// Standard IBM PC 8x16 Font Glyph Bitmap (CP437)
// Embedded 256 characters * 16 scanline rows per character.
// -----------------------------------------------------------------------------
#include "vga_font_data.inl"

VgaSubsystem::VgaSubsystem() {
    m_pixelBuffer.resize(VGA_GFX_WIDTH * VGA_GFX_HEIGHT, 0xFF000000);
}

VgaSubsystem::~VgaSubsystem() {
    DestroyTexture();
}

VideoMode VgaSubsystem::GetMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mode;
}

void VgaSubsystem::SetMode(VideoMode mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mode = mode;
}

int VgaSubsystem::GetWidth() const {
    return VGA_GFX_WIDTH;
}

int VgaSubsystem::GetHeight() const {
    return VGA_GFX_HEIGHT;
}

void VgaSubsystem::DestroyTexture() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
}

SDL_Texture* VgaSubsystem::GetTexture() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_texture;
}

void VgaSubsystem::Update(SDL_Renderer* renderer, const uint8_t* ram, size_t ramSize) {
    if (!renderer || !ram || ramSize == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Create texture if it has not been instantiated or re-create if needed
    if (!m_texture) {
        m_texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            VGA_GFX_WIDTH,
            VGA_GFX_HEIGHT
        );
        if (m_texture) {
            SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
        }
    }

    if (!m_texture) {
        return;
    }

    // Render RAM contents into m_pixelBuffer based on active mode
    if (m_mode == VideoMode::Text80x25) {
        RenderTextMode(ram, ramSize);
    } else {
        RenderGraphicsMode(ram, ramSize);
    }

    // Upload staging pixel buffer to the SDL3 streaming texture
    SDL_UpdateTexture(
        m_texture,
        nullptr,
        m_pixelBuffer.data(),
        VGA_GFX_WIDTH * static_cast<int>(sizeof(uint32_t))
    );
}

void VgaSubsystem::RenderTextMode(const uint8_t* ram, size_t ramSize) {
    // Fill the 640x480 canvas with background black
    std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), 0xFF000000);

    if (ramSize < VGA_TEXT_RAM_BASE + VGA_TEXT_RAM_SIZE) {
        return;
    }

    const uint8_t* textRam = ram + VGA_TEXT_RAM_BASE;

    // 80 columns * 8 pixels = 640 width
    // 25 rows * 16 pixels = 400 height
    // Vertically centered: Y offset = (480 - 400) / 2 = 40 pixels
    const int startY = 40;

    for (int row = 0; row < static_cast<int>(VGA_TEXT_ROWS); ++row) {
        for (int col = 0; col < static_cast<int>(VGA_TEXT_COLS); ++col) {
            size_t cellIndex = (row * VGA_TEXT_COLS + col) * 2;
            uint8_t asciiCode = textRam[cellIndex];
            uint8_t attrByte = textRam[cellIndex + 1];

            uint8_t fgIndex = attrByte & 0x0F;
            uint8_t bgIndex = (attrByte >> 4) & 0x0F;

            uint32_t fgColor = IBM_VGA_PALETTE[fgIndex].ToUint32();
            uint32_t bgColor = IBM_VGA_PALETTE[bgIndex].ToUint32();

            const uint8_t* glyph = &VGA_FONT_8X16[asciiCode * 16];

            int pixelBaseX = col * 8;
            int pixelBaseY = startY + (row * 16);

            for (int glyphY = 0; glyphY < 16; ++glyphY) {
                uint8_t rowBits = glyph[glyphY];
                int targetY = pixelBaseY + glyphY;

                for (int glyphX = 0; glyphX < 8; ++glyphX) {
                    int targetX = pixelBaseX + glyphX;
                    bool isForeground = (rowBits & (0x80 >> glyphX)) != 0;
                    m_pixelBuffer[targetY * VGA_GFX_WIDTH + targetX] = isForeground ? fgColor : bgColor;
                }
            }
        }
    }
}

void VgaSubsystem::RenderGraphicsMode(const uint8_t* ram, size_t ramSize) {
    if (ramSize < VGA_GFX_RAM_BASE + VGA_GFX_RAM_SIZE) {
        return;
    }

    const uint8_t* gfxRam = ram + VGA_GFX_RAM_BASE;

    // Translate each byte palette index to 32-bit RGBA
    for (size_t i = 0; i < VGA_GFX_RAM_SIZE; ++i) {
        uint8_t colorIndex = gfxRam[i] & 0x0F;
        m_pixelBuffer[i] = IBM_VGA_PALETTE[colorIndex].ToUint32();
    }
}

// -----------------------------------------------------------------------------
// Accelerated Software Drawing Primitives
// -----------------------------------------------------------------------------

void VgaSubsystem::PlotPixel(uint8_t* ram, size_t ramSize, int x, int y, uint8_t color) {
    if (!ram || x < 0 || x >= static_cast<int>(VGA_GFX_WIDTH) || y < 0 || y >= static_cast<int>(VGA_GFX_HEIGHT)) {
        return;
    }

    size_t offset = VGA_GFX_RAM_BASE + (static_cast<size_t>(y) * VGA_GFX_WIDTH) + static_cast<size_t>(x);
    if (offset < ramSize) {
        ram[offset] = color & 0x0F;
    }
}

void VgaSubsystem::DrawLine(uint8_t* ram, size_t ramSize, int x0, int y0, int x1, int y1, uint8_t color) {
    if (!ram) return;

    // Bresenham's Line Algorithm
    int dx = std::abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        PlotPixel(ram, ramSize, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void VgaSubsystem::FillRect(uint8_t* ram, size_t ramSize, int x, int y, int w, int h, uint8_t color) {
    if (!ram || w <= 0 || h <= 0) return;

    int xStart = std::max(0, x);
    int yStart = std::max(0, y);
    int xEnd = std::min(static_cast<int>(VGA_GFX_WIDTH), x + w);
    int yEnd = std::min(static_cast<int>(VGA_GFX_HEIGHT), y + h);

    if (xStart >= xEnd || yStart >= yEnd) return;

    uint8_t col = color & 0x0F;
    for (int curY = yStart; curY < yEnd; ++curY) {
        size_t rowOffset = VGA_GFX_RAM_BASE + (static_cast<size_t>(curY) * VGA_GFX_WIDTH) + static_cast<size_t>(xStart);
        size_t rowLength = static_cast<size_t>(xEnd - xStart);
        if (rowOffset + rowLength <= ramSize) {
            std::memset(ram + rowOffset, col, rowLength);
        }
    }
}

void VgaSubsystem::ClearScreen(uint8_t* ram, size_t ramSize, uint8_t color) {
    if (!ram) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    uint8_t col = color & 0x0F;

    if (m_mode == VideoMode::Text80x25) {
        if (ramSize >= VGA_TEXT_RAM_BASE + VGA_TEXT_RAM_SIZE) {
            uint8_t* textRam = ram + VGA_TEXT_RAM_BASE;
            uint8_t attr = (col << 4) | 0x0F; // Background color with bright white text
            for (size_t i = 0; i < VGA_TEXT_RAM_SIZE; i += 2) {
                textRam[i] = 0x20;     // Space character
                textRam[i + 1] = attr; // Attribute
            }
        }
    } else {
        if (ramSize >= VGA_GFX_RAM_BASE + VGA_GFX_RAM_SIZE) {
            std::memset(ram + VGA_GFX_RAM_BASE, col, VGA_GFX_RAM_SIZE);
        }
    }
}

} // namespace em836
