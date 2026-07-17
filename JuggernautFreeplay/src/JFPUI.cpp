#include "JFPUI.hpp"
#include "CorridorGenerator.hpp"
#include <Geode/modify/CreatorLayer.hpp>

using namespace geode::prelude;

namespace JFP {

void createWavemanLevel() {
    auto level = GJGameLevel::create();
    if (!level) return;

    level->m_levelName = "JFP Random Corridor";
    level->m_levelString = CorridorGenerator::get().generateLevelString(250);
    level->m_levelType = GJLevelType::Editor;

    auto scene = PlayLayer::scene(level, false, false);
    CCDirector::sharedDirector()->replaceScene(scene);
}

} // namespace JFP

class $modify(JFPMenuHook, CreatorLayer) {
    bool init() {
        if (!CreatorLayer::init()) return false;

        auto menu = this->getChildByID("creator-buttons-menu");
        if (!menu) {
            menu = this->getChildByID("bottom-menu");
        }

        if (menu) {
            auto spr = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
            if (spr) {
                spr->setScale(0.6f);
                auto btn = CCMenuItemSpriteExtra::create(
                    spr, this, menu_selector(JFPMenuHook::onJFPButton)
                );
                btn->setID("jfp-button");
                menu->addChild(btn);
                menu->updateLayout();
            }
        } else {
            log::warn("JFP: couldn't find a menu to attach the button to in CreatorLayer");
        }

        return true;
    }

    void onJFPButton(CCObject* sender) {
        JFP::createWavemanLevel();
    }
};
