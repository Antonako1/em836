/**
 * @file gui_vga.cpp
 * @brief VGA Dual-Mode Canvas (Text 80x25 & Graphics 640x480 Framebuffer).
 */

#include "em836/gui.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>

namespace em836 {

void GuiManager::RenderVgaWindow(Emulator& emu, GuiState& state, SDL_Renderer* renderer) {
    if (!state.showVga) return;

    if (ImGui::Begin("VGA Display", &state.showVga)) {
        // Mode toggle bar
        VideoMode curMode = emu.GetVga().GetMode();
        bool isText = (curMode == VideoMode::Text80x25);

        if (ImGui::RadioButton("Text Mode (80x25 @ 0xB8000)", isText)) {
            emu.GetVga().SetMode(VideoMode::Text80x25);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Graphics Mode (640x480 @ 0xA0000)", !isText)) {
            emu.GetVga().SetMode(VideoMode::Graphics640x480);
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        if (ImGui::SmallButton("Clear Screen")) {
            emu.GetVga().ClearScreen(emu.GetRamBuffer(), emu.GetRamSize(), 0);
        }

        ImGui::Separator();

        // Update VGA texture from guest physical RAM
        emu.GetVga().Update(renderer, emu.GetRamBuffer(), emu.GetRamSize());
        SDL_Texture* texture = emu.GetVga().GetTexture();

        if (texture) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            avail.y -= 25; // Leave room for status bar

            // Maintain 4:3 aspect ratio (640:480)
            float aspect = 640.0f / 480.0f;
            float targetW = avail.x;
            float targetH = targetW / aspect;

            if (targetH > avail.y && avail.y > 50.0f) {
                targetH = avail.y;
                targetW = targetH * aspect;
            }

            // Center image within available area
            float offsetX = std::max(0.0f, (avail.x - targetW) * 0.5f);
            if (offsetX > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            }

            ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(targetW, targetH));
        } else {
            ImGui::TextDisabled("VGA Display initializing...");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Mode: %s | Resolution: 640x480 | Refresh: 60 Hz",
            isText ? "IBM PC 80x25 Color Text (CP437)" : "640x480 Linear 16-Color Framebuffer");
    }
    ImGui::End();
}

} // namespace em836
