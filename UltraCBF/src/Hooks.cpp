#include "Hooks.hpp"
#include "SubTickEngine.hpp"

using namespace geode::prelude;

namespace UltraCBF {

bool isGameplayActive() {
    auto* playLayer = PlayLayer::get();
    if (!playLayer) return false;
    return !playLayer->m_isPaused && playLayer->m_player1 != nullptr;
}

// Decompiled GD 2.2 mode force scaling factor for analytical gravity acceleration
float getGamemodeForceScale(PlayerObject* player) {
    if (!player) return 1.0f;
    bool isMini = (player->m_vehicleSize != 1.0f);

    if (player->m_isShip)  return isMini ? 0.5875f : 0.4700f;
    if (player->m_isBird)  return isMini ? 0.7250f : 0.5800f; // UFO
    if (player->m_isSwing) return isMini ? 0.6154f : 0.4000f;
    if (player->m_isBall || player->m_isSpider) return 0.6000f;
    if (player->m_isRobot) return 0.9000f;

    return 1.0f;
}

// Extrapolate player position using true velocity vectors (Vx from getCurrentXVelocity, Vy from m_yVelocity)
void extrapolatePlayerPosition(PlayerObject* player, float subTickDt) {
    if (!player) return;

    float vx = player->m_isPlatformer ? static_cast<float>(player->m_platformerXVelocity) : static_cast<float>(player->getCurrentXVelocity());
    float vy = static_cast<float>(player->m_yVelocity);

    // 1. Slope / D-Block Sliding Safety Guard
    if (player->m_isSliding) {
        cocos2d::CCPoint pos = player->getPosition();
        player->setPosition({pos.x + (vx * subTickDt), pos.y + (vy * subTickDt)});
        return;
    }

    // 2. Free-Air Continuous Kinematic Extrapolation: s = v*t + 0.5*g*scale*t^2
    cocos2d::CCPoint pos = player->getPosition();

    float gravityTerm = 0.0f;
    if (!player->m_isDart) { // Wave uses linear constant slope; Gravity modes use quadratic scale
        float g = player->m_gravity;
        float forceScale = getGamemodeForceScale(player);
        gravityTerm = 0.5f * (g * forceScale) * (subTickDt * subTickDt);
    }

    cocos2d::CCPoint displacement{
        vx * subTickDt,
        (vy * subTickDt) + gravityTerm
    };

    player->setPosition(pos + displacement);
}

void installPlatformInputHooks() {
    log::info("[UltraCBF] Sub-tick engine hooked directly to Geode input dispatcher.");
}

} // namespace UltraCBF

// Geode Engine Modifiers for Geometry Dash 2.2081

class $modify(UltraGameLayerHook, GJBaseGameLayer) {
    struct Fields {
        bool m_buttonStates[10][2]{{false}};   // Tracks button press state [buttonID][isPlayer2]
        uint64_t m_lastPassThroughPressQPC{0}; // Real-time timestamp tracking for Pass-Through mode
    };

    void resetLevel() {
        GJBaseGameLayer::resetLevel();

        for (int b = 0; b < 10; ++b) {
            m_fields->m_buttonStates[b][0] = false;
            m_fields->m_buttonStates[b][1] = false;
        }
        m_fields->m_lastPassThroughPressQPC = 0;
    }

