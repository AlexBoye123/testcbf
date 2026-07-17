#include "Hooks.hpp"
#include "SubTickEngine.hpp"
#include <fmt/core.h>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace UltraCBF {

#ifdef GEODE_IS_WINDOWS
static WNDPROC g_originalWndProc = nullptr;

LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& engine = SubTickEngine::get();

    switch (msg) {
        case WM_INPUT: {
            UINT dwSize = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
            if (dwSize > 0) {
                std::vector<BYTE> lpb(dwSize);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(lpb.data());
                    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                        USHORT vkey = raw->data.keyboard.VKey;
                        bool isBreak = (raw->data.keyboard.Flags & RI_KEY_BREAK) != 0;

                        if (vkey == VK_SPACE || vkey == VK_UP || vkey == 'W') {
                            engine.recordHardwareInput(PlayerButton::Jump, isBreak ? InputType::Release : InputType::Press, false);
                        } else if (vkey == VK_RIGHT || vkey == 'D') {
                            engine.recordHardwareInput(PlayerButton::Right, isBreak ? InputType::Release : InputType::Press, false);
                        } else if (vkey == VK_LEFT || vkey == 'A') {
                            engine.recordHardwareInput(PlayerButton::Left, isBreak ? InputType::Release : InputType::Press, false);
                        }
                    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
                        USHORT flags = raw->data.mouse.usButtonFlags;
                        if (flags & RI_MOUSE_LEFT_BUTTON_DOWN) {
                            engine.recordHardwareInput(PlayerButton::Jump, InputType::Press, false);
                        } else if (flags & RI_MOUSE_LEFT_BUTTON_UP) {
                            engine.recordHardwareInput(PlayerButton::Jump, InputType::Release, false);
                        } else if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
                            engine.recordHardwareInput(PlayerButton::Jump, InputType::Press, true);
                        } else if (flags & RI_MOUSE_RIGHT_BUTTON_UP) {
                            engine.recordHardwareInput(PlayerButton::Jump, InputType::Release, true);
                        }
                    }
                }
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if ((lParam & (1 << 30)) == 0) { 
                if (wParam == VK_SPACE || wParam == VK_UP || wParam == 'W') {
                    engine.recordHardwareInput(PlayerButton::Jump, InputType::Press, false);
                }
            }
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if (wParam == VK_SPACE || wParam == VK_UP || wParam == 'W') {
                engine.recordHardwareInput(PlayerButton::Jump, InputType::Release, false);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            engine.recordHardwareInput(PlayerButton::Jump, InputType::Press, false);
            break;
        }
        case WM_LBUTTONUP: {
            engine.recordHardwareInput(PlayerButton::Jump, InputType::Release, false);
            break;
        }
        case WM_RBUTTONDOWN: {
            engine.recordHardwareInput(PlayerButton::Jump, InputType::Press, true);
            break;
        }
        case WM_RBUTTONUP: {
            engine.recordHardwareInput(PlayerButton::Jump, InputType::Release, true);
            break;
        }
    }

    return CallWindowProc(g_originalWndProc, hwnd, msg, wParam, lParam);
}

void installPlatformInputHooks() {
    auto* director = CCDirector::sharedDirector();
    if (!director) return;
    
    auto* glView = director->getOpenGLView();
    if (!glView) return;

#if defined(GEODE_IS_WINDOWS)
    HWND hwnd = glView->getWin32Window();
    if (hwnd) {
        if (!g_originalWndProc) {
            g_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CustomWndProc)));
            log::info("[UltraCBF] Low-latency Win32 WndProc hook attached.");
        }

        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06;
        rid[0].dwFlags = RIDEV_INPUTSINK;
        rid[0].hwndTarget = hwnd;

        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02;
        rid[1].dwFlags = RIDEV_INPUTSINK;
        rid[1].hwndTarget = hwnd;

        if (RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
            log::info("[UltraCBF] Driver-level WM_INPUT devices registered.");
        }
    }
#endif
}
#else
void installPlatformInputHooks() {
    log::info("[UltraCBF] High-resolution cross-platform input engine active.");
}
#endif

} // namespace UltraCBF

// Geode Engine Modifiers for Geometry Dash 2.2081

class $modify(UltraGameLayerHook, GJBaseGameLayer) {
    void update(float dt) {
        auto& engine = UltraCBF::SubTickEngine::get();

        if (engine.isEnabled()) {
            engine.beginFrameStep(static_cast<double>(dt));

            float lastAlphaP1 = 0.0f;
            float lastAlphaP2 = 0.0f;

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

        UltraCBF::installPlatformInputHooks();

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