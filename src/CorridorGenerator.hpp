#ifndef JFP_CORRIDOR_GENERATOR_HPP
#define JFP_CORRIDOR_GENERATOR_HPP

#include <Geode/Geode.hpp>
#include <random>
#include <string>

namespace JFP {

enum class CorridorRule {
    Juggernaut,
    Classic,
    NarrowSlopes,
    NineCircles
};

class CorridorGenerator {
private:
    std::mt19937 m_rng;
    uint32_t m_seed{0};
    CorridorRule m_rule{CorridorRule::Juggernaut};
    bool m_speedPortals{true};
    bool m_gravityPortals{true};
    bool m_removeSpam{true};

    CorridorGenerator();

public:
    static CorridorGenerator& get() {
        static CorridorGenerator instance;
        return instance;
    }

    void setSeed(uint32_t seed);
    uint32_t getSeed() const { return m_seed; }

    void setRule(CorridorRule rule) { m_rule = rule; }
    void setSpeedPortals(bool state) { m_speedPortals = state; }
    void setGravityPortals(bool state) { m_gravityPortals = state; }
    void setRemoveSpam(bool state) { m_removeSpam = state; }

    // Generates procedurally generated wave corridor level strings for GD 2.2081
    std::string generateLevelString(size_t length = 200);
};

} // namespace JFP

#endif // JFP_CORRIDOR_GENERATOR_HPP
