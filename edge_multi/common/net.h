#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "math.h"

// ---- protocol ----
enum PacketType : uint8_t {
    C2S_INPUT,
    S2C_SNAPSHOT,
    S2C_EVENT,
};

// client → server: sent every tick
struct InputPacket {
    PacketType type = C2S_INPUT;
    uint16_t seq;
    bool up, down, left, right;
    float aimX, aimY;   // aim direction (screen relative? normalized)
    bool shoot;
    bool dash;
    int upgradeChoice;   // -1 = none
};

// server → client: frequent state sync
struct PlayerState {
    uint8_t id;
    float x, y;
    float hp, maxHp;
    float shieldHp, shieldMax;
    int level, xp, xpNeeded;
    float stamina, maxStamina;
    int bulletCount;
    float fireRate;
};

struct EnemyState {
    uint16_t id;
    uint8_t type;       // 0=ball, 1=triangle, 2=square, 3=sniper, 4=runner,
                        // 5=miniboss_sniper, 6=miniboss_blinker, 7=miniboss_dasher, 8=boss
    float x, y;
    float hp, maxHp;
    float shieldHp, shieldMax;
    float r;
    uint8_t r_, g_, b_; // color
};

struct BulletState {
    uint16_t id;
    float x, y;
    float r;
    uint8_t r_, g_, b_;
    bool enemy;   // true = enemy bullet, false = player bullet
};

struct SnapshotPacket {
    PacketType type = S2C_SNAPSHOT;
    uint16_t seq;

    uint8_t playerCount;
    PlayerState players[8];

    uint16_t enemyCount;
    EnemyState enemies[256];

    uint16_t bulletCount;
    BulletState bullets[512];

    int waveNumber, totalWaves;
    int score;
    bool gameStarted, gameOver;

    uint8_t bossPhaseIndex;
    float bossHp, bossMaxHp;

    uint8_t minibossesLeft; // how many more minibosses remain
};

// server → client: infrequent events
enum EventType : uint8_t {
    EV_PLAYER_JOINED,
    EV_PLAYER_LEFT,
    EV_WAVE_START,
    EV_GAME_OVER,
    EV_VICTORY,
    EV_UPGRADE_CHOICE,  // client must show upgrade UI
};

struct EventPacket {
    PacketType type = S2C_EVENT;
    EventType event;
    uint8_t playerId;   // relevant player
    char text[64];
};

// ---- serialisation helpers ----
inline void writeU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
inline void writeU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
}
inline void writeFloat(std::vector<uint8_t>& buf, float v) {
    uint32_t uv;
    memcpy(&uv, &v, 4);
    buf.push_back(uv & 0xFF);
    buf.push_back((uv >> 8) & 0xFF);
    buf.push_back((uv >> 16) & 0xFF);
    buf.push_back((uv >> 24) & 0xFF);
}
inline uint8_t readU8(const uint8_t*& p) { return *p++; }
inline uint16_t readU16(const uint8_t*& p) {
    uint16_t v = p[0] | (p[1] << 8);
    p += 2;
    return v;
}
inline float readFloat(const uint8_t*& p) {
    uint32_t uv = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;
    float v;
    memcpy(&v, &uv, 4);
    return v;
}
