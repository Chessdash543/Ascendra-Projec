#pragma once
#include <vector>
#include <cstdint>
#include "common/math.h"
#include "common/net.h"

// ---- constants ----
constexpr int WORLD_W = 2600;
constexpr int WORLD_H = 1800;
constexpr int TOTAL_WAVES = 15;
constexpr int MINIBOSS_COUNT = 3;
constexpr int TICK_RATE = 60;

// ---- entity types ----
enum EnemyType : uint8_t {
    ENEMY_BALL, ENEMY_TRIANGLE, ENEMY_SQUARE, ENEMY_SNIPER_COMMON,
    ENEMY_RUNNER, ENEMY_MINIBOSS_SNIPER, ENEMY_MINIBOSS_BLINKER,
    ENEMY_MINIBOSS_DASHER, ENEMY_BOSS,
};

// ---- in-game entities ----
struct Bullet {
    Vec2 pos, vel;
    float r, speedMag;
    bool enemy;
    uint16_t id;
};

struct Enemy {
    uint16_t id;
    EnemyType type;
    Vec2 pos;
    float hp, maxHp, speed, damage, r;
    float shieldHp, shieldMax;
    int shootTimer, shootCooldown;
    float chargeTimer, chargeTime, laserAngle;
    int blinkTimer, blinkCooldown;
    const char* blinkStage;
    int dashTimer, dashCooldown;
    bool dashing;
    float dashDx, dashDy, dashDuration;
};

struct Player {
    uint8_t id;
    Vec2 pos;
    float hp, maxHp;
    float shieldHp, shieldMax;
    float stamina, maxStamina;
    int level, xp, xpNeeded;
    int bulletCount;
    float fireRate, bulletSpeed, damage;
    bool dashCooldown;
    float aimX, aimY;
    bool shootPressed, dashPressed;
    int lastInputSeq;
    bool connected;
    bool upgradeChoicePending;
    bool gameOver;
};

// ---- wave state ----
enum WavePhase { WAVE_NORMAL, WAVE_MINIBOSS, WAVE_BOSS_INTRO, WAVE_BOSS, WAVE_CLEARED };

struct WaveState {
    int number = 0;
    WavePhase phase = WAVE_NORMAL;
    int enemiesToSpawn = 0;
    int spawnTimer = 0;
    int spawnInterval = 1000;
    int minibossesDefeated = 0;
    bool waitingNextWave = false;
    int announcementTimer = 0;
};

// ---- boss state ----
enum BossPhase { BP_IDLE, BP_COOLDOWN, BP_LASER360, BP_SUPERLASER, BP_TELEPORTBOMB, BP_QUANTUMLASER };

struct BossState {
    Enemy* obj = nullptr;
    BossPhase phase = BP_IDLE;
    int phaseTimer = 0, attackCooldown = 0;
    int bossPhaseIndex = 0;
    int shockwaveTimer = 0;
    float damageAccumulated = 0;
    struct { int timer=0; float angle=0; bool charging=true; int chargeDuration=550, fireDuration=900; } superLaser;
    struct { int timer=0; const char* stage="none"; float targetX=0, targetY=0; } teleport;
    struct { int timer=0; const char* phase="forming"; int duration=700, fireDuration=300; } quantumLaser;
};

// ---- world ----
struct World {
    std::vector<Player> players;
    std::vector<Enemy> enemies;
    std::vector<Bullet> bullets;
    WaveState wave;
    BossState boss;
    uint16_t nextEnemyId = 1;
    uint16_t nextBulletId = 1;
    int score = 0;
    int fireTimer = 0;
    int playerContactCooldown = 0;
    bool gameStarted = false;
    bool gameOver = false;

    void init();
    void tick(int dtMs);
    void applyInput(uint8_t playerId, const InputPacket& input);
    void addPlayer(uint8_t id);
    void removePlayer(uint8_t id);

    void buildSnapshot(SnapshotPacket& snap);
};
