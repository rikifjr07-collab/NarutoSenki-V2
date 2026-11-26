#include "LanManager.h"
#include "Core/Provider.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <curl/curl.h>
#include <openssl/err.h>
#include <sstream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>

static const string APINATOR_HOST    = "ws-eu.apinator.io";
static const string APINATOR_KEY     = "app_d8d27a49ce4cdf73da83ac5dd033a098a2c0045a";
static const string APINATOR_SECRET  = "526c5f1907320c7666836ffbb48fa48fe26cf22924257bbac7d7eb9b4df6e192";

namespace {
    static const uint32_t k256[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void sha256Raw(const uint8_t* data, size_t len, uint8_t out[32]) {
        uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        uint64_t bitLen = len * 8;
        size_t paddedLen = ((len + 8) / 64 + 1) * 64;
        std::vector<uint8_t> buf(paddedLen, 0);
        memcpy(buf.data(), data, len);
        buf[len] = 0x80;
        for (int i = 0; i < 8; i++) buf[paddedLen - 1 - i] = (bitLen >> (i * 8)) & 0xff;

        for (size_t offset = 0; offset < paddedLen; offset += 64) {
            uint32_t w[64];
            for (int i = 0; i < 16; i++)
                w[i] = (buf[offset + i*4] << 24) | (buf[offset + i*4 + 1] << 16) | (buf[offset + i*4 + 2] << 8) | buf[offset + i*4 + 3];
            for (int i = 16; i < 64; i++) {
                uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
                uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }
            uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
            for (int i = 0; i < 64; i++) {
                uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t temp1 = hh + S1 + ch + k256[i] + w[i];
                uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t temp2 = S0 + maj;
                hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
            }
            h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        }
        for (int i = 0; i < 8; i++) {
            out[i*4] = (h[i] >> 24) & 0xff; out[i*4+1] = (h[i] >> 16) & 0xff;
            out[i*4+2] = (h[i] >> 8) & 0xff; out[i*4+3] = h[i] & 0xff;
        }
    }

    string hmacSha256Hex(const string& key, const string& msg) {
        uint8_t kPad[64] = {0};
        if (key.size() > 64) sha256Raw((const uint8_t*)key.data(), key.size(), kPad);
        else memcpy(kPad, key.data(), key.size());

        std::vector<uint8_t> iKeyMsg(64 + msg.size());
        uint8_t oKeyPad[64];
        for (int i = 0; i < 64; i++) {
            iKeyMsg[i] = kPad[i] ^ 0x36;
            oKeyPad[i] = kPad[i] ^ 0x5c;
        }
        memcpy(iKeyMsg.data() + 64, msg.data(), msg.size());
        uint8_t innerHash[32];
        sha256Raw(iKeyMsg.data(), iKeyMsg.size(), innerHash);

        uint8_t outerBuf[96];
        memcpy(outerBuf, oKeyPad, 64);
        memcpy(outerBuf + 64, innerHash, 32);
        uint8_t finalHash[32];
        sha256Raw(outerBuf, 96, finalHash);

        char hexOut[65];
        for (int i = 0; i < 32; i++) sprintf(hexOut + i * 2, "%02x", finalHash[i]);
        hexOut[64] = '\0';
        return string(hexOut);
    }
        bool recvExactCurl(CURL* curl, curl_socket_t sockfd, uint8_t* buf, size_t len) {
        size_t got = 0;
        while (got < len) {
            size_t n = 0;
            CURLcode res = curl_easy_recv(curl, buf + got, len - got, &n);
            if (res == CURLE_AGAIN) {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(sockfd, &readfds);
                timeval tv{30, 0};
                if (select((int)sockfd + 1, &readfds, nullptr, nullptr, &tv) <= 0) return false;
                continue;
            }
            if (res != CURLE_OK || n == 0) return false;
            got += n;
        }
        return true;
    }

    bool sendExactCurl(CURL* curl, curl_socket_t sockfd, const uint8_t* buf, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            size_t n = 0;
            CURLcode res = curl_easy_send(curl, buf + sent, len - sent, &n);
            if (res == CURLE_AGAIN) {
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(sockfd, &writefds);
                timeval tv{5, 0};
                if (select((int)sockfd + 1, nullptr, &writefds, nullptr, &tv) <= 0) return false;
                continue;
            }
            if (res != CURLE_OK) return false;
            sent += n;
        }
        return true;
	}

