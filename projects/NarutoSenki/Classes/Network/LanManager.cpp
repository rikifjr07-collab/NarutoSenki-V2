#include "LanManager.h"
#include "Core/Provider.hpp"
#include <sstream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace ExitGames::LoadBalancing;
using namespace ExitGames::Common;
using namespace ExitGames::Photon;

// GANTI DENGAN APP ID LENGKAP DARI DASHBOARD PHOTON KAMU
static const JString PHOTON_APP_ID = L"MASUKKAN_APP_ID_PHOTON_LENGKAP_DISINI";
static const JString PHOTON_APP_VER = L"1.0";

static LanManager *s_lanInstance = nullptr;

LanManager *LanManager::getInstance()
{
	if (!s_lanInstance)
		s_lanInstance = new LanManager();
	return s_lanInstance;
}

LanManager::LanManager()
{
	srand((unsigned int)time(nullptr));
	for (int i = 0; i < 6; i++)
	{
		_slots[i].ownerId = 0;
		_slots[i].group = (i < 3) ? 0 : 1;
		_slots[i].heroName = "";
	}
}

LanManager::~LanManager()
{
	disconnect();
}

string LanManager::getStatusText()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	return _statusMsg;
}

NetSlotData LanManager::getSlot(int index)
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	if (index >= 0 && index < 6)
		return _slots[index];
	return NetSlotData();
}

int LanManager::getMySlotIndex()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	for (int i = 0; i < 6; i++)
	{
		if (_slots[i].ownerId == _myPlayerId)
			return i;
	}
	return 0;
}

void LanManager::disconnect()
{
	_running = false;
	_isHost = false;
	_isClient = false;
	_gameStarted = false;
	_winnerGroup = -1;
	_pingMs = 0;
	_channelSubscribed = false;
	_roomCode = "";
	_nextPlayerId = 2;

	{
		std::lock_guard<std::mutex> lock(_sendMutex);
		while (!_sendQueue.empty())
			_sendQueue.pop();
	}

	std::lock_guard<std::mutex> lock(_dataMutex);
	for (int i = 0; i < 6; i++)
	{
		_slots[i].ownerId = 0;
		_slots[i].group = (i < 3) ? 0 : 1;
		_slots[i].heroName = "";
	}
	_myPlayerId = 1;
	_statusMsg = "Offline";
	_konohaKills = 0;
	_akatsukiKills = 0;
	_hasNewKillReport = false;
	_hasNewGuardian = false;
}

bool LanManager::startHost()
{
	disconnect();

	int codeNum = 1000 + (rand() % 9000);
	_roomCode = format("{}", codeNum);
	_isHost = true;
	_running = true;
	_myPlayerId = 1;
	_nextPlayerId = 2;

	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_slots[0].ownerId = 1;
		_statusMsg = "Menghubungkan Server...";
	}

	std::thread(&LanManager::networkThreadLoop, this).detach();
	return true;
}

bool LanManager::joinHost(const string &roomCodeOrIp)
{
	disconnect();

	string code = roomCodeOrIp;
	while (!code.empty() && (code.back() == ' ' || code.back() == '\n' || code.back() == '\r'))
		code.pop_back();

	if (code.empty())
	{
		_statusMsg = "Kode Room Kosong";
		return false;
	}

	_roomCode = code;
	_clientTempId = format("C{}", 10000 + (rand() % 90000));
	_isClient = true;
	_running = true;

	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Masuk Room " + _roomCode + "...";
	}

	std::thread(&LanManager::networkThreadLoop, this).detach();
	return true;
}

