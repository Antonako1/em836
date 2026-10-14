/**
 * @file gui_modals.cpp
 * @brief Modal Dialogs: Settings, Load Execution Options, File Open, and About.
 */

#include "em836/gui.hpp"
#include "em836/astrac.hpp"
#include <imgui.h>
#include <cstring>

namespace em836 {

void GuiManager::RenderSettingsModal(Emulator& emu, GuiState& state) {
    if (!state.showSettingsModal) return;

    ImGui::OpenPopup("Emulator Settings");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 280), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Emulator Settings", &state.showSettingsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        EmulatorConfig& cfg = emu.GetConfig();

        // 1. AstraC Path
        ImGui::Text("AstraC Compiler Executable Path:");
        char astracBuf[260];
        strncpy_s(astracBuf, cfg.astracPath.c_str(), sizeof(astracBuf));
        if (ImGui::InputText("##astrac_path_cfg", astracBuf, sizeof(astracBuf))) {
            cfg.astracPath = astracBuf;
        }

        // Test AstraC path
        std::filesystem::path foundPath = AstraCToolchain::FindAstraC(cfg.astracPath);
        if (!foundPath.empty()) {
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Found: %s", foundPath.string().c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "AstraC.exe not found at this path.");
        }

        ImGui::Spacing();
        ImGui::Separator();

        // 2. RAM Size
        ImGui::Text("Guest Physical RAM Size:");
        int ramMb = static_cast<int>(cfg.memorySize / (1024 * 1024));
        const char* ramOptions[] = { "4 MB", "8 MB", "16 MB", "32 MB", "64 MB" };
        const int ramValues[] = { 4, 8, 16, 32, 64 };
        int ramIdx = 2; // Default 16 MB
        for (int i = 0; i < 5; ++i) {
            if (ramValues[i] == ramMb) {
                ramIdx = i;
                break;
            }
        }
        if (ImGui::Combo("##ram_combo", &ramIdx, ramOptions, IM_ARRAYSIZE(ramOptions))) {
            cfg.memorySize = ramValues[ramIdx] * 1024 * 1024;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Buttons
        if (ImGui::Button("Save & Apply", ImVec2(120, 0))) {
            ConfigManager::Save(cfg);
            state.showSettingsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            state.showSettingsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void GuiManager::RenderLoadOptionsModal(Emulator& emu, GuiState& state) {
    if (!state.showLoadOptionsModal) return;

    ImGui::OpenPopup("Execution Setup & Load Options");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Execution Setup & Load Options", &state.showLoadOptionsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Configure execution parameters for: %s",
            state.pendingLoadFilePath.empty() ? "(Current Program)" : state.pendingLoadFilePath.c_str());
        ImGui::Separator();

        ImGui::Text("Base Load Address (Hex):");
        ImGui::InputText("##load_addr", state.loadAddressInput, sizeof(state.loadAddressInput), ImGuiInputTextFlags_CharsHexadecimal);

        ImGui::Text("Initial Entry Point EIP (Hex):");
        ImGui::InputText("##load_eip", state.loadEipInput, sizeof(state.loadEipInput), ImGuiInputTextFlags_CharsHexadecimal);

        ImGui::Text("Initial Stack Pointer ESP (Hex):");
        ImGui::InputText("##load_esp", state.loadEspInput, sizeof(state.loadEspInput), ImGuiInputTextFlags_CharsHexadecimal);

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Apply & Load", ImVec2(120, 0))) {
            uint32_t loadAddr = static_cast<uint32_t>(strtoul(state.loadAddressInput, nullptr, 16));
            uint32_t eip = static_cast<uint32_t>(strtoul(state.loadEipInput, nullptr, 16));
            uint32_t esp = static_cast<uint32_t>(strtoul(state.loadEspInput, nullptr, 16));

            if (!state.pendingLoadFilePath.empty()) {
                AstraCToolchain::IngestFile(state.pendingLoadFilePath, emu, emu.GetConfig().astracPath);
            }

            CpuState cpu = emu.GetCpuStateSnapshot();
            cpu.eip = eip;
            cpu.esp = esp;
            cpu.ebp = esp;
            emu.ApplyCpuState(cpu);

            state.showLoadOptionsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            state.showLoadOptionsModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void GuiManager::RenderFileOpenModal(Emulator& emu, GuiState& state) {
    if (!state.showFileOpenModal) return;

    ImGui::OpenPopup("Open Program File");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Open Program File", &state.showFileOpenModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter path to source (.as, .ac) or binary (.bin, .exe) file:");
        ImGui::SetNextItemWidth(360);
        ImGui::InputText("##open_file_path", state.fileOpenPathInput, sizeof(state.fileOpenPathInput));

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Open & Run", ImVec2(120, 0))) {
            std::string path(state.fileOpenPathInput);
            if (!path.empty()) {
                AstraCToolchain::IngestFile(path, emu, emu.GetConfig().astracPath);
                emu.Start();
            }
            state.showFileOpenModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            state.showFileOpenModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void GuiManager::RenderAboutModal(GuiState& state) {
    if (!state.showAboutModal) return;

    ImGui::OpenPopup("About em836");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("About em836", &state.showAboutModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "em836 — Interactive i386 Emulator & Debugger");
        ImGui::Text("Version 1.0.0 (C++20)");
        ImGui::Separator();

        ImGui::Text("Components:");
        ImGui::BulletText("Unicorn Engine: High-performance x86 CPU emulation");
        ImGui::BulletText("Capstone Engine: Real-time instruction disassembly");
        ImGui::BulletText("SDL3: Cross-platform windowing and GPU texture pipeline");
        ImGui::BulletText("Dear ImGui: Docking debugger interface");
        ImGui::BulletText("AstraC Integration: Automated x86 assembler and C compiler");

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Close", ImVec2(100, 0))) {
            state.showAboutModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace em836
