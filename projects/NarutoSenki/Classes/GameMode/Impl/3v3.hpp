#pragma once
#include "GameMode/IGameModeHandler.hpp"
#include "Network/LanManager.h"

class Mode3v3 : public IGameModeHandler
{
public:
	void init()
	{
		CCLOG("Enter 3 VS 3 mode.");
		gd.isHardCore = true;
	}

	void onInitHeros()
	{
		auto lan = LanManager::getInstance();
		if (lan->isMultiplayer())
		{
			clearHeroArray();
			int myId = lan->getMyPlayerId();

			for (int i = 0; i < 6; i++)
			{
				NetSlotData slot = lan->getSlot(i);
				Group grp = (i < 3) ? Group::Konoha : Group::Akatsuki;
				Role role = (slot.ownerId == myId) ? Role::Player : Role::Com;

				if (slot.ownerId == myId)
				{
					this->gd.playerGroup = grp;
					this->playerGroup = grp;
					selectLayer->_playerSelect = slot.heroName.c_str();
				}

				addHero(slot.heroName.c_str(), role, grp);
			}
		}
		else
		{
			initHeros(3, 3);
		}
	}

	void onGameStart() {}
	void onGameOver() {}
	void onCharacterInit(CharacterBase *c) {}
	void onCharacterDead(CharacterBase *c) {}
	void onCharacterReborn(CharacterBase *c) {}
};
