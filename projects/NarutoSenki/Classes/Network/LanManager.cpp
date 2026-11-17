#include "LanManager.h"
#include "Core/Provider.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>

static LanManager *s_lanInstance = nullptr;
constexpr int TCP_PORT = 8889;
constexpr int UDP_PORT = 8890;

LanManager *LanManager::getInstance()
{
	if (!s_lanInstance)
		s_lanInstance = new LanManager();
	return s_lanInstance;
}

LanManager::LanManager()
{
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

	if (_tcpSocket >= 0) { close(_tcpSocket); _tcpSocket = -1; }
	if (_udpSocket >= 0) { close(_udpSocket); _udpSocket = -1; }

	std::lock_guard<std::mutex> lock(_dataMutex);
	for (int fd : _clientSockets)
		if (fd >= 0) close(fd);
	_clientSockets.clear();
	_clientIps.clear();

	for (int i = 0; i < 6; i++)
	{
		_slots[i].ownerId = 0;
		_slots[i].group = (i < 3) ? 0 : 1;
		_slots[i].heroName = "";
	}
	_myPlayerId = 1;
	_statusMsg = "Offline";
}

bool LanManager::startHost()
{
	disconnect();

	_tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (_tcpSocket < 0) return false;

	int opt = 1;
	setsockopt(_tcpSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(TCP_PORT);

	if (::bind(_tcpSocket, (sockaddr *)&addr, sizeof(addr)) < 0)

	{
		close(_tcpSocket);
		_tcpSocket = -1;
		_statusMsg = "Port Sibuk";
		return false;
	}

	listen(_tcpSocket, 5);

	_udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in udpAddr{};
	udpAddr.sin_family = AF_INET;
	udpAddr.sin_addr.s_addr = INADDR_ANY;
	udpAddr.sin_port = htons(UDP_PORT);
	::bind(_udpSocket, (sockaddr *)&udpAddr, sizeof(udpAddr));


	_isHost = true;
	_running = true;
	_myPlayerId = 1;

	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_slots[0].ownerId = 1;
		_statusMsg = "Host Aktif (1/6)";
	}

	std::thread(&LanManager::hostAcceptLoop, this).detach();
	std::thread(&LanManager::udpReceiveLoop, this).detach();
	return true;
}

