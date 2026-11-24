#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>

#include <asp/fs/std.hpp> // lets us keep using std::filesystem via toStdPath()

#include <cmath>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

using namespace geode::prelude;

static constexpr int DO_BETTER_LEVEL_ID = 152277985;
static constexpr int BUTTER_ACCOUNT_ID = 19696663;

// Put r4ndoom's GD account ID here (a number, not the username).
// (If this is ever set to 0, the button shows a "not set" message instead.)
static constexpr int R4NDOOM_ACCOUNT_ID = 25362634;

// Random volume from 0% to 100%
static float randomVolume() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 100);
    return static_cast<float>(dist(rng)) / 100.f;
}

// Death button: frames to wait (after unpausing) before killing the player.
static int g_killCountdown = 0;
static int g_killRetry = 0;

// The game pauses sound effects while the pause menu is open, so a normal
// playEffect only plays once you unpause. Resume effects right after so it plays now.
static void playEffectNow(char const* path) {
    auto engine = FMODAudioEngine::sharedEngine();
    engine->playEffect(path, 1.f, 1.f, 1.f);
    engine->resumeAllEffects();
}

// Plays a sound that ships inside the mod (listed under "files" in mod.json).
// Tries the real file path first, then the "mod id/name" form.
static void playModSound(std::string const& name) {
    std::error_code ec;
    // Geode v6: getResourcesDir() returns asp::fs::Path, convert for std::filesystem
    auto path = Mod::get()->getResourcesDir().toStdPath() / name;
    if (std::filesystem::exists(path, ec)) {
        auto full = path.string();
        playEffectNow(full.c_str());
    } else {
        auto prefixed = Mod::get()->getID() + "/" + name;
        playEffectNow(prefixed.c_str());
    }
}

// Runs something right after leaving a level, independent of any scene.
// It waits at least a couple of frames, and until the game's own exit fade has
// finished (opening a page during the fade would get cancelled by it).
class DelayedAction : public CCObject {
    std::function<void()> m_action;
    int m_frames = 0;
    int m_minFrames = 2;

public:
    void tick(float) {
        m_frames++;
        if (m_frames < m_minFrames) return;

        auto scene = CCDirector::get()->getRunningScene();
        bool fading = scene && typeinfo_cast<CCTransitionScene*>(scene);
        if (fading && m_frames < 240) return;

        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(DelayedAction::tick), this
        );
        if (m_action) m_action();
        this->release();
    }

    static void runSoon(std::function<void()> action) {
        auto obj = new DelayedAction();
        obj->m_action = std::move(action);
        CCDirector::get()->getScheduler()->scheduleSelector(
            schedule_selector(DelayedAction::tick), obj, 0.f, kCCRepeatForever, 0.f, false
        );
    }
};

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

static std::vector<CCPoint> circlePoints(float r) {
    std::vector<CCPoint> pts;
    for (int i = 0; i < 28; i++) {
        float a = static_cast<float>(i) / 28.f * 6.2831853f;
        pts.push_back(ccp(std::cos(a) * r, std::sin(a) * r));
    }
    return pts;
}

