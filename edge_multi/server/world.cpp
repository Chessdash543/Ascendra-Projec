#include "world.h"
#include <algorithm>
#include <cstring>

static Vec2 spawnPosOffscreen() {
    float a = randf() * M_PI_F * 2;
    float d = 350 + randf() * 150;
    return Vec2(WORLD_W/2 + cosf(a)*d, WORLD_H/2 + sinf(a)*d);
}

static EnemyType randomCommonType() {
    return (EnemyType)(rand() % 5);
}

void World::addPlayer(uint8_t id) {
    Player p;
    p.id = id;
    p.pos = Vec2(WORLD_W/2 + (randf()-0.5f)*200, WORLD_H/2 + (randf()-0.5f)*200);
    p.hp = p.maxHp = 100;
    p.shieldHp = p.shieldMax = 0;
    p.stamina = p.maxStamina = 60;
    p.level = 1; p.xp = 0; p.xpNeeded = 0;
    p.bulletCount = 1;
    p.fireRate = 500; p.bulletSpeed = 10; p.damage = 12;
    p.dashCooldown = false;
    p.aimX = 1; p.aimY = 0;
    p.shootPressed = false; p.dashPressed = false;
    p.lastInputSeq = -1;
    p.connected = true;
    p.upgradeChoicePending = false;
    p.gameOver = false;
    players.push_back(p);
}

void World::removePlayer(uint8_t id) {
    for (size_t i = 0; i < players.size(); i++) {
        if (players[i].id == id) {
            players[i].connected = false;
            if (players.size() <= 1) {
                gameOver = true;
            }
            break;
        }
    }
}

void World::init() {
    wave.number = 0;
    wave.phase = WAVE_NORMAL;
    wave.minibossesDefeated = 0;
    wave.waitingNextWave = false;
    score = 0;
    gameStarted = false;
    gameOver = false;
    enemies.clear();
    bullets.clear();
}

static void spawnEnemiesForWave(World& w, int waveNum) {
    int count = 8 + (int)(waveNum * 4.5f);
    w.wave.enemiesToSpawn = count;
    w.wave.spawnTimer = 0;
    w.wave.spawnInterval = std::max(280, 950 - waveNum * 40);
}

static void spawnMiniboss(World& w) {
    Vec2 p = spawnPosOffscreen();
    EnemyType types[] = {ENEMY_MINIBOSS_SNIPER, ENEMY_MINIBOSS_BLINKER, ENEMY_MINIBOSS_DASHER};
    EnemyType t = types[w.wave.minibossesDefeated % 3];
    Enemy e;
    e.id = w.nextEnemyId++;
    e.type = t;
    e.pos = p;
    float waveScale = 1 + (w.wave.number - 1) * 0.18f + w.score * 0.0004f;
    e.shootTimer = 0;
    switch (t) {
        case ENEMY_MINIBOSS_SNIPER:
            e.r = 38; e.maxHp = 1500 * waveScale; e.speed = 0.6f; e.damage = 55;
            e.shootCooldown = 300; e.shieldMax = 600 * waveScale; e.shieldHp = e.shieldMax;
            e.chargeTime = 2200; e.chargeTimer = 0;
            break;
        case ENEMY_MINIBOSS_BLINKER:
            e.r = 36; e.maxHp = 1300 * waveScale; e.speed = 1.4f; e.damage = 20;
            e.shootCooldown = 300; e.shieldMax = 500 * waveScale; e.shieldHp = e.shieldMax;
            e.blinkCooldown = 3500; e.blinkTimer = 0; e.blinkStage = "moving";
            break;
        case ENEMY_MINIBOSS_DASHER:
            e.r = 32; e.maxHp = 1200 * waveScale; e.speed = 2.8f; e.damage = 38;
            e.shootCooldown = 300; e.shieldMax = 400 * waveScale; e.shieldHp = e.shieldMax;
            e.dashCooldown = 2000; e.dashTimer = 0; e.dashing = false;
            break;
        default: break;
    }
    e.hp = e.maxHp;
    w.enemies.push_back(e);
}