void LanManager::networkThreadLoop()
{
	Client* client = new Client(*this, PHOTON_APP_ID, PHOTON_APP_VER, ConnectionProtocol::UDP);
	client->setAutoJoinLobby(false);
	_photonClient = client;

	// Paksa terhubung ke server Singapura (asia) agar ping rendah (25-45ms)
	    // Terhubung otomatis ke server region ping terendah (Singapura/Asia)
    if (!client->connect())

	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Gagal Init Photon";
		_isHost = false;
		_isClient = false;
		_photonClient = nullptr;
		delete client;
		return;
	}

	while (_running)
	{
		client->service();

		if (client->getIsInRoom())
		{
			_pingMs = client->getRoundTripTime() / 2;

			while (_running)
			{
				OutgoingPkt pkt;
				{
					std::lock_guard<std::mutex> lock(_sendMutex);
					if (_sendQueue.empty()) break;
					pkt = _sendQueue.front();
					_sendQueue.pop();
				}
				JString payload(pkt.data.c_str());
				client->opRaiseEvent(pkt.reliable, payload, 1);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	client->disconnect();
	_photonClient = nullptr;
	delete client;
}

void LanManager::sendGamePacket(const string &rawPacket, bool reliable)
{
	if (!_channelSubscribed) return;
	string tagged = (_isHost ? "H|" : "C|") + rawPacket;
	std::lock_guard<std::mutex> lock(_sendMutex);
	if (_sendQueue.size() > 60)
		_sendQueue.pop();
	_sendQueue.push({tagged, reliable});
}
// --- Photon Callbacks ---

void LanManager::connectReturn(int errorCode, const JString& errorString, const JString& region, const JString& cluster)
{
	if (errorCode != 0)
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Err Koneksi: " + std::to_string(errorCode);
		_running = false;
		_isHost = false;
		_isClient = false;
		return;
	}

	if (!_photonClient) return;
	JString roomJStr(_roomCode.c_str());

	if (_isHost)
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Membuat Room " + _roomCode + "...";
		RoomOptions opts;
		opts.setMaxPlayers(6);
		_photonClient->opCreateRoom(roomJStr, opts);
	}
	else if (_isClient)
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Mencari Room " + _roomCode + "...";
		_photonClient->opJoinRoom(roomJStr);
	}
}

void LanManager::createRoomReturn(int localPlayerNr, const Hashtable& gameProperties, const Hashtable& playerProperties, int errorCode, const JString& errorString)
{
	if (errorCode == 0)
	{
		_channelSubscribed = true;
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Room: " + _roomCode + " (1/6)";
	}
	else
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Gagal Buat Room (" + std::to_string(errorCode) + ")";
		_running = false;
		_isHost = false;
	}
}

void LanManager::joinRoomReturn(int localPlayerNr, const Hashtable& gameProperties, const Hashtable& playerProperties, int errorCode, const JString& errorString)
{
	if (errorCode == 0)
	{
		_channelSubscribed = true;
		{
			std::lock_guard<std::mutex> lock(_dataMutex);
			_statusMsg = "Minta Slot Room " + _roomCode + "...";
		}
		sendGamePacket("HELLO:" + _clientTempId, true);
	}
	else
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Room Tidak Ditemukan";
		_running = false;
		_isClient = false;
	}
}

void LanManager::connectionErrorReturn(int errorCode)
{
	if (_running)
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Koneksi Terputus (" + std::to_string(errorCode) + ")";
		_running = false;
		_isHost = false;
		_isClient = false;
	}
}

void LanManager::disconnectReturn()
{
	_channelSubscribed = false;
}

void LanManager::customEventAction(int playerNr, nByte eventCode, const Object& eventContent)
{
	if (eventCode == 1)
	{
		JString data = ValueObject<JString>(eventContent).getDataCopy();
		string taggedPkt = data.UTF8Representation().cstr();
		processGamePacket(taggedPkt);
	}
}

// --- Logika Paket Game ---

