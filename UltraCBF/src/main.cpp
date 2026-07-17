#include <Geode/Geode.hpp>
#include "SubTickEngine.hpp"
#include "Hooks.hpp"

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#include <timeapi.h>

typedef NTSTATUS(NTAPI* pfnNtSetTimerResolution)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG ActualResolution);
#endif

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("=================================================");
    log::info(" UltraCBF Next-Gen Sub-Tick Engine Active ");
    log::info(" Target: Geometry Dash 2.2081 | Geode SDK v5.8.2 ");
    log::info(" Lock-Free SPSC Ring Buffer + Real-Time Profiler ");
    log::info("=================================================");

#ifdef GEODE_IS_WINDOWS
    // 1. Set Win32 Multimedia timer to 1ms minimum
    timeBeginPeriod(1);

    // 2. Elevate Windows NT Kernel Timer Resolution to 0.5ms (500 microseconds / 5000 100-ns units)
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        auto NtSetTimerResolution = reinterpret_cast<pfnNtSetTimerResolution>(GetProcAddress(hNtdll, "NtSetTimerResolution"));
        if (NtSetTimerResolution) {
            ULONG actualRes = 0;
            NtSetTimerResolution(5000, TRUE, &actualRes);
            log::info("[UltraCBF] Windows NT Kernel Timer Resolution boosted to 0.5ms (500us)!");
        }
    }

    // 3. Elevate current thread priority to ensure zero-latency context switching
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    log::info("[UltraCBF] Windows high-precision kernel timers & highest thread priority engaged.");
#endif

    // Calibrate QueryPerformanceCounter hardware frequency
    UltraCBF::SubTickEngine::get().init();

    // Listen for real-time setting changes via Geode API
    listenForSettingChanges<bool>("enable-engine", [](bool val) {
        UltraCBF::SubTickEngine::get().setEnabled(val);
        log::info("[UltraCBF] Sub-tick engine state set to: {}", val);
    });

    listenForSettingChanges<bool>("benchmark-overlay", [](bool val) {
        UltraCBF::SubTickEngine::get().getProfiler().setHudVisible(val);
        log::info("[UltraCBF] Benchmark HUD visibility set to: {}", val);
    });

    listenForSettingChanges<std::string>("polling-mode", [](std::string val) {
        log::info("[UltraCBF] Polling Mode updated to: {}", val);
    });

    listenForSettingChanges<bool>("deduplication", [](bool val) {
        UltraCBF::SubTickEngine::get().setDeduplicationEnabled(val);
        log::info("[UltraCBF] High-Hz Deduplication filter set to: {}", val);
    });

    listenForSettingChanges<bool>("subtick-extrapolation", [](bool val) {
        UltraCBF::SubTickEngine::get().setEnabled(val);
        log::info("[UltraCBF] Sub-tick extrapolation set to: {}", val);
    });
}