static void spawnBoss(World& w) {
    Enemy e;
    e.id = w.nextEnemyId++;
    e.type = ENEMY_BOSS;
    e.pos = Vec2(WORLD_W/2, WORLD_H/2 - 400);
    e.r = 70;
    e.maxHp = 400000;
    e.hp = e.maxHp;
    e.speed = 1.8f;
    e.damage = 22;
    e.shieldHp = e.shieldMax = 0;
    w.enemies.push_back(e);
    w.boss.obj = &w.enemies.back();
    w.boss.phase = BP_COOLDOWN;
    w.boss.attackCooldown = 900;
    w.boss.bossPhaseIndex = 0;
    w.boss.damageAccumulated = 0;
}

static void startNextWave(World& w) {
    w.wave.waitingNextWave = false;
    int nextWave = w.wave.number + 1;

    if (nextWave == 5 || nextWave == 10 || nextWave == 15) {
        w.wave.number = nextWave;
        w.wave.phase = WAVE_MINIBOSS;
        spawnMiniboss(w);
        w.wave.announcementTimer = 2500;
    } else if (nextWave <= TOTAL_WAVES) {
        w.wave.number = nextWave;
        w.wave.phase = WAVE_NORMAL;
        spawnEnemiesForWave(w, nextWave);
        w.wave.announcementTimer = 2500;
    } else if (w.wave.minibossesDefeated >= MINIBOSS_COUNT) {
        spawnBoss(w);
        w.wave.phase = WAVE_BOSS;
        w.wave.announcementTimer = 2500;
    } else {
        w.wave.phase = WAVE_MINIBOSS;
        spawnMiniboss(w);
    }
}

static void updateWaveLogic(World& w, int dt) {
    if (w.wave.phase == WAVE_NORMAL) {
        if (w.wave.enemiesToSpawn > 0) {
            w.wave.spawnTimer += dt;
            if (w.wave.spawnTimer >= w.wave.spawnInterval) {
                w.wave.spawnTimer = 0;
                w.wave.enemiesToSpawn--;
                Vec2 p = spawnPosOffscreen();
                Enemy e;
                e.id = w.nextEnemyId++;
                e.type = randomCommonType();
                e.pos = p;
                float waveScale = 1 + (w.wave.number - 1) * 0.18f + w.score * 0.0004f;
                float speedMul = 1 + (w.wave.number - 1) * 0.08f;
                switch (e.type) {
                    case ENEMY_BALL:
                        e.r = 16; e.maxHp = 50 * waveScale; e.speed = 1.8f * speedMul; e.damage = 10;
                        e.shootCooldown = 1800 + (int)(randf()*600); e.shootTimer = (int)(e.shootCooldown * randf());
                        break;
                    case ENEMY_TRIANGLE:
                        e.r = 18; e.maxHp = 30 * waveScale; e.speed = 2.4f * speedMul; e.damage = 12;
                        e.shootCooldown = 0; e.shootTimer = 0;
                        break;
                    case ENEMY_SQUARE:
                        e.r = 22; e.maxHp = 80 * waveScale; e.speed = 1.0f * speedMul; e.damage = 8;
                        e.shootCooldown = 1400 + (int)(randf()*600); e.shootTimer = (int)(e.shootCooldown * randf());
                        break;
                    case ENEMY_SNIPER_COMMON:
                        e.r = 15; e.maxHp = 25 * waveScale; e.speed = 1.2f * speedMul; e.damage = 15;
                        e.shootCooldown = 1800 + (int)(randf()*400); e.shootTimer = (int)(e.shootCooldown * randf());
                        break;
                    case ENEMY_RUNNER:
                        e.r = 14; e.maxHp = 20 * waveScale; e.speed = 3.5f * speedMul; e.damage = 14;
                        e.shootCooldown = 0; e.shootTimer = 0;
                        break;
                    default: break;
                }
                e.hp = e.maxHp;
                e.shieldHp = e.shieldMax = 0;
                e.dashing = false; e.chargeTimer = 0;
                w.enemies.push_back(e);
            }
        } else if (w.enemies.empty() && !w.wave.waitingNextWave) {
            w.wave.waitingNextWave = true;
            w.wave.minibossesDefeated = 0;
            startNextWave(w);
        }
    } else if (w.wave.phase == WAVE_MINIBOSS) {
        if (w.enemies.empty() && !w.wave.waitingNextWave) {
            w.wave.waitingNextWave = true;
            w.wave.minibossesDefeated++;
            startNextWave(w);
        }
    } else if (w.wave.phase == WAVE_BOSS) {
        if (w.enemies.empty() && !w.wave.waitingNextWave) {
            w.wave.waitingNextWave = true;
            w.wave.phase = WAVE_CLEARED;
        }
    }
}

