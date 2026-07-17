#include <Geode/Geode.hpp>
#include "SubTickEngine.hpp"
#include "Hooks.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("=================================================");
    log::info(" UltraCBF Next-Gen Sub-Tick Engine Active ");
    log::info(" Target: Geometry Dash 2.2081 | Geode SDK v5.8.2 ");
    log::info(" Lock-Free SPSC Ring Buffer + Real-Time Profiler ");
    log::info("=================================================");

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
