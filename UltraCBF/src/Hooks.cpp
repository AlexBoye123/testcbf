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

        if (!engine.isEnabled() || !UltraCBF::isGameplayActive()) {
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

        // Ensure update loop is scheduled
        this->scheduleUpdate();

        auto& profiler = UltraCBF::SubTickEngine::get().getProfiler();
        if (profiler.isHudVisible()) {
            // Use bigFont.fnt which is 100% guaranteed to exist across all GD versions
            auto label = CCLabelBMFont::create("UltraCBF Profiler Loading...", "bigFont.fnt");
            if (label) {
                label->setAnchorPoint({0.0f, 1.0f});
                label->setAlignment(cocos2d::kCCTextAlignmentLeft);
                label->setScale(0.35f);
                label->setOpacity(230);
                
                auto winSize = CCDirector::sharedDirector()->getWinSize();
                label->setPosition({10.0f, winSize.height - 10.0f});

                // Attach to UILayer if available, else PlayLayer directly
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
