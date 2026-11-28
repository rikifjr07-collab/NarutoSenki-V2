#include <time.h>
#include "StartMenu.h"
#include "Constants/UiFlowKeys.hpp"
#include "Network/LanManager.h"

#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include <jni.h>
#include "../../../cocos2dx/platform/android/jni/JniHelper.h"
#endif

SelectLayer::SelectLayer()
{
    auto ud = UserDefault::sharedUserDefault();
    ud->setStringForKey("net_cmd", "");
    ud->setStringForKey("net_new_chat", "");

    string savedName = ud->getStringForKey("player_username", "");
    if (!savedName.empty())
    {
        LanManager::getInstance()->setMyUsername(savedName);
    }
    else
    {
        ud->setStringForKey("player_username", LanManager::getInstance()->getMyUsername());
    }

    LanManager::getInstance()->probeServerStats();
    schedule(schedule_selector(SelectLayer::updateNetworkLobby), 0.15f);
}

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
		else if (cmd.find("SERVER:") == 0)
		{
			lan->selectServer(atoi(cmd.substr(7).c_str()));
		}
		else if (cmd.find("NAME:") == 0)
		{
			string newName = cmd.substr(5);
			lan->setMyUsername(newName);
			ud->setStringForKey("player_username", lan->getMyUsername());
		}
		else if (cmd.find("CHAT:") == 0)
		{
			lan->sendChatMessage(cmd.substr(5));
		}
		else if (cmd == "DISCONNECT")
		{
			lan->disconnect();
			lan->probeServerStats();
		}
	}

	ud->setStringForKey("net_status", lan->getStatusText());
	ud->setIntegerForKey("net_my_id", lan->isMultiplayer() ? lan->getMyPlayerId() : 0);
	ud->setIntegerForKey("net_server_idx", lan->getSelectedServer());
	ud->setIntegerForKey("net_online_count", lan->getServerOnlineCount());

	for (int i = 0; i < 6; i++)
	{
		NetSlotData slot = lan->getSlot(i);
		ud->setIntegerForKey(format("net_slot_owner_{}", i).c_str(), slot.ownerId);
		ud->setStringForKey(format("net_slot_hero_{}", i).c_str(), slot.heroName);
		ud->setStringForKey(format("net_slot_name_{}", i).c_str(), slot.playerName);
	}

	auto chats = lan->popChatMessages();
	if (!chats.empty())
	{
		string existing = ud->getStringForKey("net_new_chat", "");
		for (const auto &c : chats)
		{
			if (!existing.empty()) existing += "\n";
			existing += c;
		}
		ud->setStringForKey("net_new_chat", existing);
	}

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
}

void SelectLayer::keyBackClicked()
{
	setKeypadEnabled(false);
	LanManager::getInstance()->disconnect();
	lua_call_func(UiFlowKeys::kBackToStartMenu);
}
