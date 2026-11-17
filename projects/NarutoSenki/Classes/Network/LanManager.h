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
	int actionType = -1;  // -1 = None, 0 = NAttack, 1..5 = Skill, 6 = Item1
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
};

class LanManager
{
public:
	static LanManager *getInstance();

	// Status Koneksi
	bool isMultiplayer() const { return _isHost || _isClient; }
	bool isHost() const { return _isHost; }
	bool isClient() const { return _isClient; }
	int getMyPlayerId() const { return _myPlayerId; }
	string getStatusText();

	// Kontrol Lobby
	bool startHost();
	bool joinHost(const string &ipAddress);
	void disconnect();

	// Manajemen 6 Slot Fleksibel (0..2 = Konoha/Kiri, 3..5 = Akatsuki/Kanan)
	void requestMoveSlot(int targetSlot);
	void requestSelectHero(const string &heroName);
	void hostStartGame();
	bool isGameStartedSignal() const { return _gameStarted; }
	void clearGameStartedSignal() { _gameStarted = false; }

	NetSlotData getSlot(int index);
	int getMySlotIndex();

	// Sinkronisasi Pertarungan (In-Game)
	void sendClientInput(float dirX, float dirY, int actionType);
	void broadcastWorldState(const vector<NetHeroSync> &heroes);
	vector<NetPlayerInput> popPendingInputs();
	vector<NetHeroSync> getLatestWorldState();

private:
	LanManager();
	~LanManager();

	void hostAcceptLoop();
	void clientReceiveLoop();
	void udpReceiveLoop();
	void broadcastLobbyState();
	void parseLobbyPacket(const string &packet);

	std::atomic<bool> _isHost{false};
	std::atomic<bool> _isClient{false};
	std::atomic<bool> _running{false};
	std::atomic<bool> _gameStarted{false};

	int _myPlayerId = 1;
	int _tcpSocket = -1;
	int _udpSocket = -1;
	string _hostIp = "";
	string _statusMsg = "Offline";

	std::mutex _dataMutex;
	NetSlotData _slots[6];
	vector<int> _clientSockets;
	vector<string> _clientIps;
	vector<NetPlayerInput> _pendingInputs;
	vector<NetHeroSync> _latestWorldState;
};
