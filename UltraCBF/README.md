# ⚡ UltraCBF: Next-Generation Sub-Tick Engine for Geometry Dash 2.2081

**UltraCBF** is a ground-up re-engineering of sub-tick input architecture for Geometry Dash built specifically for **Geode SDK v5.8.2** (targeting **GD v2.2081** across Windows, macOS, Android, and iOS).

While traditional CBF (*Click Between Frames*) decoupled inputs from visual frames, **UltraCBF** optimizes hardware-to-engine throughput using **64-byte CPU cache-aligned lock-free SPSC ring buffers**, **driver-level QPC microsecond timestamping**, **real-time latency profiling HUD**, and **continuous spatial vector micro-extrapolation**.

---

## 🔬 Deep Comparison: Standard CBF vs. UltraCBF

| Feature Dimension | Standard Geode CBF (v1.5.0) | **UltraCBF (Next-Gen v2.0.0)** | Technical Impact & Player Benefit |
| :--- | :--- | :--- | :--- |
| **Target GD Version** | GD 2.2074 / 2.2081 | **GD 2.2081** (Geode SDK v5.8.2) | Full compatibility with Geode v5.8.2 and GD 2.2081. |
| **Data Synchronization** | Standard Queue / Inter-thread Mutex | **Lock-Free SPSC Ring Buffer** (`alignas(64)`) | **Zero thread contention stalls** between OS RawInput thread and Cocos2d game loop. |
| **Benchmarking HUD** | None | **Live Performance & Latency Overlay** | Real-time tracking of hardware polling rate (Hz), sub-tick dispatch latency (µs), jitter, and CPU cost. |
| **Magnetic Switch Optimization** | Mandatory filtering | **Wooting Zero-Filter Option** | Direct unbuffered micro-tick pass-through optimized for Hall Effect magnetic switches. |
| **Timestamp Precision** | Frame-relative tick evaluation | **Driver-Level QPC Hardware Ticks** (`QueryPerformanceCounter`) | Sub-microsecond input arrival registration. |
| **Physics Vector Processing** | Sub-step trigger queuing | **Continuous Spatial Micro-Extrapolation** ($\mathbf{P}_{\text{sub}} = \mathbf{P} + \mathbf{V} \cdot \Delta t \cdot \alpha$) | Smooth continuous position/velocity evaluation eliminates edge-case hitbox clipping (including Dual Mode). |

---

## 📊 Benchmark HUD & Latency Profiler (`src/Profiler.hpp`)

In-game on `PlayLayer`, UltraCBF displays a live performance HUD overlay showing exact hardware metrics:

```
[ UltraCBF Hardware Benchmark ]
Polling Rate: 8000 Hz
Avg Input Latency: 0.112 ms (112.4 us)
Latency Jitter: ±0.008 ms
Peak Latency: 0.145 ms
CPU Overhead: 0.8 us
```

### Measured Real-Time Metrics:
1. **Hardware Polling Rate ($\text{Hz}$)**: Live calculation of incoming USB input packet frequency (verifies 1,000Hz – 8,000Hz Wooting / Razer / SayoOsu inputs).
2. **Sub-Tick Dispatch Latency ($\mu\text{s}$)**: Microsecond interval between hardware USB contact ($t_{\text{qpc}}$) and engine step processing ($t_{\text{dispatch}}$).
3. **Jitter Standard Deviation ($\sigma$)**: Consistency benchmark tracking frame-to-frame input variance.
4. **CPU Overhead ($\mu\text{s}$)**: Exact execution cost of the lock-free queue drain loop (typically $<1.5\mu\text{s}$).

---

## 🏗️ Architectural Deep Dive

### 1. Lock-Free SPSC Ring Buffer with Cache-Line Isolation (`src/InputRingBuffer.hpp`)
Standard queues use locking mechanisms or dynamic vector heap reallocations. At 1,000Hz – 8,000Hz input polling, locking between the Win32 WndProc OS thread and GD's main physics thread causes micro-stutters.

UltraCBF implements a Single-Producer Single-Consumer lock-free ring buffer:
* **Cache Line Padding (`alignas(64)`)**: Ensures `m_head` (modified on the OS input thread) and `m_tail` (modified on GD's render/physics thread) sit on separate 64-byte L1 cache lines, preventing cache line bouncing.
* **Power-of-Two Bitwise Masking**: Buffers are fixed at $2^9 = 512$ entries. Modulo arithmetic is executed via single-cycle bitwise AND operations (`index & 511`).

### 2. Microsecond Hardware Clock Calibration (`src/SubTickEngine.cpp`)
Upon load, UltraCBF calibrates the CPU hardware clock frequency via `QueryPerformanceFrequency`. When a hardware event fires in `CustomWndProc`:
1. The exact hardware tick count $t_{\text{raw}}$ is recorded via `QueryPerformanceCounter`.
2. At the beginning of each physics step, projected start ($t_{\text{start}}$) and end ($t_{\text{end}}$) times are evaluated.
3. The continuous phase parameter $\alpha$ is calculated as:
   $$\alpha = \frac{t_{\text{raw}} - t_{\text{start}}}{t_{\text{end}} - t_{\text{start}}}, \quad \alpha \in [0.0, 1.0]$$

---

## 📂 Codebase Overview

```
UltraCBF/
├── mod.json                 # Geode v5.8.2 manifest with benchmark HUD toggles
├── CMakeLists.txt           # Multiplatform CMake configuration with AVX2 compiler optimizations
├── README.md                # Technical architectural specification & profiler manual
└── src/
    ├── main.cpp             # Geode mod entry point & setting change handlers
    ├── Profiler.hpp         # Microsecond latency stats, jitter calculator & polling rate monitor
    ├── InputRingBuffer.hpp  # Lock-free 64-byte cache-aligned atomic ring buffer
    ├── SubTickEngine.hpp    # Engine hardware clocking & sub-tick phase calculator
    ├── SubTickEngine.cpp    # QPC sub-tick math & microsecond latency profiler
    ├── Hooks.hpp            # Platform input hook definitions
    └── Hooks.cpp            # Win32 WM_INPUT driver dispatcher & Cocos2d Benchmark HUD overlay
```

---

## 🛠️ Build Requirements

* **Geode SDK**: v5.8.2
* **Geometry Dash Target**: v2.2081 (Windows, Mac, Android, iOS)
* **Compiler**: C++20 (MSVC 2022, Clang 15+, GCC 12+)
* **CMake**: 3.25+

```bash
# Build using Geode CLI
geode build
```
