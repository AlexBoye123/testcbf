#include "CorridorGenerator.hpp"
#include <sstream>

namespace JFP {

CorridorGenerator::CorridorGenerator() {
    std::random_device rd;
    setSeed(rd());
}

void CorridorGenerator::setSeed(uint32_t seed) {
    m_seed = seed;
    m_rng.seed(m_seed);
}

std::string CorridorGenerator::generateLevelString(size_t length) {
    std::stringstream ss;
    
    // Header setup: Speed 1x, Wave Gamemode (type 7)
    ss << "kS38,1,k3,7,";

    int currentY = 150;
    int currentX = 300;

    std::uniform_int_distribution<int> deltaYDist(-60, 60);
    std::uniform_int_distribution<int> portalDist(0, 100);

    for (size_t i = 0; i < length; ++i) {
        currentX += 60; // Step horizontal distance
        
        int deltaY = deltaYDist(m_rng);
        currentY = std::clamp(currentY + deltaY, 60, 600);

        // Place Top Spike Hazard (Object ID 8)
        ss << "1,8,2," << currentX << ",3," << (currentY + 45) << ",4,180;";

        // Place Bottom Spike Hazard (Object ID 8)
        ss << "1,8,2," << currentX << ",3," << (currentY - 45) << ";";

        // Procedural Gravity Portals (ID 10 for Blue, ID 11 for Yellow)
        if (m_gravityPortals && portalDist(m_rng) < 15) {
            int portalID = (i % 2 == 0) ? 10 : 11;
            ss << "1," << portalID << ",2," << (currentX + 30) << ",3," << currentY << ";";
        }

        // Procedural Speed Portals (ID 200 = 1x, ID 201 = 2x, ID 202 = 3x, ID 203 = 4x)
        if (m_speedPortals && portalDist(m_rng) < 10) {
            int speedID = 200 + (portalDist(m_rng) % 4);
            ss << "1," << speedID << ",2," << (currentX + 30) << ",3," << currentY << ";";
        }
    }

    return ss.str();
}

} // namespace JFP