    string escapeJson(const string& s) {
        string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else out += c;
        }
        return out;
    }

    string unescapeJson(const string& s) {
        string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char nxt = s[i + 1];
                if (nxt == '"') { out += '"'; i++; }
                else if (nxt == '\\') { out += '\\'; i++; }
                else if (nxt == 'n') { out += '\n'; i++; }
                else if (nxt == 'r') { out += '\r'; i++; }
                else out += s[i];
            } else {
                out += s[i];
            }
        }
        return out;
    }

    string extractJsonField(const string& json, const string& key) {
        string pattern = "\"" + key + "\":";
        size_t pos = json.find(pattern);
        if (pos == string::npos) return "";
        pos += pattern.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return "";

        if (json[pos] == '"') {
            pos++;
            string val;
            while (pos < json.size()) {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    val += json[pos];
                    val += json[pos + 1];
                    pos += 2;
                } else if (json[pos] == '"') {
                    break;
                } else {
                    val += json[pos++];
                }
            }
            return unescapeJson(val);
        } else if (json[pos] == '{') {
            int depth = 1;
            size_t start = pos++;
            while (pos < json.size() && depth > 0) {
                if (json[pos] == '"') {
                    pos++;
                    while (pos < json.size()) {
                        if (json[pos] == '\\') pos += 2;
                        else if (json[pos] == '"') { pos++; break; }
                        else pos++;
                    }
                } else {
                    if (json[pos] == '{') depth++;
                    else if (json[pos] == '}') depth--;
                    pos++;
                }
            }
            return json.substr(start, pos - start);
        }
        return "";
    }
}

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
    _wsConnected = false;
    _channelSubscribed = false;
    _socketId = "";
    _roomCode = "";
    _nextPlayerId = 2;
	{
		std::lock_guard<std::mutex> lock(_sendMutex);
		while (!_sendQueue.empty())
		{
			_sendQueue.pop();
		}
	}

	int oldSock = _sock;
	_sock = -1;
	if (oldSock >= 0)
	{
		shutdown(oldSock, SHUT_RDWR);
		close(oldSock);
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

bool LanManager::sendWsFrame(const string &text)
{
	std::lock_guard<std::mutex> lock(_sendMutex);
	if (!_wsConnected) return false;
	_sendQueue.push(text);
	return true;
}

bool sendWsFrameDirect(CURL* curl, curl_socket_t sockfd, const string &text)
{
	size_t len = text.size();
	std::vector<uint8_t> frame;
	frame.reserve(len + 14);

	frame.push_back(0x81);
	if (len <= 125)
	{
		frame.push_back(0x80 | (uint8_t)len);
	}
	else if (len <= 65535)
	{
		frame.push_back(0x80 | 126);
		frame.push_back((len >> 8) & 0xff);
		frame.push_back(len & 0xff);
	}
	else
	{
		frame.push_back(0x80 | 127);
		for (int i = 7; i >= 0; i--)
			frame.push_back(((uint64_t)len >> (i * 8)) & 0xff);
	}

	uint8_t mask[4] = {0x37, 0x91, 0x4b, 0xc2};
	frame.insert(frame.end(), mask, mask + 4);

	for (size_t i = 0; i < len; i++)
		frame.push_back((uint8_t)text[i] ^ mask[i % 4]);

	return sendExactCurl(curl, sockfd, frame.data(), frame.size());
}

void LanManager::networkThreadLoop()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	CURL* curl = curl_easy_init();
	if (!curl)
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Gagal Init CURL";
		_isHost = false;
		_isClient = false;
		return;
	}

	string httpsUrl = "https://" + APINATOR_HOST + "/";
	curl_easy_setopt(curl, CURLOPT_URL, httpsUrl.c_str());
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		curl_easy_cleanup(curl);
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = string("TLS Err: ") + curl_easy_strerror(res);
		_isHost = false;
		_isClient = false;
		return;
	}

	curl_socket_t sockfd = CURL_SOCKET_BAD;
	curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sockfd);
	if (sockfd == CURL_SOCKET_BAD)
	{
		long sockLong = -1;
		curl_easy_getinfo(curl, CURLINFO_LASTSOCKET, &sockLong);
		sockfd = (curl_socket_t)sockLong;
	}

	_ssl = curl;
	_sock = (int)sockfd;

	string req =
		"GET /app/" + APINATOR_KEY + " HTTP/1.1\r\n"
		"Host: " + APINATOR_HOST + "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n";

	if (!sendExactCurl(curl, sockfd, (const uint8_t*)req.data(), req.size()))
	{
		disconnect();
		return;
	}

	string respHeader;
	uint8_t ch = 0;
	while (_running && respHeader.find("\r\n\r\n") == string::npos)
	{
		if (!recvExactCurl(curl, sockfd, &ch, 1))
		{
			disconnect();
			return;
		}
		respHeader += (char)ch;
		if (respHeader.size() > 4096) { disconnect(); return; }
	}

	if (respHeader.find("101") == string::npos)
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Err: " + respHeader.substr(0, 22);
		_isHost = false;
		_isClient = false;
		return;
	}

	_wsConnected = true;
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Otentikasi Room...";
	}

    while (_running && _ssl)
	{
		// 1. Kirim semua pesan yang ada di antrean _sendQueue (aman dalam 1 thread)
		while (_running)
		{
			string nextMsg;
			{
				std::lock_guard<std::mutex> lock(_sendMutex);
				if (_sendQueue.empty()) break;
				nextMsg = _sendQueue.front();
				_sendQueue.pop();
			}
			if (!sendWsFrameDirect(curl, sockfd, nextMsg))
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				_statusMsg = "Gagal Kirim WS!";
				_running = false;
				break;
			}
			else
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				if (_statusMsg.find("Subscribing") == 0)
					_statusMsg = "Sent Sub " + _roomCode + "...";
			}

		}
		if (!_running) break;
				// 2. Cek apakah ada data di buffer SSL atau socket tanpa tertahan select()
				// 2. Cek apakah ada data masuk dari server (tunggu 10ms)
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		timeval tv{0, 1000};
		int sel = select((int)sockfd + 1, &readfds, nullptr, nullptr, &tv);
		if (sel < 0) break;
		if (sel == 0) continue;

		// 3. Baca 2 byte header frame WebSocket
		uint8_t hdr[2];
		if (!recvExactCurl(curl, sockfd, hdr, 2)) break;
		uint8_t opcode = hdr[0] & 0x0f;
		bool masked = (hdr[1] & 0x80) != 0;
		uint64_t payloadLen = hdr[1] & 0x7f;

		if (payloadLen == 126)
		{
			uint8_t ext[2];
			if (!recvExactCurl(curl, sockfd, ext, 2)) break;
			payloadLen = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1];
		}
		else if (payloadLen == 127)
		{
			uint8_t ext[8];
			if (!recvExactCurl(curl, sockfd, ext, 8)) break;
			payloadLen = 0;
			for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | (uint64_t)ext[i];
		}

		uint8_t maskKey[4] = {0};
		if (masked && !recvExactCurl(curl, sockfd, maskKey, 4)) break;

		if (payloadLen > 1024 * 512) break;
		std::vector<uint8_t> payload(payloadLen);
		if (payloadLen > 0 && !recvExactCurl(curl, sockfd, payload.data(), payloadLen)) break;

		if (masked)
		{
			for (size_t i = 0; i < payloadLen; i++) payload[i] ^= maskKey[i % 4];
		}

		if (opcode == 0x8)
		{
			if (payloadLen >= 2)
			{
				uint16_t closeCode = ((uint16_t)payload.at(0) << 8) | (uint16_t)payload.at(1);
				string reasonText = (payloadLen > 2) ? string((const char*)payload.data() + 2, payloadLen - 2) : "";
				std::lock_guard<std::mutex> lock(_dataMutex);
				_statusMsg = "WS " + std::to_string(closeCode) + ": " + reasonText.substr(0, 20);
			}
			break;
		}

		else if (opcode == 0x9)
		{
			std::vector<uint8_t> pongFrame;
			pongFrame.reserve(payloadLen + 6);
			pongFrame.push_back(0x8A);
			if (payloadLen <= 125)
			{
				pongFrame.push_back(0x80 | (uint8_t)payloadLen);
			}
			else
			{
				pongFrame.push_back(0x80 | 126);
				pongFrame.push_back((payloadLen >> 8) & 0xff);
				pongFrame.push_back(payloadLen & 0xff);
			}
			uint8_t pMask[4] = {0x11, 0x22, 0x33, 0x44};
			pongFrame.insert(pongFrame.end(), pMask, pMask + 4);
			for (size_t i = 0; i < payloadLen; i++)
			{
				pongFrame.push_back(payload.at(i) ^ pMask[i % 4]);
			}
			sendExactCurl(curl, sockfd, pongFrame.data(), pongFrame.size());
		}

		else if (opcode == 0x1)
		{
			string msg((const char*)payload.data(), payloadLen);
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				if (_statusMsg.find("Sent Sub") == 0)
					_statusMsg = "Rx: " + msg.substr(0, 28);
			}
			handleWsMessage(msg);
		}

	}

	{
		std::lock_guard<std::mutex> lock(_sendMutex);
		_ssl = nullptr;
	}
	curl_easy_cleanup(curl);

	if (_running)
	{
		_wsConnected = false;
		_channelSubscribed = false;
		_isHost = false;
		_isClient = false;
		std::lock_guard<std::mutex> lock(_dataMutex);
		if (_statusMsg == "Otentikasi Room...")
			_statusMsg = "Koneksi Terputus";
	}
}

