#pragma once
#include "Defines.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

struct NetSlotData
{
	int ownerId = 0;      // 0 = Bot (COM), 1 = Host (1P), 2..6 = Client (2P..6P)
	int group = 0;        // 0 = Konoha (Tim Kiri), 1 = Akatsuki (Tim Kanan)
	string heroName = ""; // Kosong jika belum dipilih
};

struct NetPlayerInput
{
    int playerId = 0;
    float dirX = 0.0f;
    float dirY = 0.0f;
    int actionType = -1;
    float posX = 0.0f;
    float posY = 0.0f;
    bool isFlipped = false;
};

struct NetHeroSync
{
	int charId = 0;
	float x = 0.0f;
	float y = 0.0f;
	bool isFlipped = false;
	int state = 0;
	uint32_t hp = 0;
	uint32_t maxHp = 0;
	int lastAttack = -1;
	int attackSeq = 0;
    bool isSticking = false;
};

struct NetFlogSync
{
	int group = 0;
	int index = 0;
	float x = 0.0f;
	float y = 0.0f;
	bool isFlipped = false;
	int state = 0;
	uint32_t hp = 0;
};

struct NetTowerSync
{
	int charId = 0;
	uint32_t hp = 0;
	uint32_t maxHp = 0;
};

struct NetMonsterSync
{
    int masterCharId = 0;
    int monIndex = 0;
    string name;
    float x = 0.0f;
    float y = 0.0f;
    bool isFlipped = false;
    int state = 0;
    uint32_t hp = 0;
    uint32_t maxHp = 0;
};

class LanManager
{
public:
	static LanManager *getInstance();

	bool isMultiplayer() const { return _isHost || _isClient; }
	bool isHost() const { return _isHost; }
	bool isClient() const { return _isClient; }
	int getMyPlayerId() const { return _myPlayerId; }
	string getStatusText();
	string getRoomCode() const { return _roomCode; }

	bool startHost();
	bool joinHost(const string &roomCodeOrIp);
	void disconnect();

	void requestMoveSlot(int targetSlot);
	void requestSelectHero(const string &heroName);
	void hostStartGame();
	bool isGameStartedSignal() const { return _gameStarted; }
	void clearGameStartedSignal() { _gameStarted = false; }

	NetSlotData getSlot(int index);
	int getMySlotIndex();

	void sendClientInput(float dirX, float dirY, int actionType, float posX = 0.0f, float posY = 0.0f, bool isFlipped = false);
	void broadcastWorldState(const vector<NetHeroSync> &heroes);
	vector<NetPlayerInput> popPendingInputs();
	vector<NetHeroSync> getLatestWorldState();
    void broadcastFlogState(const vector<NetFlogSync> &flogs);
    vector<NetFlogSync> getLatestFlogState();
    void broadcastTowerState(const vector<NetTowerSync> &towers);
    vector<NetTowerSync> getLatestTowerState();
    void broadcastMonsterState(const vector<NetMonsterSync> &mons);
    vector<NetMonsterSync> getLatestMonsterState();
    void notifySurrender(int myGroup);
    void broadcastGameOver(int winnerGroup);
    int getNetworkWinnerGroup() const { return _winnerGroup; }
    int getPingMs() const { return _pingMs; }
    void sendPing();
    void broadcastKillEvent(const string &killerName, const string &deadName, int killNum, int konohaKills, int akatsukiKills);
    bool popKillEvent(string &killerName, string &deadName, int &killNum);
    void getTeamKills(int &konohaKills, int &akatsukiKills);
    void broadcastGuardianSpawn(const string &guardName, int group);
    bool popGuardianSpawn(string &guardName, int &group);

private:
	LanManager();
	~LanManager();

	void networkThreadLoop();
	bool sendWsFrame(const string &text);
	void handleWsMessage(const string &rawMsg);
	void sendApinatorEvent(const string &eventName, const string &payloadData);
	void sendGamePacket(const string &rawPacket);
	void processGamePacket(const string &pkt);
	void broadcastLobbyState();
	void parseLobbyPacket(const string &packet);

		int _sock = -1;
	void* _sslCtx = nullptr;
	void* _ssl = nullptr;
	std::mutex _sendMutex;
	bool _wsConnected = false;
	bool _channelSubscribed = false;
	string _socketId = "";
	string _roomCode = "";
	string _clientTempId = "";
	int _nextPlayerId = 2;

	std::atomic<bool> _isHost{false};
	std::atomic<bool> _isClient{false};
	std::atomic<bool> _running{false};
	std::atomic<bool> _gameStarted{false};
    std::atomic<int> _winnerGroup{-1};
    std::atomic<int> _pingMs{0};
    int _konohaKills = 0;
    int _akatsukiKills = 0;
    bool _hasNewKillReport = false;
    string _lastKillerName = "";
    string _lastDeadName = "";
    int _lastKillStreak = 1;
    bool _hasNewGuardian = false;
    string _spawnedGuardName = "";
    int _spawnedGuardGroup = 0;

	int _myPlayerId = 1;
	string _statusMsg = "Offline";

	std::mutex _dataMutex;
	NetSlotData _slots[6];
	vector<NetPlayerInput> _pendingInputs;
	vector<NetHeroSync> _latestWorldState;
    vector<NetFlogSync> _latestFlogState;
    vector<NetTowerSync> _latestTowerState;
    vector<NetMonsterSync> _latestMonsterState;
};
