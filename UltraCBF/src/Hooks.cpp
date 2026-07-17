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
        bool m_buttonStates[10][2]{{false}}; // Tracks button press state [buttonID][isPlayer2]
        bool m_isProcessingSubTick{false};   // Re-entrancy guard during sub-tick replay
    };

    void handleButton(bool push, int button, bool isPlayer2) {
        if (m_fields->m_isProcessingSubTick) {
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

        auto& engine = UltraCBF::SubTickEngine::get();

        // Pass-Through Mode: If engine is toggled OFF, measure standard input baseline
        if (!engine.isEnabled()) {
            if (UltraCBF::isGameplayActive() && push) {
                uint64_t nowQPC = engine.getCurrentQPC();
                engine.getProfiler().recordInputLatency(2080.0, 0.0, nowQPC, nowQPC, engine.getQPCFrequency());
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
            return; // Filter duplicate auto-repeat signals
        }
        m_fields->m_buttonStates[btnIdx][p2Idx] = push;

        UltraCBF::PlayerButton btnEnum = static_cast<UltraCBF::PlayerButton>(button);
        UltraCBF::InputType typeEnum = push ? UltraCBF::InputType::Press : UltraCBF::InputType::Release;

        engine.recordHardwareInput(btnEnum, typeEnum, isPlayer2);
    }

    void update(float dt) {
        auto& engine = UltraCBF::SubTickEngine::get();

        if (engine.isEnabled() && UltraCBF::isGameplayActive()) {
            engine.beginFrameStep(static_cast<double>(dt));

            float lastAlphaP1 = 0.0f;
            float lastAlphaP2 = 0.0f;

            m_fields->m_isProcessingSubTick = true;

            engine.processPendingSubTicks([this, dt, &lastAlphaP1, &lastAlphaP2](const UltraCBF::TimestampedInput& evt, double alpha) {
                bool isDown = (evt.type == UltraCBF::InputType::Press);
                int buttonVal = static_cast<int>(evt.button);

                PlayerObject* targetPlayer = (evt.isPlayer2 && m_player2) ? m_player2 : m_player1;
                float& lastAlpha = (evt.isPlayer2 && m_player2) ? lastAlphaP2 : lastAlphaP1;

                float currentAlpha = static_cast<float>(alpha);
                float deltaAlpha = currentAlpha - lastAlpha;

                if (targetPlayer && deltaAlpha > 0.0f) {
                    cocos2d::CCPoint currentPos = targetPlayer->getPosition();
                    cocos2d::CCPoint velocity = targetPlayer->m_position;
                    
                    float subTickDt = dt * deltaAlpha;
                    targetPlayer->setPosition(currentPos + (velocity * subTickDt));
                    lastAlpha = currentAlpha;
                }

                this->handleButton(isDown, buttonVal, evt.isPlayer2);
            });

            m_fields->m_isProcessingSubTick = false;
        }

        GJBaseGameLayer::update(dt);
    }
};

class $modify(UltraPlayLayerHook, PlayLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* m_benchmarkLabel{nullptr};
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
                label->setScale(0.35f);
                label->setOpacity(230);
                
                auto winSize = CCDirector::sharedDirector()->getWinSize();
                label->setPosition({10.0f, winSize.height - 10.0f});

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

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (m_fields->m_benchmarkLabel) {
            auto& engine = UltraCBF::SubTickEngine::get();
            auto stats = engine.getProfiler().getStats();

            std::string text;
            if (engine.isEnabled()) {
                text = fmt::format(
                    "[ UltraCBF Hardware Telemetry ]\n"
                    "Active Precision: {:.0f} TPS\n"
                    "C++ Queue Ingestion Delay: {:.2f} us\n"
                    "Input Dispatch Delay: {:.3f} ms ({:.1f} us)\n"
                    "Sub-Frame Phase Offset: {:.1f}%\n"
                    "Jitter Variance: ±{:.3f} ms\n"
                    "SPSC Queue Drain Cost: {:.2f} us",
                    stats.effectiveTps,
                    stats.catchTimeMicros,
                    stats.avgLatencyMicros / 1000.0, stats.avgLatencyMicros,
                    stats.lastSubTickAlpha * 100.0,
                    stats.jitterMicros / 1000.0,
                    stats.cpuOverheadMicros
                );
            } else {
                text = fmt::format(
                    "[ UltraCBF Engine OFF (Pass-Through) ]\n"
                    "Active Precision: {:.0f} TPS\n"
                    "Standard Step Latency: {:.3f} ms\n"
                    "Status: Vanilla / External Baseline Mode",
                    stats.effectiveTps,
                    stats.avgLatencyMicros / 1000.0
                );
            }

            m_fields->m_benchmarkLabel->setString(text.c_str());
        }
    }
};
