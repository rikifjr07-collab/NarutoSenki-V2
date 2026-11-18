#include <time.h>
#include "StartMenu.h"
#include "Constants/UiFlowKeys.hpp"
#include "Network/LanManager.h"

#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include <jni.h>
#include "../../../cocos2dx/platform/android/jni/JniHelper.h"
#endif

SelectLayer::~SelectLayer()
{
	CC_SAFE_RELEASE(_selectList);

	auto handler = getGameModeHandler();
	handler->selectLayer = nullptr;
	if (!isStart)
	{
		handler->resetCheats();
		LanManager::getInstance()->disconnect();
	}
}

void SelectLayer::onGameStart()
{
	if (isStart)
		return;

	auto lan = LanManager::getInstance();
	if (lan->isHost())
	{
		lan->hostStartGame();
	}
	else if (lan->isClient() && !lan->isGameStartedSignal())
	{
		// Client tidak bisa memulai sendiri sebelum Host menekan Start
		return;
	}

	isStart = true;
	lan->clearGameStartedSignal();
	unschedule(schedule_selector(SelectLayer::updateNetworkLobby));
	setKeypadEnabled(false);

	SimpleAudioEngine::sharedEngine()->stopBackgroundMusic(true);
	SimpleAudioEngine::sharedEngine()->playEffect("Audio/Menu/confirm.ogg");

	Scene *loadScene = Scene::create();
	LoadLayer *loadLayer = LoadLayer::create();

	auto gameModeHandler = getGameModeHandler();
	gameModeHandler->selectLayer = this;
	gameModeHandler->onInitHeros();
	loadLayer->preloadAudio();
	loadScene->addChild(loadLayer);

	Director::sharedDirector()->replaceScene(TransitionFade::create(1.25f, loadScene));
}

void SelectLayer::updateNetworkLobby(float dt)
{
	auto ud = UserDefault::sharedUserDefault();
	auto lan = LanManager::getInstance();

	// 1. Baca perintah dari Lua (SelectLayer.lua)
	string cmd = ud->getStringForKey("net_cmd", "");
	if (!cmd.empty())
	{
		ud->setStringForKey("net_cmd", "");
		if (cmd == "HOST")
		{
			lan->startHost();
		}
		else if (cmd.find("JOIN:") == 0)
		{
			lan->joinHost(cmd.substr(5));
		}
		else if (cmd.find("MOVE:") == 0)
		{
			lan->requestMoveSlot(atoi(cmd.substr(5).c_str()));
		}
		else if (cmd.find("HERO:") == 0)
		{
			lan->requestSelectHero(cmd.substr(5));
		}
		else if (cmd == "DISCONNECT")
		{
			lan->disconnect();
		}
	}

	// 2. Tulis status lobby & ke-6 slot ke UserDefault untuk ditampilkan Lua
	ud->setStringForKey("net_status", lan->getStatusText());
	ud->setIntegerForKey("net_my_id", lan->isMultiplayer() ? lan->getMyPlayerId() : 0);

	for (int i = 0; i < 6; i++)
	{
		NetSlotData slot = lan->getSlot(i);
		string keyOwner = format("net_slot_owner_{}", i);
		string keyHero = format("net_slot_hero_{}", i);
		ud->setIntegerForKey(keyOwner.c_str(), slot.ownerId);
		ud->setStringForKey(keyHero.c_str(), slot.heroName);
	}

	// 3. Jika Client menerima sinyal START dari Host, otomatis masuk ke arena
	if (lan->isClient() && lan->isGameStartedSignal())
	{
		onGameStart();
	}
}

void SelectLayer::onExit()
{
	unschedule(schedule_selector(SelectLayer::updateNetworkLobby));
	setKeypadEnabled(false);
	Layer::onExit();
}

void SelectLayer::onEnter()
{
	Layer::onEnter();
	setKeypadEnabled(true);
	UserDefault::sharedUserDefault()->setStringForKey("net_cmd", "");
	schedule(schedule_selector(SelectLayer::updateNetworkLobby), 0.15f);
}

void SelectLayer::keyBackClicked()
{
	setKeypadEnabled(false);
	LanManager::getInstance()->disconnect();
	lua_call_func(UiFlowKeys::kBackToStartMenu);
}