bool LanManager::joinHost(const string &ipAddress)
{
	disconnect();

	_tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (_tcpSocket < 0) return false;

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(TCP_PORT);
	if (inet_pton(AF_INET, ipAddress.c_str(), &serverAddr.sin_addr) <= 0)
	{
		close(_tcpSocket);
		_tcpSocket = -1;
		_statusMsg = "IP Tidak Valid";
		return false;
	}

	if (connect(_tcpSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		close(_tcpSocket);
		_tcpSocket = -1;
		_statusMsg = "Gagal Terhubung";
		return false;
	}

	_udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in udpAddr{};
	udpAddr.sin_family = AF_INET;
	udpAddr.sin_addr.s_addr = INADDR_ANY;
	udpAddr.sin_port = htons(UDP_PORT + 1);
	::bind(_udpSocket, (sockaddr *)&udpAddr, sizeof(udpAddr));

	_hostIp = ipAddress;
	_isClient = true;
	_running = true;
	_statusMsg = "Terhubung ke " + ipAddress;

	std::thread(&LanManager::clientReceiveLoop, this).detach();
	std::thread(&LanManager::udpReceiveLoop, this).detach();
	return true;
}
void LanManager::hostAcceptLoop()
{
	int nextPlayerId = 2;
	while (_running && _isHost)
	{
		sockaddr_in clientAddr{};
		socklen_t len = sizeof(clientAddr);
		int clientFd = accept(_tcpSocket, (sockaddr *)&clientAddr, &len);
		if (clientFd < 0) continue;

		char ipStr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);

		int assignedId = -1;
		{
			std::lock_guard<std::mutex> lock(_dataMutex);
			if (nextPlayerId <= 6)
			{
				for (int i = 0; i < 6; i++)
				{
					if (_slots[i].ownerId == 0)
					{
						assignedId = nextPlayerId++;
						_slots[i].ownerId = assignedId;
						_clientSockets.push_back(clientFd);
						_clientIps.push_back(string(ipStr));
						_statusMsg = format("Host ({}/6)", (int)_clientSockets.size() + 1);
						break;
					}
				}
			}
		}

		if (assignedId == -1)
		{
			close(clientFd);
			continue;
		}

		string idMsg = format("ASSIGN_ID:{}\n", assignedId);
		send(clientFd, idMsg.c_str(), idMsg.size(), 0);
		broadcastLobbyState();

		std::thread([this, clientFd, assignedId]() {
			char buf[1024];
			while (_running && _isHost)
			{
				int n = recv(clientFd, buf, sizeof(buf) - 1, 0);
				if (n <= 0) break;
				buf[n] = '\0';
				string msg(buf);

				if (msg.find("MOVE:") == 0)
				{
					int targetSlot = atoi(msg.substr(5).c_str());
					std::lock_guard<std::mutex> lock(_dataMutex);
					if (targetSlot >= 0 && targetSlot < 6 && _slots[targetSlot].ownerId == 0)
					{
						for (int i = 0; i < 6; i++)
						{
							if (_slots[i].ownerId == assignedId)
							{
								_slots[targetSlot].ownerId = assignedId;
								_slots[targetSlot].heroName = _slots[i].heroName;
								_slots[i].ownerId = 0;
								_slots[i].heroName = "";
								break;
							}
						}
					}
					broadcastLobbyState();
				}
				else if (msg.find("HERO:") == 0)
				{
					string hName = msg.substr(5);
					while (!hName.empty() && (hName.back() == '\n' || hName.back() == '\r'))
						hName.pop_back();
					std::lock_guard<std::mutex> lock(_dataMutex);
					for (int i = 0; i < 6; i++)
					{
						if (_slots[i].ownerId == assignedId)
						{
							_slots[i].heroName = hName;
							break;
						}
					}
					broadcastLobbyState();
				}
				else if (msg.find("SURRENDER:") == 0)
{
	int loserGroup = atoi(msg.substr(10).c_str());
	int winnerGroup = (loserGroup == 0) ? 1 : 0;
	broadcastGameOver(winnerGroup);
}

			}
		}).detach();
	}
}

void LanManager::clientReceiveLoop()
{
	char buf[1024];
	while (_running && _isClient)
	{
		int n = recv(_tcpSocket, buf, sizeof(buf) - 1, 0);
		if (n <= 0)
		{
			_statusMsg = "Koneksi Terputus";
			_isClient = false;
			break;
		}
		buf[n] = '\0';
		std::stringstream ss(buf);
		string line;
		while (std::getline(ss, line))
		{
			if (line.find("ASSIGN_ID:") == 0)
			{
				_myPlayerId = atoi(line.substr(10).c_str());
				_statusMsg = format("Terhubung (Slot {}P)", _myPlayerId);
			}
			else if (line.find("LOBBY:") == 0)
			{
				parseLobbyPacket(line.substr(6));
			}
			else if (line.find("START") == 0)
			{
				_gameStarted = true;
			}
			else if (line.find("OVER:") == 0)
{
	_winnerGroup = atoi(line.substr(5).c_str());
}

		}
	}
}

void LanManager::broadcastLobbyState()
{
	if (!_isHost) return;
	string pkt = "LOBBY:";
	for (int i = 0; i < 6; i++)
	{
		pkt += format("{},{}", _slots[i].ownerId, _slots[i].heroName);
		if (i < 5) pkt += "|";
	}
	pkt += "\n";

	for (int fd : _clientSockets)
	{
		if (fd >= 0)
			send(fd, pkt.c_str(), pkt.size(), 0);
	}
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
		broadcastLobbyState();
	}
	else if (_isClient && _tcpSocket >= 0)
	{
		string msg = format("MOVE:{}\n", targetSlot);
		send(_tcpSocket, msg.c_str(), msg.size(), 0);
	}
}

