#include "SubTickEngine.hpp"

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

namespace UltraCBF {

SubTickEngine::SubTickEngine() {
    init();
}

void SubTickEngine::init() {
#ifdef GEODE_IS_WINDOWS
    LARGE_INTEGER freq;
    if (QueryPerformanceFrequency(&freq)) {
        m_qpcFrequency = static_cast<uint64_t>(freq.QuadPart);
        m_secondsPerQpcTick = 1.0 / static_cast<double>(m_qpcFrequency);
    }
#else
    m_qpcFrequency = 1000000000ULL; // High-res steady clock nanoseconds for Mac/Linux/Android/iOS
    m_secondsPerQpcTick = 1e-9;
#endif

    uint64_t now = getCurrentQPC();
    m_previousFrameStartQPC = now;
    m_currentFrameStartQPC = now;
}

uint64_t SubTickEngine::getCurrentQPC() const noexcept {
#ifdef GEODE_IS_WINDOWS
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart);
#else
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
#endif
}

void SubTickEngine::recordHardwareInput(PlayerButton button, InputType type, bool isPlayer2) {
    if (!m_enabled) return;

    const uint64_t nowQPC = getCurrentQPC();

    // High-Frequency Hardware Jitter & Debounce Filter (200µs threshold):
    if (m_deduplicationEnabled && type == InputType::Press) {
        double elapsedSec = static_cast<double>(nowQPC - m_lastProcessedPressQPC) * m_secondsPerQpcTick;
        if (elapsedSec < 0.0002) { // 200 microseconds
            return;
        }
        m_lastProcessedPressQPC = nowQPC;
    }

    TimestampedInput evt{
        .qpcTimestamp = nowQPC,
        .button = button,
        .type = type,
        .isPlayer2 = isPlayer2,
        .rawDeviceId = 0
    };

    m_ringBuffer.push(evt);
}

void SubTickEngine::beginFrameStep(double targetDeltaSeconds) {
    m_previousFrameStartQPC = m_currentFrameStartQPC;
    m_currentFrameStartQPC = getCurrentQPC();

    // Fallback safety for first frame
    if (m_previousFrameStartQPC == 0 || m_previousFrameStartQPC >= m_currentFrameStartQPC) {
        uint64_t deltaTicks = static_cast<uint64_t>(targetDeltaSeconds * static_cast<double>(m_qpcFrequency));
        if (deltaTicks > 0 && m_currentFrameStartQPC >= deltaTicks) {
            m_previousFrameStartQPC = m_currentFrameStartQPC - deltaTicks;
        } else {
            m_previousFrameStartQPC = m_currentFrameStartQPC;
        }
    }
}

double SubTickEngine::calculateSubTickPhase(uint64_t qpcTimestamp) const noexcept {
    if (qpcTimestamp <= m_previousFrameStartQPC) {
        return 0.0; // Input occurred prior to or at start of elapsed step
    }
    if (qpcTimestamp >= m_currentFrameStartQPC) {
        return 1.0; // Input occurred at or after end of step
    }

    uint64_t elapsedStepTicks = m_currentFrameStartQPC - m_previousFrameStartQPC;
    if (elapsedStepTicks == 0) return 0.0;

    uint64_t eventTicks = qpcTimestamp - m_previousFrameStartQPC;
    double alpha = static_cast<double>(eventTicks) / static_cast<double>(elapsedStepTicks);

    if (alpha < 0.0) return 0.0;
    if (alpha > 1.0) return 1.0;
    return alpha;
}

void SubTickEngine::processPendingSubTicks(std::function<void(const TimestampedInput&, double alpha)> dispatchCallback) {
    uint64_t startTimeQPC = getCurrentQPC();

    TimestampedInput evt;
    while (m_ringBuffer.pop(evt)) {
        double alpha = calculateSubTickPhase(evt.qpcTimestamp);

        // Compute real-time hardware dispatch latency
        uint64_t dispatchQPC = getCurrentQPC();
        double latencyMicros = static_cast<double>(dispatchQPC - evt.qpcTimestamp) * m_secondsPerQpcTick * 1000000.0;
        m_profiler.recordInputLatency(latencyMicros, dispatchQPC, m_qpcFrequency);

        dispatchCallback(evt, alpha);
    }

    uint64_t endTimeQPC = getCurrentQPC();
    double cpuMicros = static_cast<double>(endTimeQPC - startTimeQPC) * m_secondsPerQpcTick * 1000000.0;
    m_profiler.recordCpuOverhead(cpuMicros);
}

} // namespace UltraCBF