// Black orb with a red X at the top right (and a hidden green check in the same spot)
static CCNode* makeBlackOrbIcon(CCNode** xOut, CCNode** checkOut) {
    auto holder = CCNode::create();
    holder->setContentSize({44.f, 44.f});

    auto orb = CCDrawNode::create();
    auto outer = circlePoints(15.f);
    orb->drawPolygon(
        outer.data(), static_cast<unsigned int>(outer.size()),
        ccc4f(0.f, 0.f, 0.f, 1.f), 2.f, ccc4f(0.78f, 0.78f, 0.78f, 1.f)
    );
    auto inner = circlePoints(8.f);
    orb->drawPolygon(
        inner.data(), static_cast<unsigned int>(inner.size()),
        ccc4f(0.1f, 0.1f, 0.1f, 1.f), 1.f, ccc4f(0.45f, 0.45f, 0.45f, 1.f)
    );
    orb->setPosition({20.f, 20.f});
    holder->addChild(orb);

    auto red = ccc4f(1.f, 0.2f, 0.2f, 1.f);
    auto xMark = CCDrawNode::create();
    xMark->drawSegment(ccp(-5.f, -5.f), ccp(5.f, 5.f), 1.8f, red);
    xMark->drawSegment(ccp(-5.f, 5.f), ccp(5.f, -5.f), 1.8f, red);
    xMark->setPosition({36.f, 36.f});
    holder->addChild(xMark);

    auto green = ccc4f(0.2f, 1.f, 0.2f, 1.f);
    auto check = CCDrawNode::create();
    check->drawSegment(ccp(-5.f, 0.f), ccp(-1.5f, -4.f), 1.8f, green);
    check->drawSegment(ccp(-1.5f, -4.f), ccp(5.5f, 5.f), 1.8f, green);
    check->setPosition({36.f, 36.f});
    check->setVisible(false);
    holder->addChild(check);

    *xOut = xMark;
    *checkOut = check;
    return holder;
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
// Death button support: kills the player a few frames after the game unpauses
// ---------------------------------------------------------------------------
class $modify(LarpPlay, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (g_killCountdown > 0) {
            g_killCountdown--;
            if (g_killCountdown == 0 && m_player1 && !m_player1->m_isDead) {
                PlayLayer::destroyPlayer(m_player1, m_anticheatSpike);
                g_killRetry = 8;
            }
        } else if (g_killRetry > 0) {
            g_killRetry--;
            if (g_killRetry == 0 && m_player1 && !m_player1->m_isDead) {
                // backup attempt in case the first one was ignored
                PlayLayer::destroyPlayer(m_player1, nullptr);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Main menu: reverse mirror portal button above the More Games button
// ---------------------------------------------------------------------------
class $modify(LarpMenu, MenuLayer) {
    struct Fields {
        CCNode* m_helloBox = nullptr;
    };

    bool init() {
        if (!MenuLayer::init()) return false;

        this->addEasyFaceButton();

        if (!Mod::get()->getSettingValue<bool>("enable-mirror-portal")) return true;

        auto win = CCDirector::get()->getWinSize();

        CCPoint pos = ccp(win.width - 45.f, 110.f);
        if (auto ref = this->getChildByIDRecursive("more-games-button")) {
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

    void addEasyFaceButton() {
        if (!Mod::get()->getSettingValue<bool>("enable-easy-face")) return;
        auto bottom = this->getChildByID("bottom-menu");
        if (!bottom) return;

        CCNode* icon = nullptr;
        auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
        for (auto name : {"diffIcon_01_btn_001.png", "diffIcon_01_001.png"}) {
            if (cache->spriteFrameByName(name)) {
                icon = CCSprite::createWithSpriteFrameName(name);
                break;
            }
        }
        if (icon) {
            icon->setScale(45.f / icon->getContentHeight());
        } else {
            auto label = CCLabelBMFont::create("Easy", "bigFont.fnt");
            label->setScale(0.5f);
            icon = label;
        }

        auto btn = CCMenuItemSpriteExtra::create(
            icon, this, menu_selector(LarpMenu::onEasyFace)
        );
        btn->setID("easy-face-button"_spr);
        bottom->addChild(btn);
        bottom->updateLayout();
    }

    void onEasyFace(CCObject*) {
        // does nothing, on purpose
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
// Level page: "2.3" checkbox and an empty button, far left
// ---------------------------------------------------------------------------
class $modify(LarpInfo, LevelInfoLayer) {
    struct Fields {
        Ref<CCNode> m_playRef = nullptr;
        CCNode* m_rateBtn = nullptr;
        CCNode* m_ratedLabel = nullptr;
    };

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        this->addRateButton();

        auto mod = Mod::get();
        bool wantCheckbox = mod->getSettingValue<bool>("enable-2-3-checkbox");
        bool wantEmpty = mod->getSettingValue<bool>("enable-empty-button");
        if (!wantCheckbox && !wantEmpty) return true;

        CCMenuItemToggler* box = nullptr;
        if (wantCheckbox) {
            box = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(LarpInfo::onTwoThree), 0.7f
            );
            box->setID("two-three-checkbox"_spr);

            auto label = CCLabelBMFont::create("2.3", "bigFont.fnt");
            label->setScale(0.45f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({box->getContentSize().width + 4.f, box->getContentSize().height / 2.f});
            box->addChild(label);
        }

        CCMenuItemSpriteExtra* empty = nullptr;
        if (wantEmpty) {
            auto spr = ButtonSprite::create(
                "", 36, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.8f
            );
            empty = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(LarpInfo::onEmptyButton)
            );
            empty->setID("empty-button"_spr);
        }

        // Join the existing far-left column so we line up with other buttons/mods
        auto side = typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu"));
        if (side && side->getLayout()) {
            if (box) side->addChild(box);
            if (empty) side->addChild(empty);
            side->updateLayout();
        } else {
            auto menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            this->addChild(menu, 100);
            if (box) {
                box->setPosition({24.f, 70.f});
                menu->addChild(box);
            }
            if (empty) {
                empty->setPosition({24.f, 30.f});
                menu->addChild(empty);
            }
        }

        return true;
    }

    void addRateButton() {
        if (!Mod::get()->getSettingValue<bool>("enable-rate-level")) return;
        auto win = CCDirector::get()->getWinSize();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("rate-level-menu"_spr);
        this->addChild(menu, 100);

        auto spr = ButtonSprite::create(
            "rate level", 90, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.45f
        );
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(LarpInfo::onRateLevel)
        );
        btn->setID("rate-level-button"_spr);
        btn->setPosition({win.width / 2.f, 60.f});
        menu->addChild(btn);
        m_fields->m_rateBtn = btn;

        // Sit just beneath the play button, and stay there if the layout shifts
        if (auto ref = this->getChildByIDRecursive("play-button")) {
            m_fields->m_playRef = ref;
            this->schedule(schedule_selector(LarpInfo::alignRate));
            this->alignRate(0.f);
        }
    }

    void alignRate(float) {
        auto ref = m_fields->m_playRef.data();
        auto btn = m_fields->m_rateBtn;
        if (!ref || !btn || !ref->getParent()) return;

        auto world = ref->getParent()->convertToWorldSpace(ref->getPosition());
        auto local = this->convertToNodeSpace(world);
        btn->setPosition(ccp(local.x, local.y - ref->getScaledContentHeight() / 2.f - 14.f));

        if (m_fields->m_ratedLabel) {
            m_fields->m_ratedLabel->setPosition(ccp(local.x, btn->getPositionY() - 24.f));
        }
    }

    void onRateLevel(CCObject*) {
        if (m_fields->m_ratedLabel) {
            this->unschedule(schedule_selector(LarpInfo::onHideRated));
            m_fields->m_ratedLabel->removeFromParent();
            m_fields->m_ratedLabel = nullptr;
        }

        auto label = CCLabelBMFont::create("Rated!", "bigFont.fnt");
        label->setScale(0.6f);
        if (m_fields->m_rateBtn) {
            label->setPosition(ccp(
                m_fields->m_rateBtn->getPositionX(), m_fields->m_rateBtn->getPositionY() - 24.f
            ));
        }
        this->addChild(label, 200);
        m_fields->m_ratedLabel = label;

        this->scheduleOnce(schedule_selector(LarpInfo::onHideRated), 2.f);
    }

    void onHideRated(float) {
        if (m_fields->m_ratedLabel) {
            m_fields->m_ratedLabel->removeFromParent();
            m_fields->m_ratedLabel = nullptr;
        }
    }

    void onTwoThree(CCObject*) {
        // does nothing, on purpose
    }

    void onEmptyButton(CCObject*) {
        // does nothing, on purpose
    }
};

// ---------------------------------------------------------------------------
// Search page: butter button to the right of the X
// ---------------------------------------------------------------------------
class $modify(LarpSearch, LevelSearchLayer) {
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;
        if (!Mod::get()->getSettingValue<bool>("enable-butter")) return true;

        auto win = CCDirector::get()->getWinSize();

        CCNode* ref = this->getChildByIDRecursive("clear-search-button");
        if (!ref) ref = this->getChildByIDRecursive("clear-button");

        // The butter image is already a round button, so match the size of the X button
        float targetHeight = (ref ? ref->getScaledContentHeight() : 40.f);

        CCNode* icon = CCSprite::create("butter.png"_spr);
        if (icon) {
            icon->setScale(targetHeight / icon->getContentHeight());
        } else {
            auto label = CCLabelBMFont::create("Butter", "bigFont.fnt");
            label->setScale(0.4f);
            icon = label;
        }

        // Right next to the X, on its right side
        CCPoint pos = ccp(win.width - 30.f, win.height - 40.f);
        if (ref && ref->getParent()) {
            auto world = ref->getParent()->convertToWorldSpace(ref->getPosition());
            auto local = this->convertToNodeSpace(world);
            pos = ccp(
                local.x + ref->getScaledContentWidth() / 2.f + 6.f + icon->getScaledContentWidth() / 2.f,
                local.y
            );
        }

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("butter-menu"_spr);
        this->addChild(menu, 100);

        auto btn = CCMenuItemSpriteExtra::create(
            icon, this, menu_selector(LarpSearch::onButter)
        );
        btn->setID("butter-button"_spr);
        btn->setPosition(pos);
        menu->addChild(btn);

        return true;
    }

    void onButter(CCObject*) {
        ProfilePage::create(BUTTER_ACCOUNT_ID, false)->show();
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
        Ref<CCNode> m_resumeRef = nullptr;
        CCNode* m_pauseCenterBtn = nullptr;
        CCNode* m_infoBtn = nullptr;
        CCNode* m_orbX = nullptr;
        CCNode* m_orbCheck = nullptr;
        bool m_orbChecked = false;
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
                {"GJ_safeBtn_001.png", "secretDoorBtn_closed_001.png", "secretDoorBtn_open_001.png"},
                "Vault"
            );
            addLeft(icon, menu_selector(LarpPause::onVault), "vault-button"_spr);
        }

        // Black orb with an X that toggles to a check
        if (mod->getSettingValue<bool>("enable-black-orb")) {
            CCNode* xMark = nullptr;
            CCNode* checkMark = nullptr;
            auto icon = makeBlackOrbIcon(&xMark, &checkMark);
            m_fields->m_orbX = xMark;
            m_fields->m_orbCheck = checkMark;
            addLeft(icon, menu_selector(LarpPause::onBlackOrb), "black-orb-button"_spr);
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
                pos = ccp(local.x, local.y - ref->getScaledContentHeight() / 2.f - h / 2.f - 2.f);
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

            if (ref) {
                m_fields->m_resumeRef = ref;
                m_fields->m_pauseCenterBtn = btn;
            }
        }

        // Info button, top right of the continue button
        if (mod->getSettingValue<bool>("enable-continue-info")) {
            CCNode* ref = this->getChildByIDRecursive("resume-button");
            if (!ref) ref = this->getChildByIDRecursive("play-button");

            CCNode* icon = nullptr;
            auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
            if (cache->spriteFrameByName("GJ_infoIcon_001.png")) {
                icon = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
                icon->setScale(26.f / icon->getContentHeight());
            } else {
                auto label = CCLabelBMFont::create("i", "bigFont.fnt");
                label->setScale(0.6f);
                icon = label;
            }

            auto btn = CCMenuItemSpriteExtra::create(
                icon, this, menu_selector(LarpPause::onContinueInfo)
            );
            btn->setID("continue-info-button"_spr);
            btn->setPosition({win.width / 2.f + 45.f, win.height / 2.f + 25.f});
            menu->addChild(btn);

            m_fields->m_infoBtn = btn;
            if (ref && !m_fields->m_resumeRef.data()) m_fields->m_resumeRef = ref;
        }

        // Keep the pause center box and the info button lined up with the
        // continue button even if the layout shifts later.
        if (m_fields->m_resumeRef.data() && (m_fields->m_pauseCenterBtn || m_fields->m_infoBtn)) {
            this->schedule(schedule_selector(LarpPause::alignPauseCenter));
            this->alignPauseCenter(0.f);
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

            // Between the Normal Mode bar and the settings gear
            CCPoint likePos = ccp(win.width - 105.f, win.height / 2.f + 95.f);
            if (auto opts = this->getChildByIDRecursive("options-button")) {
                if (opts->getParent()) {
                    auto world = opts->getParent()->convertToWorldSpace(opts->getPosition());
                    auto local = this->convertToNodeSpace(world);
                    likePos = ccp(local.x - 70.f, local.y - 30.f);
                }
            }
            btn->setPosition(likePos);
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

        // Dice (random music volume) and "Both?" (random music + SFX), under the music slider
        if (mod->getSettingValue<bool>("enable-dice")) {
            CCPoint dicePos = ccp(win.width * 0.34f, 22.f);
            if (auto slider = this->getChildByIDRecursive("music-slider")) {
                if (slider->getParent()) {
                    auto world = slider->getParent()->convertToWorldSpace(slider->getPosition());
                    auto local = this->convertToNodeSpace(world);
                    dicePos = ccp(local.x - 40.f, local.y - 33.f);
                }
            }

            CCNode* diceIcon = CCSprite::create("dice.png"_spr);
            if (diceIcon) {
                diceIcon->setScale(28.f / diceIcon->getContentHeight());
            } else {
                auto label = CCLabelBMFont::create("Dice", "bigFont.fnt");
                label->setScale(0.4f);
                diceIcon = label;
            }
            auto diceBtn = CCMenuItemSpriteExtra::create(
                diceIcon, this, menu_selector(LarpPause::onDice)
            );
            diceBtn->setID("dice-button"_spr);
            diceBtn->setPosition(dicePos);
            menu->addChild(diceBtn);

            auto bothSpr = ButtonSprite::create(
                "Both?", 46, true, "bigFont.fnt", "GJ_button_01.png", 20.f, 0.4f
            );
            auto bothBtn = CCMenuItemSpriteExtra::create(
                bothSpr, this, menu_selector(LarpPause::onBoth)
            );
            bothBtn->setID("both-button"_spr);
            bothBtn->setPosition(ccp(dicePos.x + 50.f, dicePos.y));
            menu->addChild(bothBtn);
        }
    }

    void setMusicVolume(float v) {
        auto slider = typeinfo_cast<Slider*>(this->getChildByIDRecursive("music-slider"));
        if (slider) {
            slider->setValue(v);
            this->musicSliderChanged(slider->getThumb());
        } else {
            FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(v);
        }
    }

    void setSfxVolume(float v) {
        auto slider = typeinfo_cast<Slider*>(this->getChildByIDRecursive("sfx-slider"));
        if (slider) {
            slider->setValue(v);
            this->sfxSliderChanged(slider->getThumb());
        } else {
            FMODAudioEngine::sharedEngine()->setEffectsVolume(v);
        }
    }

    void onDice(CCObject*) {
        // the "chest still has time left" sound, then a random music volume
        playEffectNow("chestClick.ogg");
        this->setMusicVolume(randomVolume());
    }

    void onBoth(CCObject*) {
        this->setMusicVolume(randomVolume());
        this->setSfxVolume(randomVolume());
    }

    void alignPauseCenter(float) {
        auto ref = m_fields->m_resumeRef.data();
        if (!ref || !ref->getParent()) return;

        auto world = ref->getParent()->convertToWorldSpace(ref->getPosition());
        auto local = this->convertToNodeSpace(world);
        float w = ref->getScaledContentWidth();
        float ht = ref->getScaledContentHeight();

        if (auto btn = m_fields->m_pauseCenterBtn) {
            float h = w / 4.f;
            btn->setPosition(ccp(local.x, local.y - ht / 2.f - h / 2.f - 2.f));
        }
        if (auto info = m_fields->m_infoBtn) {
            info->setPosition(ccp(local.x + w * 0.38f, local.y + ht * 0.38f));
        }
    }

    void onContinueInfo(CCObject*) {
        FLAlertLayer::create("Unpause button", "Unpauses the level.", "OK")->show();
    }

    void onPulse(CCObject*) {
        playEffectNow("chestOpen01.ogg");
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
        playModSound("Kerplunck.mp3");
        FLAlertLayer::create("Locked", "key required!", "OK")->show();
    }

    void onBlackOrb(CCObject*) {
        bool on = !m_fields->m_orbChecked;
        m_fields->m_orbChecked = on;
        if (m_fields->m_orbX) m_fields->m_orbX->setVisible(!on);
        if (m_fields->m_orbCheck) m_fields->m_orbCheck->setVisible(on);
    }

    void onZucchini(CCObject*) {
        // does nothing, on purpose
    }

    void onPauseCenter(CCObject*) {
        this->onQuit(nullptr);
    }

    void onDoBetterLike(CCObject*) {
        // Leave the level first so you never come back to a half-paused game,
        // then open the Do Better page once the exit transition is done.
        DelayedAction::runSoon([] {
            auto level = GJGameLevel::create();
            level->m_levelID = DO_BETTER_LEVEL_ID;
            auto scene = LevelInfoLayer::scene(level, false);
            CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
        });
        this->onQuit(nullptr);
    }

    void onR4ndoom(CCObject*) {
        if (R4NDOOM_ACCOUNT_ID <= 0) {
            FLAlertLayer::create(
                "r4ndoom", "r4ndoom's account ID isn't set in the mod yet.", "OK"
            )->show();
            return;
        }
        // Leave the level first. Playing a level from the profile and then going
        // back into the old level crashes the game.
        DelayedAction::runSoon([] {
            ProfilePage::create(R4NDOOM_ACCOUNT_ID, false)->show();
        });
        this->onQuit(nullptr);
    }

    void onDeath(CCObject*) {
        g_killCountdown = 4;
        g_killRetry = 0;
        this->onResume(nullptr);
    }
};
