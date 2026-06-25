#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

// ---- protocol ----
enum PacketType : uint8_t { PKT_INPUT, PKT_SNAPSHOT };

struct InputPacket {
    uint16_t seq;
    bool up, down, left, right;
    float aimX, aimY;
    bool shoot;
};

struct NetPlayer {
    uint8_t id;
    float x, y;
    float hp, maxHp;
    float shieldHp, shieldMax;
    int level;
};

struct NetEnemy {
    uint16_t id;
    uint8_t type;
    float x, y;
    float hp, maxHp;
    float shieldHp, shieldMax;
    float r;
    uint8_t r_, g_, b_;
};

struct NetBullet {
    float x, y;
    float r;
    bool enemy;
};

struct Snapshot {
    uint16_t seq;
    int playerCount;
    NetPlayer players[8];
    int enemyCount;
    NetEnemy enemies[256];
    int bulletCount;
    NetBullet bullets[512];
    int waveNumber;
    int score;
    bool gameOver;
    float bossHp, bossMaxHp;
    uint8_t bossPhaseIndex;
};

// ---- public API ----
bool netInit();
void netCleanup();
bool netHostStart(int port);
bool netConnect(const char* host, int port);
void netDisconnect();
void netTick();
bool netIsHost();
bool netIsClient();
bool netIsConnected();
int  netGetClientCount();
void netSendInput(const InputPacket& inp);
bool netReceiveSnapshot(Snapshot& snap);
void netBroadcastSnapshot(const Snapshot& snap);
void netSetLocalPlayerId(uint8_t id);
uint8_t netGetLocalPlayerId();
bool netClientSentInput();
