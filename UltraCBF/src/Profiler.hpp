#ifndef ULTRA_CBF_PROFILER_HPP
#define ULTRA_CBF_PROFILER_HPP

#include <cstdint>
#include <array>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <string>

namespace UltraCBF {

struct DiagnosticStats {
    double avgLatencyMicros{0.0};
    double minLatencyMicros{0.0};
    double maxLatencyMicros{0.0};
    double jitterMicros{0.0};
    double pollingRateHz{0.0};
    double effectiveTps{240.0};
    double catchTimeMicros{0.0};    // Time taken to catch & lock-free queue input in C++
    double cpuOverheadMicros{0.0};  // SPSC queue drain & vector extrapolation cost
    double lastSubTickAlpha{0.0};   // Continuous sub-frame phase percentage
};

template <size_t Samples = 128>
class PerformanceProfiler {
private:
    std::array<double, Samples> m_latencySamplesMicros{};
    size_t m_sampleIndex{0};
    size_t m_totalSampleCount{0};

    uint64_t m_lastInputCounterTimeQPC{0};
    uint32_t m_eventsInCurrentSecond{0};
    double m_calculatedPollingRateHz{0.0};

    uint64_t m_lastInputTimestampQPC{0};
    double m_effectiveTps{240.0};

    double m_lastCatchTimeMicros{0.0};
    double m_lastCpuOverheadMicros{0.0};
    double m_lastSubTickAlpha{0.0};
    bool m_hudVisible{true};

public:
    PerformanceProfiler() = default;

    void recordInputCatch(double catchMicros) {
        m_lastCatchTimeMicros = catchMicros;
    }

    void recordInputLatency(double latencyMicros, double alpha, uint64_t inputQPC, uint64_t currentQPC, uint64_t qpcFrequency) {
        m_latencySamplesMicros[m_sampleIndex % Samples] = latencyMicros;
        m_sampleIndex++;
        m_totalSampleCount++;

        m_lastSubTickAlpha = alpha;

        // Compute inter-input timing delta to determine real-time Effective Input TPS
        if (m_lastInputTimestampQPC > 0 && inputQPC > m_lastInputTimestampQPC) {
            double deltaSec = static_cast<double>(inputQPC - m_lastInputTimestampQPC) / static_cast<double>(qpcFrequency);
            if (deltaSec > 0.00001 && deltaSec < 0.1) {
                m_effectiveTps = 1.0 / deltaSec;
            }
        }
        m_lastInputTimestampQPC = inputQPC;

        // Compute Polling Frequency
        m_eventsInCurrentSecond++;
        double elapsedSec = static_cast<double>(currentQPC - m_lastInputCounterTimeQPC) / static_cast<double>(qpcFrequency);
        if (elapsedSec >= 1.0) {
            m_calculatedPollingRateHz = static_cast<double>(m_eventsInCurrentSecond) / elapsedSec;
            m_eventsInCurrentSecond = 0;
            m_lastInputCounterTimeQPC = currentQPC;
        }
    }

    void recordCpuOverhead(double cpuMicros) {
        m_lastCpuOverheadMicros = cpuMicros;
    }

    DiagnosticStats getStats() const {
        DiagnosticStats stats;
        stats.catchTimeMicros = m_lastCatchTimeMicros;
        stats.cpuOverheadMicros = m_lastCpuOverheadMicros;
        stats.pollingRateHz = m_calculatedPollingRateHz;
        stats.effectiveTps = m_effectiveTps;
        stats.lastSubTickAlpha = m_lastSubTickAlpha;

        size_t count = std::min(m_totalSampleCount, Samples);
        if (count == 0) return stats;

        double sum = 0.0;
        double minVal = m_latencySamplesMicros[0];
        double maxVal = m_latencySamplesMicros[0];

        for (size_t i = 0; i < count; ++i) {
            double val = m_latencySamplesMicros[i];
            sum += val;
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }

        stats.avgLatencyMicros = sum / static_cast<double>(count);
        stats.minLatencyMicros = minVal;
        stats.maxLatencyMicros = maxVal;

        // Jitter Calculation
        double varianceSum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            double diff = m_latencySamplesMicros[i] - stats.avgLatencyMicros;
            varianceSum += diff * diff;
        }
        stats.jitterMicros = std::sqrt(varianceSum / static_cast<double>(count));

        return stats;
    }

    bool isHudVisible() const { return m_hudVisible; }
    void setHudVisible(bool state) { m_hudVisible = state; }
};

} // namespace UltraCBF

#endif // ULTRA_CBF_PROFILER_HPP
