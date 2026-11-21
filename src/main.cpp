#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// Uses the first game sprite that exists, otherwise shows text instead.
static CCNode* frameIcon(std::initializer_list<char const*> names, char const* fallbackText) {
    auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    for (auto name : names) {
        if (cache->spriteFrameByName(name)) {
            auto spr = CCSprite::createWithSpriteFrameName(name);
            spr->setScale(32.f / spr->getContentHeight());
            return spr;
        }
    }
    auto label = CCLabelBMFont::create(fallbackText, "bigFont.fnt");
    label->setScale(0.45f);
    return label;
}

class VerityPopup : public Popup {
protected:
    bool init() {
        if (!Popup::init(260.f, 120.f)) return false;
        this->setTitle("Verity");

        auto label = CCLabelBMFont::create(
            "verity mode: enabled\n(cool down 3 days)", "bigFont.fnt"
        );
        label->setScale(0.45f);
        label->setAlignment(kCCTextAlignmentCenter);
        m_mainLayer->addChildAtPosition(label, Anchor::Center, {0.f, -8.f});
        return true;
    }

public:
    static VerityPopup* create() {
        auto ret = new VerityPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(LarpPause, PauseLayer) {
    struct Fields {
        CCNode* m_refreshIcon = nullptr;
        CCNode* m_spinner = nullptr;
        bool m_refreshing = false;
    };

    void customSetup() {
        PauseLayer::customSetup();

        auto mod = Mod::get();
        auto win = CCDirector::get()->getWinSize();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("larp-buttons-menu"_spr);
        this->addChild(menu, 100);

        // Left column, top to bottom
        float y = win.height / 2.f + 75.f;
        auto addLeft = [&](CCNode* icon, SEL_MenuHandler cb, std::string const& id) {
            auto btn = CCMenuItemSpriteExtra::create(icon, this, cb);
            btn->setID(id);
            btn->setPosition({28.f, y});
            menu->addChild(btn);
            y -= 45.f;
            return btn;
        };

        // Upside-down pulse trigger
        if (mod->getSettingValue<bool>("enable-pulse")) {
            auto icon = frameIcon({"edit_ePulseBtn_001.png"}, "Pulse");
            icon->setRotation(180.f);
            addLeft(icon, menu_selector(LarpPause::onPulse), "pulse-button"_spr);
        }

        // Verity (uses ball.png from the repo)
        if (mod->getSettingValue<bool>("enable-verity")) {
            CCNode* icon = CCSprite::create("ball.png"_spr);
            if (icon) {
                icon->setScale(32.f / icon->getContentHeight());
            } else {
                auto label = CCLabelBMFont::create("Verity", "bigFont.fnt");
                label->setScale(0.4f);
                icon = label;
            }
            addLeft(icon, menu_selector(LarpPause::onVerity), "verity-button"_spr);
        }

        // Refresh nothing
        if (mod->getSettingValue<bool>("enable-refresh")) {
            auto icon = frameIcon({"GJ_updateBtn_001.png"}, "Refresh");
            m_fields->m_refreshIcon = icon;
            addLeft(icon, menu_selector(LarpPause::onRefresh), "refresh-button"_spr);
        }

        // Locked vault
        if (mod->getSettingValue<bool>("enable-vault")) {
            auto icon = frameIcon(
                {"GJ_safeBtn_001.png", "secretDoorBtn_closed_001.png"}, "Vault"
            );
            addLeft(icon, menu_selector(LarpPause::onVault), "vault-button"_spr);
        }

        // ZucchiniV2 checkbox, far right (does nothing)
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

        // "Pause center" box, below the continue button
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

    void onVerity(CCObject*) {
        if (auto p = VerityPopup::create()) p->show();
    }

    void onRefresh(CCObject* sender) {
        if (m_fields->m_refreshing) return;
        auto btn = static_cast<CCNode*>(sender);
        auto icon = m_fields->m_refreshIcon;
        if (!btn || !icon || !btn->getParent()) return;

        m_fields->m_refreshing = true;
        icon->setVisible(false);

        CCNode* spinner = CCSprite::create("loadingCircle.png");
        if (spinner) {
            spinner->setScale(28.f / spinner->getContentHeight());
            spinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.f, 360.f)));
        } else {
            auto dots = CCLabelBMFont::create("...", "bigFont.fnt");
            dots->setScale(0.5f);
            spinner = dots;
        }
        spinner->setPosition(btn->getPosition());
        btn->getParent()->addChild(spinner);
        m_fields->m_spinner = spinner;

        this->scheduleOnce(schedule_selector(LarpPause::onRefreshDone), 2.f);
    }

    void onRefreshDone(float) {
        if (m_fields->m_spinner) {
            m_fields->m_spinner->removeFromParent();
            m_fields->m_spinner = nullptr;
        }
        if (m_fields->m_refreshIcon) m_fields->m_refreshIcon->setVisible(true);
        m_fields->m_refreshing = false;
    }

    void onVault(CC