void LanManager::requestSelectHero(const string &heroName)
{
	if (_isHost)
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
		broadcastLobbyState();
	}
	else if (_isClient && _tcpSocket >= 0)
	{
		string msg = format("HERO:{}\n", heroName);
		send(_tcpSocket, msg.c_str(), msg.size(), 0);
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
	string startMsg = "START\n";
	for (int fd : _clientSockets)
	{
		if (fd >= 0)
			send(fd, startMsg.c_str(), startMsg.size(), 0);
	}
	_gameStarted = true;
}

void LanManager::sendClientInput(float dirX, float dirY, int actionType)
{
	if (!_isClient || _udpSocket < 0) return;
	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(UDP_PORT);
	inet_pton(AF_INET, _hostIp.c_str(), &dest.sin_addr);

	string pkt = format("IN:{},{:.2f},{:.2f},{}", _myPlayerId, dirX, dirY, actionType);
	sendto(_udpSocket, pkt.c_str(), pkt.size(), 0, (sockaddr *)&dest, sizeof(dest));
}

void LanManager::broadcastWorldState(const vector<NetHeroSync> &heroes)
{
	if (!_isHost || _udpSocket < 0) return;
	string pkt = "WS:";
	for (size_t i = 0; i < heroes.size(); i++)
	{
		const auto &h = heroes[i];
		pkt += format("{},{:.1f},{:.1f},{},{},{},{}", h.charId, h.x, h.y, h.isFlipped ? 1 : 0, h.state, h.hp, h.maxHp);
		if (i + 1 < heroes.size()) pkt += ";";
	}

	std::lock_guard<std::mutex> lock(_dataMutex);
	for (const auto &ip : _clientIps)
	{
		sockaddr_in dest{};
		dest.sin_family = AF_INET;
		dest.sin_port = htons(UDP_PORT + 1);
		inet_pton(AF_INET, ip.c_str(), &dest.sin_addr);
		sendto(_udpSocket, pkt.c_str(), pkt.size(), 0, (sockaddr *)&dest, sizeof(dest));
	}
}

void LanManager::udpReceiveLoop()
{
	char buf[2048];
	while (_running && _udpSocket >= 0)
	{
		sockaddr_in sender{};
		socklen_t slen = sizeof(sender);
		int n = recvfrom(_udpSocket, buf, sizeof(buf) - 1, 0, (sockaddr *)&sender, &slen);
		if (n <= 0) continue;
		buf[n] = '\0';
		string pkt(buf);

		if (_isHost && pkt.find("IN:") == 0)
		{
			NetPlayerInput inp{};
			sscanf(pkt.c_str() + 3, "%d,%f,%f,%d", &inp.playerId, &inp.dirX, &inp.dirY, &inp.actionType);
			std::lock_guard<std::mutex> lock(_dataMutex);
			_pendingInputs.push_back(inp);
		}
		else if (_isClient && pkt.find("WS:") == 0)
		{
			vector<NetHeroSync> list;
			std::stringstream ss(pkt.substr(3));
			string seg;
			while (std::getline(ss, seg, ';'))
			{
				NetHeroSync h{};
				int flip = 0;
				if (sscanf(seg.c_str(), "%d,%f,%f,%d,%d,%u,%u", &h.charId, &h.x, &h.y, &flip, &h.state, &h.hp, &h.maxHp) == 7)
				{
					h.isFlipped = (flip == 1);
					list.push_back(h);
				}
			}
			std::lock_guard<std::mutex> lock(_dataMutex);
			_latestWorldState = list;
		}
	}
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
	if (_isHost)
	{
		broadcastGameOver(enemyGroup);
	}
	else if (_isClient && _tcpSocket >= 0)
	{
		string msg = format("SURRENDER:{}\n", myGroup);
		send(_tcpSocket, msg.c_str(), msg.size(), 0);
	}
}

void LanManager::broadcastGameOver(int winnerGroup)
{
	_winnerGroup = winnerGroup;
	if (!_isHost) return;

	string msg = format("OVER:{}\n", winnerGroup);
	std::lock_guard<std::mutex> lock(_dataMutex);
	for (int fd : _clientSockets)
	{
		if (fd >= 0)
			send(fd, msg.c_str(), msg.size(), 0);
	}
}