void LanManager::processGamePacket(const string &taggedPkt)
{
	if (taggedPkt.size() < 3) return;
	char senderRole = taggedPkt[0];
	string pkt = taggedPkt.substr(2);

	if (_isHost && senderRole == 'H') return;
	if (_isClient && senderRole == 'C') return;

	if (_isHost)
	{
		if (pkt.find("HELLO:") == 0)
		{
			string tempId = pkt.substr(6);
			int assignedId = -1;
			int activeCount = 1;
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				if (_nextPlayerId <= 6)
				{
					for (int i = 0; i < 6; i++)
					{
						if (_slots[i].ownerId == 0)
						{
							assignedId = _nextPlayerId++;
							_slots[i].ownerId = assignedId;
							break;
						}
					}
				}
				for (int i = 0; i < 6; i++)
					if (_slots[i].ownerId > 1) activeCount++;
				_statusMsg = format("Room: {} ({}/6)", _roomCode, activeCount);
			}

			if (assignedId != -1)
			{
				sendGamePacket(format("ASSIGN:{},{}", tempId, assignedId), true);
				broadcastLobbyState();
			}
		}
		else if (pkt.find("MOVE:") == 0)
		{
			int playerId = 0, targetSlot = -1;
			if (sscanf(pkt.c_str() + 5, "%d,%d", &playerId, &targetSlot) == 2)
			{
				{
					std::lock_guard<std::mutex> lock(_dataMutex);
					if (targetSlot >= 0 && targetSlot < 6 && _slots[targetSlot].ownerId == 0)
					{
						for (int i = 0; i < 6; i++)
						{
							if (_slots[i].ownerId == playerId)
							{
								_slots[targetSlot].ownerId = playerId;
								_slots[targetSlot].heroName = _slots[i].heroName;
								_slots[i].ownerId = 0;
								_slots[i].heroName = "";
								break;
							}
						}
					}
				}
				broadcastLobbyState();
			}
		}
		else if (pkt.find("HERO:") == 0)
		{
			size_t comma = pkt.find(',', 5);
			if (comma != string::npos)
			{
				int playerId = atoi(pkt.substr(5, comma - 5).c_str());
				string hName = pkt.substr(comma + 1);
				{
					std::lock_guard<std::mutex> lock(_dataMutex);
					for (int i = 0; i < 6; i++)
					{
						if (_slots[i].ownerId == playerId)
						{
							_slots[i].heroName = hName;
							break;
						}
					}
				}
				broadcastLobbyState();
			}
		}
		else if (pkt.find("SURRENDER:") == 0)
		{
			int loserGroup = atoi(pkt.substr(10).c_str());
			int winnerGroup = (loserGroup == 0) ? 1 : 0;
			broadcastGameOver(winnerGroup);
		}
		else if (pkt.find("IN:") == 0)
		{
			NetPlayerInput inp{};
			int flip = 0;
			int parsed = sscanf(pkt.c_str() + 3, "%d,%f,%f,%d,%f,%f,%d",
								&inp.playerId, &inp.dirX, &inp.dirY, &inp.actionType, &inp.posX, &inp.posY, &flip);
			if (parsed >= 4)
			{
				inp.isFlipped = (flip == 1);
				std::lock_guard<std::mutex> lock(_dataMutex);
				_pendingInputs.push_back(inp);
			}
		}
	}
	else if (_isClient)
	{
		if (pkt.find("ASSIGN:") == 0)
		{
			size_t comma = pkt.find(',', 7);
			if (comma != string::npos)
			{
				string targetTempId = pkt.substr(7, comma - 7);
				int assignedId = atoi(pkt.substr(comma + 1).c_str());
				if (targetTempId == _clientTempId)
				{
					_myPlayerId = assignedId;
					std::lock_guard<std::mutex> lock(_dataMutex);
					_statusMsg = format("Room {} (Slot {}P)", _roomCode, _myPlayerId);
				}
			}
		}
		else if (pkt.find("LOBBY:") == 0) { parseLobbyPacket(pkt.substr(6)); }
		else if (pkt.find("START") == 0) { _gameStarted = true; }
		else if (pkt.find("OVER:") == 0) { _winnerGroup = atoi(pkt.substr(5).c_str()); }
		else if (pkt.find("KILL:") == 0)
		{
			char kBuf[64] = {0}, dBuf[64] = {0};
			int streak = 1, kKills = 0, aKills = 0;
			if (sscanf(pkt.c_str() + 5, "%63[^,],%63[^,],%d,%d,%d", kBuf, dBuf, &streak, &kKills, &aKills) == 5)
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				_lastKillerName = kBuf; _lastDeadName = dBuf; _lastKillStreak = streak;
				_konohaKills = kKills; _akatsukiKills = aKills; _hasNewKillReport = true;
			}
		}
		else if (pkt.find("GARD:") == 0)
		{
			char gBuf[64] = {0};
			int grp = 0;
			if (sscanf(pkt.c_str() + 5, "%63[^,],%d", gBuf, &grp) == 2)
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				_spawnedGuardName = gBuf; _spawnedGuardGroup = grp; _hasNewGuardian = true;
			}
		}
		else if (pkt.find("WS:") == 0)
		{
			vector<NetHeroSync> list;
			std::stringstream ss(pkt.substr(3));
			string seg;
			while (std::getline(ss, seg, ';'))
			{
				NetHeroSync h{};
				int flip = 0, stick = 0;
				if (sscanf(seg.c_str(), "%d,%f,%f,%d,%d,%u,%u,%d,%d,%d",
						   &h.charId, &h.x, &h.y, &flip, &h.state, &h.hp, &h.maxHp, &h.lastAttack, &h.attackSeq, &stick) == 10)
				{
					h.isFlipped = (flip == 1); h.isSticking = (stick == 1);
					list.push_back(h);
				}
			}
			std::lock_guard<std::mutex> lock(_dataMutex);
			_latestWorldState = list;
		}
		else if (pkt.find("FL:") == 0)
		{
			vector<NetFlogSync> list;
			std::stringstream ss(pkt.substr(3));
			string seg;
			while (std::getline(ss, seg, ';'))
			{
				NetFlogSync f{};
				int flip = 0;
				if (sscanf(seg.c_str(), "%d,%d,%f,%f,%d,%d,%u", &f.group, &f.index, &f.x, &f.y, &flip, &f.state, &f.hp) == 7)
				{
					f.isFlipped = (flip == 1);
					list.push_back(f);
				}
			}
			std::lock_guard<std::mutex> lock(_dataMutex);
			_latestFlogState = list;
		}
		else if (pkt.find("TW:") == 0)
		{
			vector<NetTowerSync> list;
			std::stringstream ss(pkt.substr(3));
			string seg;
			while (std::getline(ss, seg, ';'))
			{
				NetTowerSync t{};
				if (sscanf(seg.c_str(), "%d,%u,%u", &t.charId, &t.hp, &t.maxHp) == 3) list.push_back(t);
			}
			std::lock_guard<std::mutex> lock(_dataMutex);
			_latestTowerState = list;
		}
		else if (pkt.find("MN:") == 0)
		{
			vector<NetMonsterSync> list;
			string body = pkt.substr(3);
			if (!body.empty())
			{
				std::stringstream ss(body);
				string seg;
				while (std::getline(ss, seg, ';'))
				{
					NetMonsterSync m{};
					char nameBuf[64] = {0};
					int flip = 0;
					if (sscanf(seg.c_str(), "%d,%d,%63[^,],%f,%f,%d,%d,%u,%u",
							   &m.masterCharId, &m.monIndex, nameBuf, &m.x, &m.y, &flip, &m.state, &m.hp, &m.maxHp) == 9)
					{
						m.name = nameBuf; m.isFlipped = (flip == 1);
						list.push_back(m);
					}
				}
			}
			std::lock_guard<std::mutex> lock(_dataMutex);
			_latestMonsterState = list;
		}
	}
}
void LanManager::broadcastLobbyState()
{
	if (!_isHost) return;
	string pkt = "LOBBY:";
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		for (int i = 0; i < 6; i++)
		{
			pkt += format("{},{}", _slots[i].ownerId, _slots[i].heroName);
			if (i < 5) pkt += "|";
		}
	}
	sendGamePacket(pkt, true);
}

