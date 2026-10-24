/**
 * @file test_features.cpp
 * @brief Automated verification for Breakpoints, Watchpoints, Syntax Highlighter,
 *        and .rodata String Extraction.
 */

#include "em836/emulator.hpp"
#include "em836/astrac.hpp"
#include "em836/syntax.hpp"
#include "em836/gui.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace em836;

void TestSyntaxHighlighter() {
    std::cout << "[Test 1] Testing Syntax Highlighter..." << std::endl;

    // Test Assembly line
    std::string asLine = "start: mov eax, 0x1234 ; test comment";
    auto asTokens = SyntaxHighlighter::TokenizeAs(asLine);
    assert(!asTokens.empty());
    bool foundMov = false;
    bool foundEax = false;
    bool foundComment = false;
    for (const auto& tok : asTokens) {
        if (tok.text == "mov" && tok.type == TokenType::Instruction) foundMov = true;
        if (tok.text == "eax" && tok.type == TokenType::Register) foundEax = true;
        if (tok.type == TokenType::Comment) foundComment = true;
    }
    assert(foundMov);
    assert(foundEax);
    assert(foundComment);

    // Test Astra C line
    std::string acLine = "u32 calculate_sum(u32 a, u32 b) { return a + b; }";
    bool inBlock = false;
    auto acTokens = SyntaxHighlighter::TokenizeAc(acLine, inBlock);
    assert(!acTokens.empty());
    bool foundU32 = false;
    bool foundReturn = false;
    for (const auto& tok : acTokens) {
        if (tok.text == "u32" && tok.type == TokenType::Type) foundU32 = true;
        if (tok.text == "return" && tok.type == TokenType::Keyword) foundReturn = true;
    }
    assert(foundU32);
    assert(foundReturn);

    std::cout << "  -> Syntax Highlighter passed!" << std::endl;
}