static void spawnBullet(World& w, Vec2 pos, Vec2 vel, float r, float speedMag, bool enemy, float damage) {
    Bullet b;
    b.id = w.nextBulletId++;
    b.pos = pos;
    b.vel = vel;
    b.r = r;
    b.speedMag = speedMag;
    b.enemy = enemy;
    w.bullets.push_back(b);
}

static void updateEnemyAI(World& w, Enemy& e, int dt) {
    if (e.hp <= 0) return;

    // find closest player
    float nearDist = 1e9f;
    Vec2 toPlayer;
    for (auto& p : w.players) {
        if (!p.connected) continue;
        float d = dist(e.pos, p.pos);
        if (d < nearDist) { nearDist = d; toPlayer = p.pos - e.pos; }
    }
    float d = toPlayer.len();
    if (d < 1) d = 1;
    Vec2 dir = toPlayer / d;

    switch (e.type) {
        case ENEMY_BALL: {
            float moveX = dir.x, moveY = dir.y;
            e.shootTimer += dt;
            if (e.shootTimer >= e.shootCooldown) {
                e.shootTimer = 0;
                Vec2 aim = (d > 200) ? dir : (toPlayer / d * -1);
                float a = atan2f(aim.y, aim.x);
                spawnBullet(w, e.pos, Vec2(cosf(a)*4, sinf(a)*4), 5, 4, true, e.damage);
            }
            e.pos.x += moveX * e.speed;
            e.pos.y += moveY * e.speed;
            break;
        }
        case ENEMY_TRIANGLE: {
            e.pos.x += dir.x * e.speed;
            e.pos.y += dir.y * e.speed;
            break;
        }
        case ENEMY_SQUARE: {
            float moveX = dir.x, moveY = dir.y;
            if (d > 300) { moveX = dir.x; moveY = dir.y; }
            else if (d < 100) { moveX = -dir.x; moveY = -dir.y; }
            else moveX = moveY = 0;
            e.shootTimer += dt;
            if (e.shootTimer >= e.shootCooldown) {
                e.shootTimer = 0;
                Vec2 aim = dir;
                float a = atan2f(aim.y, aim.x);
                spawnBullet(w, e.pos, Vec2(cosf(a)*3, sinf(a)*3), 6, 3, true, e.damage);
            }
            e.pos.x += moveX * e.speed;
            e.pos.y += moveY * e.speed;
            break;
        }
        case ENEMY_SNIPER_COMMON: {
            e.shootTimer += dt;
            if (d > 400) {
                e.pos.x += dir.x * e.speed;
                e.pos.y += dir.y * e.speed;
            }
            if (e.shootTimer >= e.shootCooldown) {
                e.shootTimer = 0;
                float a = atan2f(toPlayer.y, toPlayer.x);
                spawnBullet(w, e.pos, Vec2(cosf(a)*6, sinf(a)*6), 5, 6, true, e.damage);
            }
            break;
        }
        case ENEMY_RUNNER: {
            e.pos.x += dir.x * e.speed;
            e.pos.y += dir.y * e.speed;
            break;
        }
        case ENEMY_MINIBOSS_SNIPER: {
            e.shootTimer += dt;
            if (e.shootTimer >= e.shootCooldown) {
                e.shootTimer = 0;
                float baseAngle = atan2f(toPlayer.y, toPlayer.x);
                for (int i = 0; i < 3; i++) {
                    float spread = (i - 1) * 0.12f;
                    float a = baseAngle + spread;
                    spawnBullet(w, e.pos, Vec2(cosf(a)*7, sinf(a)*7), 5, 7, true, e.damage);
                }
            }
            if (d > 200) { e.pos.x += dir.x * e.speed; e.pos.y += dir.y * e.speed; }
            e.chargeTimer += dt;
            e.laserAngle = atan2f(toPlayer.y, toPlayer.x);
            if (e.chargeTimer >= e.chargeTime) {
                e.chargeTimer = 0;
                float a = e.laserAngle;
                spawnBullet(w, e.pos, Vec2(cosf(a)*12, sinf(a)*12), 8, 12, true, e.damage);
            }
            break;
        }
        case ENEMY_MINIBOSS_BLINKER: {
            e.shootTimer += dt;
            if (e.shootTimer >= e.shootCooldown) {
                e.shootTimer = 0;
                float baseAngle = atan2f(toPlayer.y, toPlayer.x);
                for (int i = 0; i < 3; i++) {
                    float spread = (i - 1) * 0.12f;
                    float a = baseAngle + spread;
                    spawnBullet(w, e.pos, Vec2(cosf(a)*7, sinf(a)*7), 5, 7, true, e.damage);
                }
            }
            if (strcmp(e.blinkStage, "moving") == 0) {
                e.pos.x += dir.x * e.speed; e.pos.y += dir.y * e.speed;
                e.blinkTimer += dt;
                if (e.blinkTimer >= e.blinkCooldown) {
                    e.blinkTimer = 0;
                    e.blinkStage = "warning";
                }
            } else if (strcmp(e.blinkStage, "warning") == 0) {
                e.blinkTimer += dt;
                if (e.blinkTimer >= 400) {
                    e.blinkTimer = 0;
                    float a = randf() * M_PI_F * 2;
                    e.pos.x = clamp(WORLD_W/2 + cosf(a)*(70+randf()*80), 60, WORLD_W-60);
                    e.pos.y = clamp(WORLD_H/2 + sinf(a)*(70+randf()*80), 60, WORLD_H-60);
                    e.blinkStage = "moving";
                }
            }
            break;
        }
        case ENEMY_MINIBOSS_DASHER: {
            e.shootTimer += dt;
            if (e.shootTimer >= e.shootCooldown) {
                e.shootTimer = 0;
                float baseAngle = atan2f(toPlayer.y, toPlayer.x);
                for (int i = 0; i < 3; i++) {
                    float spread = (i - 1) * 0.12f;
                    float a = baseAngle + spread;
                    spawnBullet(w, e.pos, Vec2(cosf(a)*7, sinf(a)*7), 5, 7, true, e.damage);
                }
            }
            if (!e.dashing) {
                e.pos.x += dir.x * e.speed; e.pos.y += dir.y * e.speed;
                e.dashTimer += dt;
                if (e.dashTimer >= e.dashCooldown && d < 350) {
                    e.dashTimer = 0; e.dashing = true;
                    e.dashDx = dir.x; e.dashDy = dir.y;
                    e.dashDuration = 280;
                }
            } else {
                e.pos.x += e.dashDx * e.speed * 8;
                e.pos.y += e.dashDy * e.speed * 8;
                e.dashDuration -= dt;
                if (e.dashDuration <= 0) e.dashing = false;
            }
            break;
        }
        case ENEMY_BOSS: {
            if (e.type != ENEMY_BOSS) break;
            if (d > 200 && w.boss.phase != BP_TELEPORTBOMB) {
                e.pos.x += dir.x * e.speed;
                e.pos.y += dir.y * e.speed;
            }
            float hpPct = e.hp / e.maxHp;
            int maxIdx = hpPct <= 0.25f ? 3 : hpPct <= 0.50f ? 2 : hpPct <= 0.75f ? 1 : 0;
            float cdMul = 1.0f - maxIdx * 0.15f;
            w.boss.phaseTimer += dt;
            if (w.boss.phase == BP_COOLDOWN) {
                w.boss.attackCooldown -= dt;
                if (w.boss.attackCooldown <= 0) {
                    int phases[] = {BP_LASER360, BP_SUPERLASER, BP_TELEPORTBOMB, BP_QUANTUMLASER};
                    w.boss.phase = (BossPhase)phases[rand() % 4];
                    w.boss.phaseTimer = 0;
                }
            } else if (w.boss.phase == BP_LASER360) {
                float baseAngle = w.boss.phaseTimer * 0.002f;
                if (w.boss.phaseTimer % 200 < 20) {
                    for (int i = 0; i < 12; i++) {
                        float a = baseAngle + i * M_PI_F * 2 / 12;
                        spawnBullet(w, e.pos, Vec2(cosf(a)*6, sinf(a)*6), 6, 6, true, e.damage*0.5f);
                    }
                }
                if (w.boss.phaseTimer > 1200) {
                    w.boss.phase = BP_COOLDOWN;
                    w.boss.attackCooldown = (int)(1500 * cdMul);
                }
            } else if (w.boss.phase == BP_SUPERLASER) {
                w.boss.superLaser.timer += dt;
                if (w.boss.superLaser.charging) {
                    if (w.boss.superLaser.timer >= w.boss.superLaser.chargeDuration) {
                        w.boss.superLaser.charging = false;
                        w.boss.superLaser.timer = 0;
                    }
                } else {
                    if (randf() < 0.3f) {
                        float spread = (randf() - 0.5f) * 0.15f;
                        float a = atan2f(toPlayer.y, toPlayer.x) + spread;
                        spawnBullet(w, e.pos + Vec2(cosf(a), sinf(a))*50, Vec2(cosf(a)*10, sinf(a)*10), 7, 10, true, e.damage);
                    }
                    if (w.boss.superLaser.timer >= w.boss.superLaser.fireDuration) {
                        w.boss.phase = BP_COOLDOWN;
                        w.boss.attackCooldown = (int)(1800 * cdMul);
                    }
                }
            } else if (w.boss.phase == BP_TELEPORTBOMB) {
                w.boss.teleport.timer += dt;
                if (strcmp(w.boss.teleport.stage, "none") == 0) {
                    w.boss.teleport.stage = "warning";
                    w.boss.teleport.timer = 0;
                } else if (strcmp(w.boss.teleport.stage, "warning") == 0) {
                    if (w.boss.teleport.timer > 600) {
                        w.boss.teleport.stage = "teleporting";
                        w.boss.teleport.timer = 0;
                        float a = randf() * M_PI_F * 2;
                        float dist = 150 + randf() * 200;
                        w.boss.teleport.targetX = clamp(WORLD_W/2 + cosf(a)*dist, 100, WORLD_W-100);
                        w.boss.teleport.targetY = clamp(WORLD_H/2 + sinf(a)*dist, 100, WORLD_H-100);
                    }
                } else if (strcmp(w.boss.teleport.stage, "teleporting") == 0) {
                    if (w.boss.teleport.timer > 200) {
                        e.pos.x = w.boss.teleport.targetX;
                        e.pos.y = w.boss.teleport.targetY;
                        w.boss.teleport.stage = "none";
                        w.boss.phase = BP_COOLDOWN;
                        w.boss.attackCooldown = (int)(1200 * cdMul);
                    }
                }
            } else if (w.boss.phase == BP_QUANTUMLASER) {
                w.boss.quantumLaser.timer += dt;
                if (strcmp(w.boss.quantumLaser.phase, "forming") == 0) {
                    if (w.boss.quantumLaser.timer >= w.boss.quantumLaser.duration) {
                        w.boss.quantumLaser.phase = "firing";
                        w.boss.quantumLaser.timer = 0;
                    }
                } else if (strcmp(w.boss.quantumLaser.phase, "firing") == 0) {
                    if (w.boss.quantumLaser.timer % 100 < 15) {
                        float baseA = atan2f(toPlayer.y, toPlayer.x);
                        for (int i = 0; i < 5; i++) {
                            float spread = (i - 2) * 0.1f;
                            float a = baseA + spread + (randf() - 0.5f) * 0.08f;
                            spawnBullet(w, e.pos, Vec2(cosf(a)*8, sinf(a)*8), 5, 8, true, e.damage*0.4f);
                        }
                    }
                    if (w.boss.quantumLaser.timer >= w.boss.quantumLaser.fireDuration) {
                        w.boss.phase = BP_COOLDOWN;
                        w.boss.attackCooldown = (int)(2000 * cdMul);
                        w.boss.quantumLaser.phase = "forming";
                    }
                }
            }
            break;
        }
        default: break;
    }

    e.pos.x = clamp(e.pos.x, 10, WORLD_W-10);
    e.pos.y = clamp(e.pos.y, 10, WORLD_H-10);
}

