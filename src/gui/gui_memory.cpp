/**
 * @file gui_memory.cpp
 * @brief Memory Hex Viewer with ASCII Dump and Quick Jump Navigation.
 */

#include "em836/gui.hpp"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace em836 {

void GuiManager::RenderMemoryWindow(Emulator& emu, GuiState& state) {
    if (!state.showMemory) return;

    if (ImGui::Begin("Memory Hex Viewer", &state.showMemory)) {
        CpuState cpu = emu.GetCpuStateSnapshot();

        // -------------------------------------------------------------
        // Quick Jump Controls & Hex Address Input
        // -------------------------------------------------------------
        ImGui::Text("Jump: ");
        ImGui::SameLine();
        if (ImGui::SmallButton("Origin 0x0")) {
            state.memoryCurrentAddress = 0x00000000;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("EIP")) {
            state.memoryCurrentAddress = cpu.eip;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("VGA (0xB8000)")) {
            state.memoryCurrentAddress = VGA_TEXT_RAM_BASE;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("GFX (0xA0000)")) {
            state.memoryCurrentAddress = VGA_GFX_RAM_BASE;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Stack (ESP)")) {
            state.memoryCurrentAddress = cpu.esp;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputText("##mem_addr_input", state.memoryAddressInput, sizeof(state.memoryAddressInput),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
            state.memoryCurrentAddress = static_cast<uint32_t>(strtoul(state.memoryAddressInput, nullptr, 16));
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("< Prev")) {
            if (state.memoryCurrentAddress >= 256) state.memoryCurrentAddress -= 256;
            else state.memoryCurrentAddress = 0;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Next >")) {
            if (state.memoryCurrentAddress + 256 < emu.GetRamSize()) state.memoryCurrentAddress += 256;
            snprintf(state.memoryAddressInput, sizeof(state.memoryAddressInput), "%08X", state.memoryCurrentAddress);
        }

        ImGui::Separator();

        // -------------------------------------------------------------
        // Hex & ASCII Dump Table
        // -------------------------------------------------------------
        uint32_t ramSize = static_cast<uint32_t>(emu.GetRamSize());
        uint32_t baseAddr = state.memoryCurrentAddress & ~0x0F; // Align to 16 bytes

        size_t rows = 16;
        std::vector<uint8_t> buffer(rows * 16, 0);
        size_t bytesToRead = std::min(static_cast<size_t>(rows * 16), static_cast<size_t>(ramSize > baseAddr ? ramSize - baseAddr : 0));

        if (bytesToRead > 0) {
            emu.ReadMemory(baseAddr, buffer.data(), bytesToRead);
        }

        ImGui::BeginChild("hex_scroll_region", ImVec2(0, 0), true);

        for (size_t r = 0; r < rows; ++r) {
            uint32_t rowAddr = baseAddr + static_cast<uint32_t>(r * 16);
            if (rowAddr >= ramSize) break;

            // Address column
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%08X: ", rowAddr);
            ImGui::SameLine();

            // 16 Hex Byte columns
            for (size_t b = 0; b < 16; ++b) {
                size_t idx = r * 16 + b;
                if (idx < bytesToRead) {
                    uint8_t val = buffer[idx];
                    if (val == 0) {
                        ImGui::TextDisabled("%02X ", val);
                    } else {
                        ImGui::Text("%02X ", val);
                    }
                } else {
                    ImGui::TextDisabled("   ");
                }
                ImGui::SameLine();
                if (b == 7) {
                    ImGui::TextDisabled(" ");
                    ImGui::SameLine();
                }
            }

            ImGui::TextDisabled("| ");
            ImGui::SameLine();

            // ASCII sidebar column
            char ascii[17];
            for (size_t b = 0; b < 16; ++b) {
                size_t idx = r * 16 + b;
                if (idx < bytesToRead) {
                    uint8_t val = buffer[idx];
                    ascii[b] = (val >= 32 && val <= 126) ? static_cast<char>(val) : '.';
                } else {
                    ascii[b] = ' ';
                }
            }
            ascii[16] = '\0';
            ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.7f, 1.0f), "%s", ascii);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace em836
