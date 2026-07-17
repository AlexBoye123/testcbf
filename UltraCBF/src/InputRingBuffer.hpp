#ifndef ULTRA_CBF_INPUT_RING_BUFFER_HPP
#define ULTRA_CBF_INPUT_RING_BUFFER_HPP

#include <atomic>
#include <array>
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

struct alignas(32) TimestampedInput {
    uint64_t qpcTimestamp;    // High-resolution hardware clock timestamp (QPC ticks / steady_clock nanoseconds)
    PlayerButton button;
    InputType type;
    bool isPlayer2;
    uint8_t rawDeviceId;
};

// Fixed-capacity, lock-free SPSC (Single Producer Single Consumer) ring buffer
// Explicitly cache-aligned (64-byte boundary) to completely prevent L1 CPU cache false sharing
template <size_t Capacity = 512>
class alignas(64) LockFreeInputBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for fast bitwise masking");

private:
    alignas(64) std::array<TimestampedInput, Capacity> m_buffer{};
    
    alignas(64) std::atomic<size_t> m_head{0}; // Modified exclusively by OS RawInput Thread
    alignas(64) std::atomic<size_t> m_tail{0}; // Modified exclusively by Cocos2d Physics Thread

public:
    LockFreeInputBuffer() = default;

    // Push new hardware timestamped input from OS thread (Zero dynamic heap allocations)
    bool push(const TimestampedInput& input) noexcept {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);
        const size_t currentTail = m_tail.load(std::memory_order_acquire);

        if ((currentHead - currentTail) >= Capacity) {
            // Buffer overflow drop guard - avoids locks or heap resize stalls
            return false;
        }

        m_buffer[currentHead & (Capacity - 1)] = input;
        m_head.store(currentHead + 1, std::memory_order_release);
        return true;
    }

    // Pop event in main GD physics thread
    bool pop(TimestampedInput& outInput) noexcept {
        const size_t currentTail = m_tail.load(std::memory_order_relaxed);
        const size_t currentHead = m_head.load(std::memory_order_acquire);

        if (currentTail == currentHead) {
            return false; // Queue empty
        }

        outInput = m_buffer[currentTail & (Capacity - 1)];
        m_tail.store(currentTail + 1, std::memory_order_release);
        return true;
    }

    size_t pendingCount() const noexcept {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);
        const size_t currentTail = m_tail.load(std::memory_order_relaxed);
        return (currentHead >= currentTail) ? (currentHead - currentTail) : 0;
    }

    void clear() noexcept {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);
        m_tail.store(currentHead, std::memory_order_release);
    }
};

} // namespace UltraCBF

#endif // ULTRA_CBF_INPUT_RING_BUFFER_HPP