static void updateBullets(World& w, int dt) {
    for (size_t i = 0; i < w.bullets.size();) {
        auto& b = w.bullets[i];
        b.pos += b.vel;

        if (b.pos.x < -200 || b.pos.x > WORLD_W+200 || b.pos.y < -200 || b.pos.y > WORLD_H+200) {
            w.bullets.erase(w.bullets.begin() + i);
            continue;
        }

        bool removed = false;

        if (b.enemy) {
            // enemy bullet hits players
            for (auto& p : w.players) {
                if (!p.connected) continue;
                if (dist(b.pos, p.pos) < b.r + 12) {
                    float dmg = 8;
                    if (p.shieldHp > 0) {
                        if (dmg <= p.shieldHp) p.shieldHp -= dmg;
                        else { dmg -= p.shieldHp; p.shieldHp = 0; p.hp -= dmg; }
                    } else {
                        p.hp -= dmg;
                    }
                    if (p.hp <= 0) { p.hp = 0; p.gameOver = true; }
                    removed = true;
                    break;
                }
            }
        } else {
            // player bullet hits enemies
            for (auto& e : w.enemies) {
                if (dist(b.pos, e.pos) < b.r + e.r) {
                    e.hp -= 12; // flat damage for simplicity
                    if (e.hp <= 0) { e.hp = 0; w.score++; }
                    removed = true;
                    break;
                }
            }
        }

        if (removed) w.bullets.erase(w.bullets.begin() + i);
        else i++;
    }
}