    void handleButton(bool push, int button, bool isPlayer2) {
        auto& engine = UltraCBF::SubTickEngine::get();

        // Pass-Through Mode: Record physical key press time for Vanilla step latency comparison
        if (!engine.isEnabled()) {
            if (UltraCBF::isGameplayActive() && push) {
                m_fields->m_lastPassThroughPressQPC = engine.getCurrentQPC();
            }
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

        if (!UltraCBF::isGameplayActive()) {
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

        int btnIdx = std::clamp(button, 0, 9);
        int p2Idx = isPlayer2 ? 1 : 0;

        // Auto-Repeat Filter: Prevent OS auto-repeat signals while key is held down
        if (push == m_fields->m_buttonStates[btnIdx][p2Idx]) {
            return;
        }
        m_fields->m_buttonStates[btnIdx][p2Idx] = push;

        // Record high-resolution microsecond timestamp immediately
        UltraCBF::PlayerButton btnEnum = static_cast<UltraCBF::PlayerButton>(button);
        UltraCBF::InputType typeEnum = push ? UltraCBF::InputType::Press : UltraCBF::InputType::Release;
        engine.recordHardwareInput(btnEnum, typeEnum, isPlayer2);

        // Execute core GD jump force immediately so player action registers with zero delay
        GJBaseGameLayer::handleButton(push, button, isPlayer2);
    }

    void update(float dt) {
        auto& engine = UltraCBF::SubTickEngine::get();

        if (UltraCBF::isGameplayActive()) {
            if (engine.isEnabled()) {
                engine.beginFrameStep(static_cast<double>(dt));

                float lastAlphaP1 = 0.0f;
                float lastAlphaP2 = 0.0f;

                bool isDualActive = m_gameState.m_isDualMode && m_player2 != nullptr;

                // Process Player 1 Sub-Ticks for spatial vector extrapolation
                engine.processPendingSubTicksForPlayer(false, [this, dt, &lastAlphaP1](const UltraCBF::TimestampedInput& evt, double alpha) {
                    float currentAlpha = static_cast<float>(alpha);
                    float deltaAlpha = currentAlpha - lastAlphaP1;

                    if (m_player1 && deltaAlpha > 0.0f) {
                        float subTickDt = dt * deltaAlpha;
                        UltraCBF::extrapolatePlayerPosition(m_player1, subTickDt);
                        lastAlphaP1 = currentAlpha;
                    }
                });

                // Process Player 2 Sub-Ticks (Dual Mode)
                if (isDualActive && m_player2) {
                    engine.processPendingSubTicksForPlayer(true, [this, dt, &lastAlphaP2](const UltraCBF::TimestampedInput& evt, double alpha) {
                        float currentAlpha = static_cast<float>(alpha);
                        float deltaAlpha = currentAlpha - lastAlphaP2;

                        if (m_player2 && deltaAlpha > 0.0f) {
                            float subTickDt = dt * deltaAlpha;
                            UltraCBF::extrapolatePlayerPosition(m_player2, subTickDt);
                            lastAlphaP2 = currentAlpha;
                        }
                    });
                }
            } else if (m_fields->m_lastPassThroughPressQPC > 0) {
                // True Vanilla Physical Step Latency Calculation
                uint64_t nowQPC = engine.getCurrentQPC();
                double pollDeltaMicros = static_cast<double>(nowQPC - m_fields->m_lastPassThroughPressQPC) * (1.0 / static_cast<double>(engine.getQPCFrequency())) * 1000000.0;
                
                double alpha = engine.calculateSubTickPhase(m_fields->m_lastPassThroughPressQPC);
                double stepDurationMicros = static_cast<double>(dt) * 1000000.0;
                
                double totalVanillaLatencyMicros = pollDeltaMicros + ((1.0 - alpha) * stepDurationMicros);

                engine.getProfiler().recordInputLatency(totalVanillaLatencyMicros, alpha, m_fields->m_lastPassThroughPressQPC, nowQPC, engine.getQPCFrequency());
                m_fields->m_lastPassThroughPressQPC = 0;
            }
        }

        GJBaseGameLayer::update(dt);
    }
};

class $modify(UltraPlayLayerHook, PlayLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* m_benchmarkLabel{nullptr};
        float m_hudAccumulator{0.0f};
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        auto& profiler = UltraCBF::SubTickEngine::get().getProfiler();
        if (profiler.isHudVisible()) {
            auto label = CCLabelBMFont::create("UltraCBF Telemetry Initializing...", "bigFont.fnt");
            if (label) {
                label->setAnchorPoint({0.0f, 1.0f});
                label->setAlignment(cocos2d::kCCTextAlignmentLeft);
                label->setScale(0.30f);
                label->setOpacity(235);
                
                auto winSize = CCDirector::sharedDirector()->getWinSize();
                label->setPosition({20.0f, winSize.height - 15.0f});

                if (m_uiLayer) {
                    m_uiLayer->addChild(label, 99999);
                } else {
                    this->addChild(label, 99999);
                }

                m_fields->m_benchmarkLabel = label;
            }
        }

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        UltraCBF::SubTickEngine::get().init();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (m_fields->m_benchmarkLabel) {
            m_fields->m_hudAccumulator += dt;

            if (m_fields->m_hudAccumulator >= 0.25f) {
                m_fields->m_hudAccumulator = 0.0f;

                auto& engine = UltraCBF::SubTickEngine::get();
                auto stats = engine.getProfiler().getStats();

                std::string text;
                if (engine.isEnabled()) {
                    text = fmt::format(
                        "[ Telemetry: UltraCBF Active ]\n"
                        "Precision: {:.0f} TPS\n"
                        "Queue Ingest: {:.2f} us\n"
                        "Input Delay: {:.3f} ms ({:.1f} us)\n"
                        "Phase Offset: {:.1f}%\n"
                        "Jitter: +- {:.3f} ms\n"
                        "SPSC Drain: {:.2f} us",
                        stats.effectiveTps,
                        stats.catchTimeMicros,
                        stats.avgLatencyMicros / 1000.0, stats.avgLatencyMicros,
                        stats.lastSubTickAlpha * 100.0,
                        stats.jitterMicros / 1000.0,
                        stats.cpuOverheadMicros
                    );
                } else {
                    text = fmt::format(
                        "[ Telemetry: UltraCBF OFF ]\n"
                        "Precision: {:.0f} TPS\n"
                        "Vanilla Delay: {:.3f} ms ({:.1f} us)\n"
                        "Phase Offset: 0.0% (Step Rounded)\n"
                        "Jitter: +- {:.3f} ms\n"
                        "Status: Pass-Through Mode",
                        stats.effectiveTps,
                        stats.avgLatencyMicros / 1000.0, stats.avgLatencyMicros,
                        stats.jitterMicros / 1000.0
                    );
                }

                m_fields->m_benchmarkLabel->setString(text.c_str());
            }
        }
    }
};