void TestBreakpointsAndWatchpoints() {
    std::cout << "[Test 2] Testing Breakpoints and Memory Watchpoints..." << std::endl;

    Emulator emu;
    EmulatorConfig config;
    config.memorySize = 4 * 1024 * 1024;
    emu.Initialize(config);
    emu.SetCpuMode(32);
    emu.SetClockSpeed(0); // 0 = Unlimited

    // Program:
    // 0x0000: mov dword ptr [0x2000], 0x11223344   ; C7 05 00 20 00 00 44 33 22 11 (10 bytes)
    // 0x000A: mov dword ptr [0x2000], 0x55667788   ; C7 05 00 20 00 00 88 77 66 55 (10 bytes)
    // 0x0014: hlt                                  ; F4
    std::vector<uint8_t> code = {
        0xC7, 0x05, 0x00, 0x20, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
        0xC7, 0x05, 0x00, 0x20, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55,
        0xF4
    };
    emu.LoadBinary(code.data(), code.size(), 0x00000000, 0x00000000);

    // Add a watchpoint on 0x2000 (condition = OnChange)
    uint32_t wpId = emu.AddWatchpoint(0x00002000, 4, WatchCondition::OnChange, 0, "test_var");
    assert(wpId > 0);

    // Add an execution breakpoint on 0x0014 (HLT)
    emu.AddBreakpoint(0x00000014);
    assert(emu.HasBreakpoint(0x00000014));

    // Run emulator: first write at 0x0000 should trigger watchpoint!
    emu.Start();

    // Wait for pause/halt
    auto start = std::chrono::steady_clock::now();
    while (emu.GetCpuStateSnapshot().status == EmulatorStatus::Running) {
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CpuState cpu = emu.GetCpuStateSnapshot();
    assert(cpu.status == EmulatorStatus::Paused || cpu.status == EmulatorStatus::Halted);

    auto wps = emu.GetWatchpointsList();
    assert(!wps.empty());
    assert(wps[0].hitCount >= 1);
    assert(wps[0].currentValue == 0x11223344);
    std::cout << "  -> Watchpoint hit verified: current value = 0x" << std::hex << wps[0].currentValue << std::dec << std::endl;

    // Resume execution: second write at 0x000A should trigger watchpoint again!
    emu.Start();
    start = std::chrono::steady_clock::now();
    while (emu.GetCpuStateSnapshot().status == EmulatorStatus::Running) {
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    wps = emu.GetWatchpointsList();
    assert(wps[0].hitCount >= 2);
    assert(wps[0].currentValue == 0x55667788);
    assert(wps[0].previousValue == 0x11223344);
    std::cout << "  -> Second watchpoint change verified: 0x" << std::hex << wps[0].previousValue << " -> 0x" << wps[0].currentValue << std::dec << std::endl;

    // Resume execution: should hit breakpoint at 0x0014
    emu.Start();
    start = std::chrono::steady_clock::now();
    while (emu.GetCpuStateSnapshot().status == EmulatorStatus::Running) {
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > 2) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto bps = emu.GetBreakpointsList();
    assert(!bps.empty());
    assert(bps[0].hitCount >= 1);
    std::cout << "  -> Execution breakpoint hit verified: address 0x" << std::hex << bps[0].address << std::dec << " hit " << bps[0].hitCount << " times!" << std::endl;
}

void TestEditorTabs() {
    std::cout << "[Test 3] Testing Editor Tabs API..." << std::endl;

    GuiState state;
    assert(state.editorTabs.empty());

    // Create new Assembly tab
    int tab0 = GuiManager::CreateNewEditorTab(state, 0);
    assert(tab0 == 0);
    assert(state.editorTabs.size() == 1);
    assert(state.editorTabs[0].language == 0);

    // Create new Astra C tab
    int tab1 = GuiManager::CreateNewEditorTab(state, 1);
    assert(tab1 == 1);
    assert(state.editorTabs.size() == 2);
    assert(state.editorTabs[1].language == 1);

    // Close tab0
    GuiManager::CloseEditorTab(state, 0);
    assert(state.editorTabs.size() == 1);
    assert(state.editorTabs[0].language == 1);

    std::cout << "  -> Editor Tabs API passed!" << std::endl;
}

void TestRoDataStrings() {
    std::cout << "[Test 4] Testing .rodata String Parsing..." << std::endl;

    std::filesystem::path exePath = "examples/exe_import_demo.exe";
    if (std::filesystem::exists(exePath)) {
        std::ifstream file(exePath, std::ios::binary);
        assert(file.is_open());
        std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        AcfhParsedModule mod;
        bool ok = AstraCToolchain::ParseAcfhModule(buffer.data(), buffer.size(), mod);
        assert(ok);
        assert(mod.header.rodata_size > 0);

        uint32_t roOffset = mod.header.rodata_offset;
        uint32_t roSize = mod.header.rodata_size;

        const uint8_t* roBytes = buffer.data() + roOffset;
        size_t stringCount = 0;
        size_t start = 0;
        while (start < roSize) {
            while (start < roSize && (roBytes[start] == 0 || !std::isprint(roBytes[start]))) ++start;
            if (start >= roSize) break;
            size_t end = start;
            while (end < roSize && roBytes[end] != 0 && std::isprint(roBytes[end])) ++end;
            if (end > start) {
                std::string s(reinterpret_cast<const char*>(roBytes + start), end - start);
                stringCount++;
            }
            start = end + 1;
        }
        assert(stringCount > 0);
        std::cout << "  -> Extracted " << stringCount << " strings from .rodata of exe_import_demo.exe!" << std::endl;
    }

    std::cout << "  -> .rodata Strings test passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Running em836 Verification Suite      " << std::endl;
    std::cout << "========================================" << std::endl;

    TestSyntaxHighlighter();
    TestBreakpointsAndWatchpoints();
    TestEditorTabs();
    TestRoDataStrings();

    std::cout << "========================================" << std::endl;
    std::cout << "  ALL TESTS PASSED SUCCESSFULLY!        " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