void World::tick(int dtMs) {
    if (gameOver || !gameStarted) return;

    // update shooting for each player
    for (auto& p : players) {
        if (!p.connected || p.gameOver) continue;

        if (p.shootPressed && p.fireRate > 0) {
            // fire a bullet in aim direction
            Vec2 aim(p.aimX, p.aimY);
            float aLen = aim.len();
            if (aLen < 0.1f) aim = Vec2(1, 0);
            else aim = aim / aLen;

            for (int i = 0; i < p.bulletCount; i++) {
                float spread = (i - (p.bulletCount-1)/2.0f) * 0.12f;
                float a = atan2f(aim.y, aim.x) + spread;
                spawnBullet(*this, p.pos, Vec2(cosf(a)*p.bulletSpeed, sinf(a)*p.bulletSpeed),
                           4, p.bulletSpeed, false, p.damage);
            }
        }
    }

    // update enemies
    for (auto& e : enemies) {
        updateEnemyAI(*this, e, dtMs);
    }

    // update bullets + collisions
    updateBullets(*this, dtMs);

    // remove dead enemies
    for (size_t i = 0; i < enemies.size();) {
        if (enemies[i].hp <= 0) enemies.erase(enemies.begin() + i);
        else i++;
    }

    // wave logic
    updateWaveLogic(*this, dtMs);

    // contact damage
    // check all alive players
    for (auto& p : players) {
        if (!p.connected || p.gameOver) continue;
        for (auto& e : enemies) {
            if (dist(p.pos, e.pos) < 12 + e.r) {
                float dmg = e.damage * 0.3f;
                if (p.shieldHp > 0) {
                    if (dmg <= p.shieldHp) p.shieldHp -= dmg;
                    else { dmg -= p.shieldHp; p.shieldHp = 0; p.hp -= dmg; }
                } else p.hp -= dmg;
                if (p.hp <= 0) { p.hp = 0; p.gameOver = true; }
            }
        }
    }

    // check if all players are dead
    bool allDead = true;
    for (auto& p : players) {
        if (p.connected && !p.gameOver) allDead = false;
    }
    if (allDead && !players.empty()) gameOver = true;
}