void LanManager::handleWsMessage(const string &rawMsg)
{
	string eventName = extractJsonField(rawMsg, "event");
	string eventData = extractJsonField(rawMsg, "data");

	if (eventName == "realtime:connection_established" || eventName == "connection_established" || eventName == "pusher:connection_established")
	{
		_socketId = extractJsonField(eventData, "socket_id");
		if (!_socketId.empty())
		{
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				_statusMsg = "Subscribing " + _roomCode + "...";
			}

			string channelName = "private-room-" + _roomCode;
			string signPayload = _socketId + ":" + channelName;
			string signature = hmacSha256Hex(APINATOR_SECRET, signPayload);
			string authString = APINATOR_KEY + ":" + signature;

			string innerData = "{\"channel\":\"" + channelName + "\",\"auth\":\"" + authString + "\"}";
			string subJson = "{\"event\":\"realtime:subscribe\",\"data\":\"" + escapeJson(innerData) + "\"}";
			sendWsFrame(subJson);
		}
		else
		{
			std::lock_guard<std::mutex> lock(_dataMutex);
			_statusMsg = "NoSocketId: " + rawMsg.substr(0, 22);
		}
	}
	else if (eventName == "realtime:subscription_succeeded" || eventName == "subscription_succeeded" || eventName == "pusher_internal:subscription_succeeded")
	{
		_channelSubscribed = true;
		if (_isHost)
		{
			std::lock_guard<std::mutex> lock(_dataMutex);
			_statusMsg = "Room: " + _roomCode + " (1/6)";
		}
		else if (_isClient)
		{
			{
				std::lock_guard<std::mutex> lock(_dataMutex);
				_statusMsg = "Minta Slot Room " + _roomCode + "...";
			}
			sendGamePacket("HELLO:" + _clientTempId);
		}
	}
	else if (eventName == "realtime:ping" || eventName == "pusher:ping")
	{
		sendWsFrame("{\"event\":\"realtime:pong\",\"data\":\"{}\"}");
	}
	else if (eventName == "realtime:error" || eventName == "realtime:subscription_error" || eventName == "pusher:error")
	{
		string errMsg = extractJsonField(eventData, "message");
		if (errMsg.empty()) errMsg = extractJsonField(eventData, "error");
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Err: " + (errMsg.empty() ? rawMsg.substr(0, 26) : errMsg.substr(0, 26));
	}
	else if (eventName == "client-pkt")
	{
		string pkt = extractJsonField(eventData, "p");
		if (!pkt.empty())
		{
			processGamePacket(pkt);
		}
	}
	else
	{
		std::lock_guard<std::mutex> lock(_dataMutex);
		_statusMsg = "Srv: " + (eventName.empty() ? rawMsg.substr(0, 26) : eventName.substr(0, 26));
	}
}