void LanManager::parseLobbyPacket(const string &packet)
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	std::stringstream ss(packet);
	string item;
	int idx = 0;
	while (std::getline(ss, item, '|') && idx < 6)
	{
		size_t comma = item.find(',');
		if (comma != string::npos)
		{
			_slots[idx].ownerId = atoi(item.substr(0, comma).c_str());
			_slots[idx].heroName = item.substr(comma + 1);
		}
		idx++;
	}
}

void LanManager::requestMoveSlot(int targetSlot)
{
	if (targetSlot < 0 || targetSlot >= 6) return;
	if (_isHost)
	{
		{
			std::lock_guard<std::mutex> lock(_dataMutex);
			if (_slots[targetSlot].ownerId == 0)
			{
				for (int i = 0; i < 6; i++)
				{
					if (_slots[i].ownerId == _myPlayerId)
					{
						_slots[targetSlot].ownerId = _myPlayerId;
						_slots[targetSlot].heroName = _slots[i].heroName;
						_slots[i].ownerId = 0;
						_slots[i].heroName = "";
						break;
					}
				}
			}
		}
		broadcastLobbyState();
	}
	else if (_isClient)
	{
		sendGamePacket(format("MOVE:{},{}", _myPlayerId, targetSlot), true);
	}
}

void LanManager::requestSelectHero(const string &heroName)
{
	if (_isHost)
	{
		{
			std::lock_guard<std::mutex> lock(_dataMutex);
			for (int i = 0; i < 6; i++)
			{
				if (_slots[i].ownerId == _myPlayerId)
				{
					_slots[i].heroName = heroName;
					break;
				}
			}
		}
		broadcastLobbyState();
	}
	else if (_isClient)
	{
		sendGamePacket(format("HERO:{},{}", _myPlayerId, heroName), true);
	}
}

