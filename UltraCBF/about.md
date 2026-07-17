# UltraCBF: Next-Gen Sub-Tick Engine

UltraCBF is a zero-allocation, lock-free sub-tick input engine for **Geometry Dash 2.2081** built on **Geode SDK v5.8.2**.

### Features:
- **Lock-Free SPSC Ring Buffer**: Cache-line isolated atomic queue (`alignas(64)`).
- **QPC Hardware Clocking**: Microsecond hardware timestamping.
- **200µs Hardware Deduplication**: Prevents 8kHz mouse lag and stutters.
- **Continuous Spatial Vector Extrapolation**: Micro-interpolates player spatial displacement.
