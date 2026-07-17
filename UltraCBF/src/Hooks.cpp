#include "Hooks.hpp"
#include "SubTickEngine.hpp"

using namespace geode::prelude;

namespace UltraCBF {

bool isGameplayActive() {
    auto* playLayer = PlayLayer::get();
    if (!playLayer) return false;
    return !playLayer->m_isPaused && playLayer->m_player1 != nullptr;
}

void installPlatformInputHooks() {
    log::info("[UltraCBF] Sub-tick engine hooked directly to Geode input dispatcher.");
}

} // namespace UltraCBF

// Geode Engine Modifiers for Geometry Dash 2.2081

class $modify(UltraGameLayerHook, GJBaseGameLayer) {
    struct Fields {
        bool m_buttonStates[10][2]{{false}};   // Tracks button press state [buttonID][isPlayer2]
        bool m_isProcessingSubTick{false};     // Re-entrancy guard during sub-tick replay
        uint64_t m_lastPassThroughPressQPC{0}; // Real-time timestamp tracking for Pass-Through mode
    };

    void handleButton(bool push, int button, bool isPlayer2) {
        if (m_fields->m_isProcessingSubTick) {
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

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

        if (push == m_fields->m_buttonStates[btnIdx][p2Idx]) {
            return; // Filter duplicate OS auto-repeat signals
        }
        m_fields->m_buttonStates[btnIdx][p2Idx] = push;

        UltraCBF::PlayerButton btnEnum = static_cast<UltraCBF::PlayerButton>(button);
        UltraCBF::InputType typeEnum = push ? UltraCBF::InputType::Press : UltraCBF::InputType::Release;

        engine.recordHardwareInput(btnEnum, typeEnum, isPlayer2);
    }

    void update(float dt) {
        auto& engine = UltraCBF::SubTickEngine::get();

        if (UltraCBF::isGameplayActive()) {
            if (engine.isEnabled()) {
                engine.beginFrameStep(static_cast<double>(dt));

                float lastAlpha = 0.0f;
                m_fields->m_isProcessingSubTick = true;

                // Check if level is currently in Dual Mode
                bool isDualActive = m_gameState.m_isDualMode && m_player2 != nullptr;

                engine.processPendingSubTicks([this, dt, isDualActive, &lastAlpha](const UltraCBF::TimestampedInput& evt, double alpha) {
                    bool isDown = (evt.type == UltraCBF::InputType::Press);
                    int buttonVal = static_cast<int>(evt.button);

                    float currentAlpha = static_cast<float>(alpha);
                    float deltaAlpha = currentAlpha - lastAlpha;

                    if (deltaAlpha > 0.0f) {
                        float subTickDt = dt * deltaAlpha;

                        // Continuous Position Extrapolation for Player 1
                        if (m_player1) {
                            cocos2d::CCPoint pos1 = m_player1->getPosition();
                            cocos2d::CCPoint vel1 = m_player1->m_position;
                            m_player1->setPosition(pos1 + (vel1 * subTickDt));
                        }

                        // Synchronous Lockstep Position Extrapolation for Player 2 (Keeps dual ships/cubes 100% aligned in X space)
                        if (isDualActive && m_player2) {
                            cocos2d::CCPoint pos2 = m_player2->getPosition();
                            cocos2d::CCPoint vel2 = m_player2->m_position;
                            m_player2->setPosition(pos2 + (vel2 * subTickDt));
                        }

                        lastAlpha = currentAlpha;
                    }

                    // Dispatch jump/action to targeted player (Player 1 or Player 2)
                    this->handleButton(isDown, buttonVal, evt.isPlayer2 && isDualActive);
                });

                m_fields->m_isProcessingSubTick = false;
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
        // Reset sub-tick state queues on practice checkpoint reset / level restart
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
