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
        // If we are currently replaying a sub-tick event, call original core handler
        if (m_fields->m_isProcessingSubTick) {
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

        auto& engine = UltraCBF::SubTickEngine::get();

        // If outside active gameplay or engine disabled, pass straight to vanilla GD
        if (!engine.isEnabled() || !UltraCBF::isGameplayActive()) {
            GJBaseGameLayer::handleButton(push, button, isPlayer2);
            return;
        }

        // Auto-Repeat Filter: Prevent Windows key-hold repeat messages from generating 30 CPS spam
        int btnIdx = std::clamp(button, 0, 9);
        int p2Idx = isPlayer2 ? 1 : 0;

        if (push == m_fields->m_buttonStates[btnIdx][p2Idx]) {
            return; // Ignore redundant key-repeat signal
        }
        m_fields->m_buttonStates[btnIdx][p2Idx] = push;

        // Record high-resolution hardware timestamp immediately
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

                // Continuous Sub-Tick Spatial Vector Interpolation
                if (targetPlayer && deltaAlpha > 0.0f) {
                    cocos2d::CCPoint currentPos = targetPlayer->getPosition();
                    cocos2d::CCPoint velocity = targetPlayer->m_position;
                    
                    float subTickDt = dt * deltaAlpha;
                    targetPlayer->setPosition(currentPos + (velocity * subTickDt));
                    lastAlpha = currentAlpha;
                }

                // Dispatch button event to GD core
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
            auto label = CCLabelBMFont::create("UltraCBF Profiler Loading...", "chatFont.fnt");
            if (label) {
                label->setAnchorPoint({0.0f, 1.0f});
                label->setScale(0.45f);
                label->setPosition({10.0f, CCDirector::sharedDirector()->getWinSize().height - 10.0f});
                label->setOpacity(220);
                this->addChild(label, 9999);
                m_fields->m_benchmarkLabel = label;
            }
        }

        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (m_fields->m_benchmarkLabel) {
            auto& engine = UltraCBF::SubTickEngine::get();
            auto stats = engine.getProfiler().getStats();

            std::string text = fmt::format(
                "[ UltraCBF Hardware Benchmark ]\n"
                "Polling Rate: {:.0f} Hz\n"
                "Avg Input Latency: {:.3f} ms ({:.1f} us)\n"
                "Latency Jitter: ±{:.3f} ms\n"
                "Peak Latency: {:.3f} ms\n"
                "CPU Overhead: {:.1f} us",
                stats.pollingRateHz,
                stats.avgLatencyMicros / 1000.0, stats.avgLatencyMicros,
                stats.jitterMicros / 1000.0,
                stats.maxLatencyMicros / 1000.0,
                stats.cpuOverheadMicros
            );

            m_fields->m_benchmarkLabel->setString(text.c_str());
        }
    }
};
