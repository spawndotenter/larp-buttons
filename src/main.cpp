#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

static constexpr int DO_BETTER_LEVEL_ID = 152277985;

// Put r4ndoom's GD account ID here (a number, not the username).
// (If this is ever set to 0, the button shows a "not set" message instead.)
static constexpr int R4NDOOM_ACCOUNT_ID = 25362634;

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

// Purple sideways mirror portal. Uses the game's portal sprite if it is loaded,
// otherwise draws a purple oval so the button always shows something.
static CCNode* makeMirrorPortalIcon() {
    auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    for (auto name : {"portal_05_front_001.png", "portal_06_front_001.png"}) {
        if (cache->spriteFrameByName(name)) {
            auto spr = CCSprite::createWithSpriteFrameName(name);
            spr->setColor({190, 90, 255});
            spr->setRotation(270.f); // sideways + upside down
            spr->setScale(40.f / spr->getContentHeight());
            return spr;
        }
    }

    auto holder = CCNode::create();
    holder->setContentSize({44.f, 30.f});

    auto makeOval = [](float rx, float ry) {
        std::vector<CCPoint> pts;
        for (int i = 0; i < 32; i++) {
            float a = static_cast<float>(i) / 32.f * 6.2831853f;
            pts.push_back(ccp(std::cos(a) * rx, std::sin(a) * ry));
        }
        return pts;
    };

    auto draw = CCDrawNode::create();
    auto outer = makeOval(21.f, 13.f);
    draw->drawPolygon(
        outer.data(), static_cast<unsigned int>(outer.size()),
        ccc4f(0.74f, 0.35f, 1.f, 1.f), 2.f, ccc4f(1.f, 1.f, 1.f, 1.f)
    );
    auto inner = makeOval(13.f, 7.f);
    draw->drawPolygon(
        inner.data(), static_cast<unsigned int>(inner.size()),
        ccc4f(0.35f, 0.1f, 0.6f, 1.f), 0.f, ccc4f(0.f, 0.f, 0.f, 0.f)
    );
    draw->setPosition({22.f, 15.f});
    holder->addChild(draw);
    return holder;
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

// ---------------------------------------------------------------------------
// Main menu: reverse mirror portal button above the daily chests
// ---------------------------------------------------------------------------
class $modify(LarpMenu, MenuLayer) {
    struct Fields {
        CCNode* m_helloBox = nullptr;
    };

    bool init() {
        if (!MenuLayer::init()) return false;

        if (!Mod::get()->getSettingValue<bool>("enable-mirror-portal")) return true;

        auto win = CCDirector::get()->getWinSize();

        CCPoint pos = ccp(win.width - 45.f, win.height / 2.f + 40.f);
        if (auto ref = this->getChildByIDRecursive("daily-chest-button")) {
            if (ref->getParent()) {
                auto world = ref->getParent()->convertToWorldSpace(ref->getPosition());
                auto local = this->convertToNodeSpace(world);
                pos = ccp(local.x, local.y + ref->getScaledContentHeight() / 2.f + 32.f);
            }
        }

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("larp-mirror-menu"_spr);
        this->addChild(menu, 50);

        auto btn = CCMenuItemSpriteExtra::create(
            makeMirrorPortalIcon(), this, menu_selector(LarpMenu::onMirrorPortal)
        );
        btn->setID("mirror-portal-button"_spr);
        btn->setPosition(pos);
        menu->addChild(btn);

        return true;
    }

    void onMirrorPortal(CCObject*) {
        if (m_fields->m_helloBox) {
            this->unschedule(schedule_selector(LarpMenu::onHideHello));
            m_fields->m_helloBox->removeFromParent();
            m_fields->m_helloBox = nullptr;
        }

        auto win = CCDirector::get()->getWinSize();

        auto box = CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
        box->setContentSize({170.f, 34.f});
        box->setColor({0, 0, 0});
        box->setOpacity(170);
        box->setPosition({win.width / 2.f, 30.f});

        auto label = CCLabelBMFont::create("hello", "bigFont.fnt");
        label->setScale(0.55f);
        label->setPosition({85.f, 17.f});
        box->addChild(label);

        this->addChild(box, 200);
        m_fields->m_helloBox = box;

        this->scheduleOnce(schedule_selector(LarpMenu::onHideHello), 2.f);
    }

    void onHideHello(float) {
        if (m_fields->m_helloBox) {
            m_fields->m_helloBox->removeFromParent();
            m_fields->m_helloBox = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Pause menu buttons
// ---------------------------------------------------------------------------
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

        // Locked vault (open door with the keyhole)
        if (mod->getSettingValue<bool>("enable-vault")) {
            auto icon = frameIcon(
                {"secretDoorBtn_open_001.png", "secretDoorBtn_closed_001.png", "GJ_safeBtn_001.png"},
                "Vault"
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

        // Do better Like, far right (above the checkbox)
        if (mod->getSettingValue<bool>("enable-do-better-like")) {
            auto holder = CCNode::create();
            holder->setContentSize({56.f, 58.f});

            auto like = frameIcon({"GJ_likeBtn_001.png"}, "Like");
            like->setPosition({28.f, 40.f});
            holder->addChild(like);

            auto label = CCLabelBMFont::create("do better", "bigFont.fnt");
            label->setScale(0.35f);
            label->setPosition({28.f, 10.f});
            holder->addChild(label);

            auto btn = CCMenuItemSpriteExtra::create(
                holder, this, menu_selector(LarpPause::onDoBetterLike)
            );
            btn->setID("do-better-like-button"_spr);
            btn->setPosition({win.width - 40.f, win.height / 2.f + 85.f});
            menu->addChild(btn);
        }

        // R4ndoom button, far right (below the checkbox)
        if (mod->getSettingValue<bool>("enable-r4ndoom")) {
            CCNode* icon = CCSprite::create("cube_459.png"_spr);
            if (icon) {
                icon->setScale(36.f / icon->getContentHeight());
            } else {
                auto label = CCLabelBMFont::create("r4ndoom", "bigFont.fnt");
                label->setScale(0.4f);
                icon = label;
            }
            auto btn = CCMenuItemSpriteExtra::create(
                icon, this, menu_selector(LarpPause::onR4ndoom)
            );
            btn->setID("r4ndoom-button"_spr);
            btn->setPosition({win.width - 40.f, win.height / 2.f - 85.f});
            menu->addChild(btn);
        }

        // Death, above the practice mode button
        if (mod->getSettingValue<bool>("enable-death")) {
            CCNode* ref = nullptr;
            for (auto id : {"practice-button", "practice-mode-button", "exit-practice-button"}) {
                ref = this->getChildByIDRecursive(id);
                if (ref) break;
            }

            CCPoint pos = ccp(win.width - 70.f, 90.f);
            if (ref && ref->getParent()) {
                auto world = ref->getParent()->convertToWorldSpace(ref->getPosition());
                auto local = this->convertToNodeSpace(world);
                pos = ccp(local.x, local.y + ref->getScaledContentHeight() / 2.f + 28.f);
            }

            auto icon = frameIcon({"spike_01_001.png", "spike_02_001.png"}, "Death");
            auto btn = CCMenuItemSpriteExtra::create(
                icon, this, menu_selector(LarpPause::onDeath)
            );
            btn->setID("death-button"_spr);
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

    void onVault(CCObject*) {
        FMODAudioEngine::sharedEngine()->playEffect("Kerplunck.mp3"_spr, 1.f, 1.f, 1.f);
        FLAlertLayer::create("Locked", "key required!", "OK")->show();
    }

    void onZucchini(CCObject*) {
        // does nothing, on purpose
    }

    void onPauseCenter(CCObject*) {
        this->onQuit(nullptr);
    }

    void onDoBetterLike(CCObject*) {
        auto level = GJGameLevel::create();
        level->m_levelID = DO_BETTER_LEVEL_ID;
        auto scene = LevelInfoLayer::scene(level, false);
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    }

    void onR4ndoom(CCObject*) {
        if (R4NDOOM_ACCOUNT_ID <= 0) {
            FLAlertLayer::create(
                "r4ndoom", "r4ndoom's account ID isn't set in the mod yet.", "OK"
            )->show();
            return;
        }
        ProfilePage::create(R4NDOOM_ACCOUNT_ID, false)->show();
    }

    void onDeath(CCObject*) {
        auto pl = PlayLayer::get();
        this->onResume(nullptr);
        if (pl && pl->m_player1) {
            pl->destroyPlayer(pl->m_player1, pl->m_anticheatSpike);
        }
    }
};