void LanManager::hostStartGame()
{
	if (!_isHost) return;
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		vector<string> usedHeroes;
		for (int i = 0; i < 6; i++)
		{
			if (!_slots[i].heroName.empty())
				usedHeroes.push_back(_slots[i].heroName);
		}
		for (int i = 0; i < 6; i++)
		{
			if (_slots[i].heroName.empty())
			{
				string rndHero = kHeroList[rand() % kHeroNum];
				while (std::find(usedHeroes.begin(), usedHeroes.end(), rndHero) != usedHeroes.end())
				{
					rndHero = kHeroList[rand() % kHeroNum];
				}
				_slots[i].heroName = rndHero;
				usedHeroes.push_back(rndHero);
			}
		}
	}

	broadcastLobbyState();
	sendGamePacket("START", true);
	_gameStarted = true;
}

void LanManager::sendClientInput(float dirX, float dirY, int actionType, float posX, float posY, bool isFlipped)
{
	if (!_isClient || !_channelSubscribed) return;
	string pkt = format("IN:{},{:.2f},{:.2f},{},{:.1f},{:.1f},{}",
						_myPlayerId, dirX, dirY, actionType, posX, posY, isFlipped ? 1 : 0);
	bool isSkillOrAttack = (actionType != -1);
	sendGamePacket(pkt, isSkillOrAttack);
}

void LanManager::broadcastWorldState(const vector<NetHeroSync> &heroes)
{
	if (!_isHost || !_channelSubscribed) return;
	string pkt = "WS:";
	for (size_t i = 0; i < heroes.size(); i++)
	{
		const auto &h = heroes[i];
		pkt += format("{},{:.1f},{:.1f},{},{},{},{},{},{},{}",
			  h.charId, h.x, h.y, h.isFlipped ? 1 : 0, h.state, h.hp, h.maxHp, h.lastAttack, h.attackSeq, h.isSticking ? 1 : 0);
		if (i + 1 < heroes.size()) pkt += ";";
	}
	sendGamePacket(pkt, false);
}

vector<NetPlayerInput> LanManager::popPendingInputs()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	auto copy = _pendingInputs;
	_pendingInputs.clear();
	return copy;
}

vector<NetHeroSync> LanManager::getLatestWorldState()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	return _latestWorldState;
}