void World::applyInput(uint8_t playerId, const InputPacket& input) {
    for (auto& p : players) {
        if (p.id != playerId) continue;
        if (input.seq <= p.lastInputSeq) return;
        p.lastInputSeq = input.seq;

        Vec2 move;
        if (input.left) move.x -= 1;
        if (input.right) move.x += 1;
        if (input.up) move.y -= 1;
        if (input.down) move.y += 1;

        float speed = 3.0f;
        float mlen = move.len();
        if (mlen > 0.1f) move = move / mlen * speed;
        p.pos.x = clamp(p.pos.x + move.x, 10, WORLD_W-10);
        p.pos.y = clamp(p.pos.y + move.y, 10, WORLD_H-10);

        if (fabs(input.aimX) > 0.1f || fabs(input.aimY) > 0.1f) {
            p.aimX = input.aimX;
            p.aimY = input.aimY;
        }
        p.shootPressed = input.shoot;
        p.dashPressed = input.dash;

        // upgrade choice
        if (input.upgradeChoice >= 0 && p.upgradeChoicePending) {
            p.upgradeChoicePending = false;
            // apply upgrade buffs
            p.bulletCount += 1;
            p.damage += 3;
        }

        // check level up (simplified: fixed XP threshold)
        if (p.xp >= p.xpNeeded) {
            p.xp = 0;
            p.level++;
            p.xpNeeded = (int)((p.xpNeeded == 0 ? 50 : p.xpNeeded) * 1.1f);
            p.upgradeChoicePending = true;
        }

        break;
    }
}

