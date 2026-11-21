#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

class $modify(LarpPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto mod = Mod::get();
        auto win = CCDirector::get()->getWinSize();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("larp-buttons-menu"_spr);
        this->addChild(menu, 100);

        // 1. Upside-down pulse trigger, far left
        if (mod->getSettingValue<bool>("enable-pulse")) {
            CCNode* icon = CCSprite::createWithSpriteFrameName("edit_ePulseBtn_001.png");
            if (!icon) {
                auto fallback = CCLabelBMFont::create("Pulse", "bigFont.fnt");
                fallback->setScale(0.5f);
                icon = fallback;
            }
            icon->setRotation(180.f);

            auto btn = CCMenuItemSpriteExtra::create(
                icon, this, menu_selector(LarpPause::onPulse)
            );
            btn->setID("pulse-button"_spr);
            btn->setPosition({28.f, win.height / 2.f});
            menu->addChild(btn);
        }

        // 2. ZucchiniV2 checkbox, far right (does nothing)
        if (mod->getSettingValue<bool>("enable-zucchini")) {
            auto box = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(LarpPause::onZucchini), 0.7f
            );
            box->setID("zucchini-checkbox"_spr);
            box->setPosition({win.width - 100.f, win.height / 2.f});
            menu->addChild(box);

            auto label = CCLabelBMFont::create("ZucchiniV2", "bigFont.fnt");
            label->setScale(0.4f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({win.width - 84.f, win.height / 2.f});
            this->addChild(label, 100);
        }

        // 3. "Pause center" box, below the continue button
        if (mod->getSettingValue<bool>("enable-pause-center")) {
            auto ref = this->getChildByIDRecursive("resume-button");
            if (!ref) ref = this->getChildByIDRecursive("play-button");

            float width = 80.f;
            CCPoint pos = ccp(win.width / 2.f, win.height / 2.f - 80.f);
            if (ref) {
                width = ref->getScaledContentWidth();
                auto world = ref->getParent()->convertToWorldSpace(ref->getPosition());
                auto local = this->convertToNodeSpace(world);
                float h = width / 4.f;
                pos = ccp(local.x, local.y - ref->getScaledContentHeight() / 2.f - h / 2.f - 10.f);
            }

            auto spr = ButtonSprite::create(
                "Pause center", static_cast<int>(width), true,
                "bigFont.fnt", "GJ_button_01.png", width / 4.f, 0.35f
            );
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(LarpPause::onPauseCenter)
            );
            btn->setID("pause-center-button"_spr);
            btn->setPosition(pos);
            menu->addChild(btn);
        }
    }

    void onPulse(CCObject*) {
        FMODAudioEngine::sharedEngine()->playEffect("chestOpen01.ogg", 1.f, 1.f, 1.f);
        bool on = !Mod::get()->getSavedValue<bool>("reverse-pulse", false);
        Mod::get()->setSavedValue("reverse-pulse", on);
        Notification::create(
            on ? "reverse pulse mode: On" : "reverse pulse mode: Off",
            NotificationIcon::None
        )->show();
    }

    void onZucchini(CCObject*) {
        // does nothing, on purpose
    }

    void onPauseCenter(CCObject*) {
        this->onQuit(nullptr);
    }
};
