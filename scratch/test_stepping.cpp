#include "em836/emulator.hpp"
#include "em836/astrac.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

int main() {
    std::cout << "[TEST] Starting Stepping Engine & Library Debugging Test..." << std::endl;

    em836::EmulatorConfig config;
    em836::Emulator emu;
    if (!emu.Initialize(config)) {
        std::cerr << "[FAIL] Failed to initialize emulator!" << std::endl;
        return 1;
    }

    std::string exePath = "examples/exe_import_demo.exe";
    std::cout << "[TEST] Ingesting " << exePath << "..." << std::endl;
    if (!em836::AstraCToolchain::IngestFile(exePath, emu, "C:\\Users\\anton\\source\\repos\\AstraC\\build\\Release\\AstraC.exe")) {
        std::cerr << "[FAIL] IngestFile failed!" << std::endl;
        return 1;
    }

    const auto& libs = emu.GetLoadedLibraries();
    std::cout << "[TEST] Loaded libraries count: " << libs.size() << std::endl;
    if (libs.empty()) {
        std::cerr << "[FAIL] No libraries loaded!" << std::endl;
        return 1;
    }
    std::cout << "[TEST] Library 0: " << libs[0].fileName << " @ 0x" << std::hex << libs[0].baseAddress << std::dec << std::endl;

    // Initial EIP
    auto state = emu.GetCpuStateSnapshot();
    std::cout << "[TEST] Initial EIP: 0x" << std::hex << state.eip << std::dec << std::endl;

    // Single step a few instructions
    for (int i = 0; i < 5; ++i) {
        emu.StepInto();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        state = emu.GetCpuStateSnapshot();
        std::cout << "[TEST] Step " << i << " EIP: 0x" << std::hex << state.eip << std::dec << std::endl;
    }

    // Step until we hit the first call to math_lib (or reach 0x00100000+)
    bool enteredLib = false;
    uint32_t lastEip = state.eip;
    for (int step = 0; step < 200; ++step) {
        emu.StepInto();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        state = emu.GetCpuStateSnapshot();
        if (state.eip >= 0x00100000 && state.eip < 0x00200000) {
            std::cout << "[PASS] Successfully stepped INTO dynamic library! EIP = 0x" 
                      << std::hex << state.eip << std::dec << " (caller was 0x" 
                      << std::hex << lastEip << std::dec << ")" << std::endl;
            enteredLib = true;
            break;
        }
        lastEip = state.eip;
    }

    if (!enteredLib) {
        std::cerr << "[FAIL] Did not step into library within 200 steps!" << std::endl;
        return 1;
    }

    // Now inside library function! Let's test StepOut!
    std::cout << "[TEST] Testing StepOut from library function..." << std::endl;
    emu.StepOut();

    // Wait for StepOut to complete (worker pauses when caller return address is hit)
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!emu.IsStepOutActive()) {
            break;
        }
    }

    state = emu.GetCpuStateSnapshot();
    std::cout << "[TEST] After StepOut: EIP = 0x" << std::hex << state.eip 
              << ", status = " << (int)state.status << std::dec << std::endl;

    if (state.eip >= 0x00100000) {
        std::cerr << "[FAIL] StepOut failed to return to caller! EIP is still in library: 0x" 
                  << std::hex << state.eip << std::dec << std::endl;
        return 1;
    }
    std::cout << "[PASS] StepOut successfully returned to caller in main module at EIP = 0x" 
              << std::hex << state.eip << std::dec << "!" << std::endl;

    // Now let's step until next call to math_lib
    std::cout << "[TEST] Stepping to next call to test StepOver..." << std::endl;
    uint32_t callEip = 0;
    for (int step = 0; step < 100; ++step) {
        uint8_t op = 0;
        emu.ReadMemory(state.eip, &op, 1);
        // Look for CALL E8
        if (op == 0xE8) {
            callEip = state.eip;
            std::cout << "[TEST] Found CALL instruction at EIP = 0x" << std::hex << callEip << std::dec << std::endl;
            break;
        }
        emu.StepInto();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        state = emu.GetCpuStateSnapshot();
    }

    if (callEip != 0) {
        std::cout << "[TEST] Testing StepOver on CALL at 0x" << std::hex << callEip << std::dec << "..." << std::endl;
        emu.StepOver();

        for (int i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!emu.IsStepOverActive()) {
                break;
            }
        }

        state = emu.GetCpuStateSnapshot();
        std::cout << "[TEST] After StepOver: EIP = 0x" << std::hex << state.eip 
                  << ", status = " << (int)state.status << std::dec << std::endl;
        if (state.eip == callEip + 5) {
            std::cout << "[PASS] StepOver landed exactly on the next instruction at 0x" 
                      << std::hex << state.eip << std::dec << " without entering library!" << std::endl;
        } else {
            std::cout << "[INFO] Landed at 0x" << std::hex << state.eip << std::dec << std::endl;
        }
    }

    std::cout << "\n[ALL STEPPING ENGINE & LIBRARY TESTS PASSED!]" << std::endl;
    emu.Shutdown();
    return 0;
}