void World::buildSnapshot(SnapshotPacket& snap) {
    snap.seq++;
    snap.playerCount = 0;
    for (size_t i = 0; i < players.size() && i < 8; i++) {
        if (!players[i].connected) continue;
        auto& src = players[i];
        auto& dst = snap.players[snap.playerCount++];
        dst.id = src.id;
        dst.x = src.pos.x;
        dst.y = src.pos.y;
        dst.hp = src.hp;
        dst.maxHp = src.maxHp;
        dst.shieldHp = src.shieldHp;
        dst.shieldMax = src.shieldMax;
        dst.level = src.level;
        dst.xp = src.xp;
        dst.xpNeeded = src.xpNeeded;
        dst.stamina = src.stamina;
        dst.maxStamina = src.maxStamina;
        dst.bulletCount = src.bulletCount;
        dst.fireRate = src.fireRate;
    }

    snap.enemyCount = 0;
    for (size_t i = 0; i < enemies.size() && i < 256; i++) {
        auto& src = enemies[i];
        auto& dst = snap.enemies[snap.enemyCount++];
        dst.id = src.id;
        dst.type = (uint8_t)src.type;
        dst.x = src.pos.x;
        dst.y = src.pos.y;
        dst.hp = src.hp;
        dst.maxHp = src.maxHp;
        dst.shieldHp = src.shieldHp;
        dst.shieldMax = src.shieldMax;
        dst.r = src.r;
        switch (src.type) {
            case ENEMY_BALL: dst.r_=255; dst.g_=255; dst.b_=255; break;
            case ENEMY_TRIANGLE: dst.r_=100; dst.g_=200; dst.b_=255; break;
            case ENEMY_SQUARE: dst.r_=100; dst.g_=255; dst.b_=100; break;
            case ENEMY_SNIPER_COMMON: dst.r_=200; dst.g_=200; dst.b_=100; break;
            case ENEMY_RUNNER: dst.r_=255; dst.g_=100; dst.b_=100; break;
            case ENEMY_MINIBOSS_SNIPER: dst.r_=255; dst.g_=136; dst.b_=0; break;
            case ENEMY_MINIBOSS_BLINKER: dst.r_=200; dst.g_=80; dst.b_=255; break;
            case ENEMY_MINIBOSS_DASHER: dst.r_=255; dst.g_=68; dst.b_=0; break;
            case ENEMY_BOSS: dst.r_=122; dst.g_=0; dst.b_=48; break;
        }
    }

    snap.bulletCount = 0;
    for (size_t i = 0; i < bullets.size() && i < 512; i++) {
        auto& src = bullets[i];
        auto& dst = snap.bullets[snap.bulletCount++];
        dst.id = src.id;
        dst.x = src.pos.x;
        dst.y = src.pos.y;
        dst.r = src.r;
        dst.r_ = src.enemy ? 255 : 80;
        dst.g_ = src.enemy ? 100 : 200;
        dst.b_ = src.enemy ? 100 : 255;
        dst.enemy = src.enemy;
    }

    snap.waveNumber = wave.number;
    snap.totalWaves = TOTAL_WAVES;
    snap.score = score;
    snap.gameStarted = gameStarted;
    snap.gameOver = gameOver;
    snap.bossHp = 0;
    snap.bossMaxHp = 0;
    snap.bossPhaseIndex = boss.bossPhaseIndex;
    snap.minibossesLeft = MINIBOSS_COUNT - wave.minibossesDefeated;
    for (auto& e : enemies) {
        if (e.type == ENEMY_BOSS) {
            snap.bossHp = e.hp;
            snap.bossMaxHp = e.maxHp;
            break;
        }
    }
}
