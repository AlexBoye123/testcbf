#ifndef ULTRA_CBF_PROFILER_HPP
#define ULTRA_CBF_PROFILER_HPP

#include <cstdint>
#include <array>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <string>

namespace UltraCBF {

struct LatencyStats {
    double avgLatencyMicros{0.0};
    double minLatencyMicros{0.0};
    double maxLatencyMicros{0.0};
    double jitterMicros{0.0};
    double pollingRateHz{0.0};
    double cpuOverheadMicros{0.0};
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

    double m_lastCpuOverheadMicros{0.0};
    bool m_hudVisible{true};

public:
    PerformanceProfiler() = default;

    void recordInputLatency(double latencyMicros, uint64_t currentQPC, uint64_t qpcFrequency) {
        m_latencySamplesMicros[m_sampleIndex % Samples] = latencyMicros;
        m_sampleIndex++;
        m_totalSampleCount++;

        // Calculate Hardware Polling Rate
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

    LatencyStats getStats() const {
        LatencyStats stats;
        stats.cpuOverheadMicros = m_lastCpuOverheadMicros;
        stats.pollingRateHz = m_calculatedPollingRateHz;

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

        // Calculate Jitter (Standard Deviation)
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
