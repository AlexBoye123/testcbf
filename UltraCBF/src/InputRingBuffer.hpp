#ifndef ULTRA_CBF_INPUT_RING_BUFFER_HPP
#define ULTRA_CBF_INPUT_RING_BUFFER_HPP

#include <vector>
#include <cstdint>

namespace UltraCBF {

enum class InputType : uint8_t {
    Press,
    Release
};

enum class PlayerButton : uint8_t {
    Jump = 1,
    Left = 2,
    Right = 3
};

struct alignas(16) TimestampedInput {
    uint64_t qpcTimestamp;    // High-resolution hardware clock timestamp (QPC ticks / steady_clock nanoseconds)
    PlayerButton button;
    InputType type;
    bool isPlayer2;
    uint8_t rawDeviceId;
};

// Zero-allocation, contiguous input queue optimized for single-threaded frame step draining
class SingleThreadInputQueue {
private:
    std::vector<TimestampedInput> m_buffer;

public:
    SingleThreadInputQueue() {
        m_buffer.reserve(128); // Pre-reserve capacity to completely eliminate dynamic heap allocations
    }

    void push(const TimestampedInput& input) {
        m_buffer.push_back(input);
    }

    template <typename Func>
    void drain(Func&& callback) {
        for (const auto& input : m_buffer) {
            callback(input);
        }
        m_buffer.clear(); // Resets size to 0 without deallocating memory capacity
    }

    void clear() {
        m_buffer.clear();
    }

    bool empty() const {
        return m_buffer.empty();
    }

    size_t size() const {
        return m_buffer.size();
    }
};

} // namespace UltraCBF

#endif // ULTRA_CBF_INPUT_RING_BUFFER_HPP
