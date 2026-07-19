#ifndef ULTRA_CBF_SUB_TICK_ENGINE_HPP
#define ULTRA_CBF_SUB_TICK_ENGINE_HPP

#include "InputRingBuffer.hpp"
#include "Profiler.hpp"
#include <Geode/Geode.hpp>
#include <functional>

namespace UltraCBF {

class SubTickEngine {
private:
    SingleThreadInputQueue m_inputQueue;
    PerformanceProfiler<128> m_profiler;
    
    // Hardware Clock Calibration Data
    uint64_t m_qpcFrequency{1};
    uint64_t m_previousFrameStartQPC{0};
    uint64_t m_currentFrameStartQPC{0};
    double m_secondsPerQpcTick{0.0};

    // Sub-Tick Execution Flags
    bool m_enabled{true};
    bool m_deduplicationEnabled{false}; // Disabled by default for Wooting / Hall Effect magnetic switch keyboards
    bool m_isReplayingSubTick{false};   // Re-entrancy flag to prevent infinite event loop recursion
    uint64_t m_lastProcessedPressQPC{0};

    SubTickEngine();

public:
    static SubTickEngine& get() {
        static SubTickEngine instance;
        return instance;
    }

    // Initialize high-resolution timer frequency & OS hooks
    void init();

    // Record high-precision raw hardware input event from main thread
    void recordHardwareInput(PlayerButton button, InputType type, bool isPlayer2);

    // Set frame boundaries at the beginning of GD step update
    void beginFrameStep(double targetDeltaSeconds);

    // Compute exact continuous sub-tick phase [0.0, 1.0] for a raw input timestamp
    double calculateSubTickPhase(uint64_t qpcTimestamp) const noexcept;

    // Drain lock-free buffer and invoke callback with calculated fractional phase alpha
    void processPendingSubTicks(std::function<void(const TimestampedInput&, double alpha)> dispatchCallback);

    // Utility clock queries
    uint64_t getCurrentQPC() const noexcept;
    uint64_t getQPCFrequency() const noexcept { return m_qpcFrequency; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool state) { m_enabled = state; }
    
    bool isDeduplicationEnabled() const { return m_deduplicationEnabled; }
    void setDeduplicationEnabled(bool state) { m_deduplicationEnabled = state; }

    bool isReplayingSubTick() const { return m_isReplayingSubTick; }

    PerformanceProfiler<128>& getProfiler() { return m_profiler; }
};

} // namespace UltraCBF

#endif // ULTRA_CBF_SUB_TICK_ENGINE_HPP