void LanManager::notifySurrender(int myGroup)
{
	int enemyGroup = (myGroup == 0) ? 1 : 0;
	if (_isHost) broadcastGameOver(enemyGroup);
	else if (_isClient) sendGamePacket(format("SURRENDER:{}", myGroup), true);
}

void LanManager::broadcastGameOver(int winnerGroup)
{
	_winnerGroup = winnerGroup;
	if (!_isHost) return;
	sendGamePacket(format("OVER:{}", winnerGroup), true);
}

void LanManager::sendPing()
{
	// Ping dihitung otomatis oleh Photon UDP di networkThreadLoop
}

void LanManager::broadcastFlogState(const vector<NetFlogSync> &flogs)
{
	if (!_isHost || !_channelSubscribed || flogs.empty()) return;
	string pkt = "FL:";
	for (size_t i = 0; i < flogs.size(); i++)
	{
		const auto &f = flogs[i];
		pkt += format("{},{},{:.0f},{:.0f},{},{},{}", f.group, f.index, f.x, f.y, f.isFlipped ? 1 : 0, f.state, f.hp);
		if (i + 1 < flogs.size()) pkt += ";";
	}
	sendGamePacket(pkt, false);
}

vector<NetFlogSync> LanManager::getLatestFlogState()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	return _latestFlogState;
}

void LanManager::broadcastTowerState(const vector<NetTowerSync> &towers)
{
	if (!_isHost || !_channelSubscribed || towers.empty()) return;
	string pkt = "TW:";
	for (size_t i = 0; i < towers.size(); i++)
	{
		const auto &t = towers[i];
		pkt += format("{},{},{}", t.charId, t.hp, t.maxHp);
		if (i + 1 < towers.size()) pkt += ";";
	}
	sendGamePacket(pkt, false);
}

vector<NetTowerSync> LanManager::getLatestTowerState()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	return _latestTowerState;
}

void LanManager::broadcastMonsterState(const vector<NetMonsterSync> &mons)
{
	if (!_isHost || !_channelSubscribed) return;
	string pkt = "MN:";
	for (size_t i = 0; i < mons.size(); i++)
	{
		const auto &m = mons[i];
		pkt += format("{},{},{},{:.0f},{:.0f},{},{},{},{}",
					  m.masterCharId, m.monIndex, m.name, m.x, m.y, m.isFlipped ? 1 : 0, m.state, m.hp, m.maxHp);
		if (i + 1 < mons.size()) pkt += ";";
	}
	sendGamePacket(pkt, false);
}

vector<NetMonsterSync> LanManager::getLatestMonsterState()
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	return _latestMonsterState;
}

void LanManager::broadcastKillEvent(const string &killerName, const string &deadName, int killNum, int konohaKills, int akatsukiKills)
{
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_konohaKills = konohaKills;
		_akatsukiKills = akatsukiKills;
	}
	if (!_isHost) return;
	sendGamePacket(format("KILL:{},{},{},{},{}", killerName, deadName, killNum, konohaKills, akatsukiKills), true);
}

bool LanManager::popKillEvent(string &killerName, string &deadName, int &killNum)
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	if (!_hasNewKillReport) return false;
	killerName = _lastKillerName;
	deadName = _lastDeadName;
	killNum = _lastKillStreak;
	_hasNewKillReport = false;
	return true;
}

void LanManager::getTeamKills(int &konohaKills, int &akatsukiKills)
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	konohaKills = _konohaKills;
	akatsukiKills = _akatsukiKills;
}

void LanManager::broadcastGuardianSpawn(const string &guardName, int group)
{
	if (!_isHost) return;
	sendGamePacket(format("GARD:{},{}", guardName, group), true);
}

bool LanManager::popGuardianSpawn(string &guardName, int &group)
{
	std::lock_guard<std::mutex> lock(_dataMutex);
	if (!_hasNewGuardian) return false;
	guardName = _spawnedGuardName;
	group = _spawnedGuardGroup;
	_hasNewGuardian = false;
	return true;
}
