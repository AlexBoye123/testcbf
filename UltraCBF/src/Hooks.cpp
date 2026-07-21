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
        uint64_t m_lastPassThroughPressQPC{0}; // Real-time timestamp tracking for Pass-Through mode
        bool m_isReplayingSubTickStep{false};
    };

    void resetLevel() {
        GJBaseGameLayer::resetLevel();

        for (int b = 0; b < 10; ++b) {
            m_fields->m_buttonStates[b][0] = false;
            m_fields->m_buttonStates[b][1] = false;
        }
        m_fields->m_lastPassThroughPressQPC = 0;
        m_fields->m_isReplayingSubTickStep = false;
    }

    void handleButton(bool push, int button, bool isPlayer2) {
        auto& engine = UltraCBF::SubTickEngine::get();

        // If we are currently executing a sub-tick replay step, invoke core GD button handler directly
        if (engine.isReplayingSubTick() || m_fields->m_isReplayingSubTickStep) {
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

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

        // Auto-Repeat Filter: Prevent duplicate OS key-hold messages
        if (push == m_fields->m_buttonStates[btnIdx][p2Idx]) {
            return;
        }
        m_fields->m_buttonStates[btnIdx][p2Idx] = push;

        UltraCBF::PlayerButton btnEnum = static_cast<UltraCBF::PlayerButton>(button);
        UltraCBF::InputType typeEnum = push ? UltraCBF::InputType::Press : UltraCBF::InputType::Release;

        // Enqueue event with high-resolution QPC hardware timestamp for sub-tick execution
        engine.recordHardwareInput(btnEnum, typeEnum, isPlayer2);
    }

    void update(float dt) {
        auto& engine = UltraCBF::SubTickEngine::get();

        if (UltraCBF::isGameplayActive()) {
            if (engine.isEnabled()) {
                engine.beginFrameStep(static_cast<double>(dt));

                float lastAlphaP1 = 0.0f;
                float lastAlphaP2 = 0.0f;

                bool isDualActive = m_gameState.m_isDualMode && m_player2 != nullptr;

                m_fields->m_isReplayingSubTickStep = true;

                // Process Player 1 Sub-Ticks: Advance player physics by exact fractional deltas
                engine.processPendingSubTicksForPlayer(false, [this, dt, &lastAlphaP1](const UltraCBF::TimestampedInput& evt, double alpha) {
                    float currentAlpha = static_cast<float>(alpha);
                    float deltaAlpha = currentAlpha - lastAlphaP1;

                    if (m_player1 && deltaAlpha > 0.0f) {
                        float subTickDt = dt * deltaAlpha;
                        m_player1->update(subTickDt);
                        lastAlphaP1 = currentAlpha;
                    }

                    bool isDown = (evt.type == UltraCBF::InputType::Press);
                    int buttonVal = static_cast<int>(evt.button);
                    this->handleButton(isDown, buttonVal, false);
                });

                // Complete remaining step fraction for Player 1
                float remainingP1 = 1.0f - lastAlphaP1;
                if (m_player1 && remainingP1 > 0.0f) {
                    m_player1->update(dt * remainingP1);
                }

                // Process Player 2 Sub-Ticks (Dual Mode)
                if (isDualActive && m_player2) {
                    engine.processPendingSubTicksForPlayer(true, [this, dt, &lastAlphaP2](const UltraCBF::TimestampedInput& evt, double alpha) {
                        float currentAlpha = static_cast<float>(alpha);
                        float deltaAlpha = currentAlpha - lastAlphaP2;

                        if (m_player2 && deltaAlpha > 0.0f) {
                            float subTickDt = dt * deltaAlpha;
                            m_player2->update(subTickDt);
                            lastAlphaP2 = currentAlpha;
                        }

                        bool isDown = (evt.type == UltraCBF::InputType::Press);
                        int buttonVal = static_cast<int>(evt.button);
                        this->handleButton(isDown, buttonVal, true);
                    });

                    float remainingP2 = 1.0f - lastAlphaP2;
                    if (m_player2 && remainingP2 > 0.0f) {
                        m_player2->update(dt * remainingP2);
                    }
                }

                m_fields->m_isReplayingSubTickStep = false;
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