void LanManager::sendApinatorEvent(const string &eventName, const string &payloadData)
{
	if (!_wsConnected || !_channelSubscribed) return;
	string channelName = "private-room-" + _roomCode;
	string innerData = "{\"p\":\"" + escapeJson(payloadData) + "\"}";
	string json = "{\"event\":\"" + eventName + "\",\"channel\":\"" + channelName + "\",\"data\":\"" + escapeJson(innerData) + "\"}";
	sendWsFrame(json);
}

void LanManager::sendGamePacket(const string &rawPacket)
{
	string tagged = (_isHost ? "H|" : "C|") + rawPacket;
	sendApinatorEvent("client-pkt", tagged);
}

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
				sendGamePacket(format("ASSIGN:{},{}", tempId, assignedId));
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
		else if (pkt.find("PING:") == 0)
		{
			sendGamePacket("PONG:" + pkt.substr(5));
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
		else if (pkt.find("PONG:") == 0)
		{
			long long sentTime = std::stoll(pkt.substr(5));
			auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			int rtt = static_cast<int>((now - sentTime) / 2);
			_pingMs = (rtt < 0) ? 0 : rtt;
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
	sendGamePacket(pkt);
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
		sendGamePacket(format("MOVE:{},{}", _myPlayerId, targetSlot));
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
		sendGamePacket(format("HERO:{},{}", _myPlayerId, heroName));
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
	sendGamePacket("START");
	_gameStarted = true;
}

void LanManager::sendClientInput(float dirX, float dirY, int actionType, float posX, float posY, bool isFlipped)
{
    if (!_isClient || !_channelSubscribed) return;
    string pkt = format("IN:{},{:.2f},{:.2f},{},{:.1f},{:.1f},{}",
                        _myPlayerId, dirX, dirY, actionType, posX, posY, isFlipped ? 1 : 0);
    sendGamePacket(pkt);
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
	sendGamePacket(pkt);
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
	else if (_isClient) sendGamePacket(format("SURRENDER:{}", myGroup));
}

void LanManager::broadcastGameOver(int winnerGroup)
{
	_winnerGroup = winnerGroup;
	if (!_isHost) return;
	sendGamePacket(format("OVER:{}", winnerGroup));
}

void LanManager::sendPing()
{
    if (!_isClient || !_channelSubscribed) return;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    sendGamePacket(format("PING:{}", now));
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
	sendGamePacket(pkt);
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
	sendGamePacket(pkt);
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
    sendGamePacket(pkt);
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
    sendGamePacket(format("KILL:{},{},{},{},{}", killerName, deadName, killNum, konohaKills, akatsukiKills));
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
    sendGamePacket(format("GARD:{},{}", guardName, group));
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
