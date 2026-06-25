// Compile: g++ edge_extrusion.cpp -o edge_extrusion -lraylib -lm -lpthread -ldl -lrt

#include "raylib.h"
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <string>
#include <algorithm>
#include <map>

const int SCREEN_W = 1280;
const int SCREEN_H = 720;
const int WORLD_W = 5000;
const int WORLD_H = 5000;
const int TOTAL_WAVES = 15;
const int MINIBOSS_COUNT = 3;
const int BH_COOLDOWN = 6000;
const int BH_DURATION = 2500;
const float BH_RADIUS = 220.0f;
const float BH_DAMAGE = 0.25f;
const int IDLE_LIMIT = 3000;
const int IDLE_WARNING_AT = 2000;
const float BOSS_MAX_HP = 400000.0f;
const float BOSS_DEBUFF_THRESHOLD = 100000.0f;
const char* BOSS_NAME = "The Ultimate Boss";

static inline float randf() { return (float)rand() / (float)RAND_MAX; }
static inline float randRange(float a, float b) { return a + randf() * (b - a); }
static inline float lerp(float a, float b, float t) { return a + (b-a)*t; }
static inline float clamp(float v, float mn, float mx) { return v<mn?mn:(v>mx?mx:v); }

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2(Vector2 v) : x(v.x), y(v.y) {}
    float len() const { return sqrtf(x*x + y*y); }
    Vec2 normalized() const { float l=len(); return l>0 ? Vec2(x/l,y/l) : Vec2(0,0); }
    Vec2 operator+(const Vec2& o) const { return Vec2(x+o.x, y+o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x-o.x, y-o.y); }
    Vec2 operator*(float s) const { return Vec2(x*s, y*s); }
};

static float angleBetween(Vec2 a, Vec2 b) {
    return atan2f(b.y-a.y, b.x-a.x);
}
static float dist(Vec2 a, Vec2 b) {
    return sqrtf((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y));
}

static Color hexToCol(unsigned int h) {
    return Color{(unsigned char)((h>>16)&0xFF),(unsigned char)((h>>8)&0xFF),(unsigned char)(h&0xFF),255};
}
static const Color COL_BG       = hexToCol(0x181b22);
static const Color COL_GRID     = hexToCol(0xffffff);
static const Color COL_GOLD     = hexToCol(0xffd24d);
static const Color COL_HP       = hexToCol(0xff3b3b);
static const Color COL_SHIELD   = hexToCol(0x4dc8ff);
static const Color COL_STAMINA  = hexToCol(0xffcc00);
static const Color COL_BULLET   = hexToCol(0xffd700);
static const Color COL_HOMING   = hexToCol(0xffd24d);
static const Color COL_EXPLOSIVE= hexToCol(0x7a5aff);

enum EnemyType {
    ENEMY_BALL, ENEMY_TRIANGLE, ENEMY_SQUARE, ENEMY_SNIPER_COMMON,
    ENEMY_RUNNER, ENEMY_MINIBOSS_SNIPER, ENEMY_MINIBOSS_BLINKER,
    ENEMY_MINIBOSS_DASHER, ENEMY_BOSS
};
enum AbilityID {
    ABILITY_NONE=-1, ABILITY_BULLMASTER, ABILITY_WINDRUNNER,
    ABILITY_PIERCING, ABILITY_BLACKHOLE, ABILITY_OVERDRIVE, ABILITY_LASER
};
enum GamePhase { PHASE_MENU, PHASE_LOBBY, PHASE_GAME, PHASE_UPGRADE, PHASE_GAMEOVER };
enum WavePhase { WAVE_NORMAL, WAVE_MINIBOSS, WAVE_BOSS_INTRO, WAVE_BOSS, WAVE_CLEARED };
enum GraphicsMode { GRAPHICS_MAX, GRAPHICS_LOW };
enum ControlMode { CONTROL_WASD, CONTROL_MOUSE };

struct Particle {
    Vec2 pos, vel;
    int life;
    Color color;
};

struct Bullet {
    Vec2 pos, vel;
    float r, speedMag, damage;
    bool homing, piercing, tethered;
    int piercingHits, piercingMaxHits;
    Vec2 trail[6];
    int trailCount;
    bool active;
    
    Bullet() : r(5), speedMag(0), damage(1), homing(false), piercing(false),
               tethered(false), piercingHits(0), piercingMaxHits(2), active(true), trailCount(0) {}
};

struct Minion {
    float angle, orbitDist, orbitSpeed;
    int shootTimer, shootCooldown;
    float damage, shootRange;
    Color color;
};

struct Bomb {
    Vec2 pos;
    float radius, maxRadius, life;
    bool exploded;
    int timer, fuseTime;
    bool isMini;
};

struct BlackHole {
    Vec2 pos;
    int timer;
    bool active;
};

struct Shockwave {
    Vec2 pos;
    float radius, maxRadius, life;
    Color color;
};

struct Obstacle {
    float x, y, w, h;
    float hp, maxHp;
};

struct UniqueElement {
    Vec2 pos;
    float r;
    int type; // 0=spikes,1=poison,2=lava
    Color color;
};

struct Biome {
    const char* name;
    Color bg, grid, accent, obstacleColor, obstacleEdge, decorColor;
    int uniqueType;
    Color uniqueColor;
    int uniqueCount;
};

Biome biomes[3] = {
    {"Planicie Cinzenta", hexToCol(0x181b22), {255,255,255,9}, hexToCol(0x3a4a6a),
     hexToCol(0x2e3548), hexToCol(0x5a6a9a), hexToCol(0x788acc), 0, hexToCol(0xaa5555), 5},
    {"Pantano Venenoso", hexToCol(0x10201a), {140,255,170,10}, hexToCol(0x1f5c3f),
     hexToCol(0x16331f), hexToCol(0x3fae6e), hexToCol(0x5adc8c), 1, hexToCol(0x66ff66), 6},
    {"Caverna Carmesim", hexToCol(0x220e10), {255,100,100,11}, hexToCol(0x5c1f24),
     hexToCol(0x3a1518), hexToCol(0xb03a3a), hexToCol(0xff5a5a), 2, hexToCol(0xff4400), 4}
};

struct Upgrade {
    const char* name;
    const char* desc;
    void (*apply)();
};

static float playerMaxHp=100, playerHp=100, playerDamage=15;
static float playerSpeed=4, playerBaseSpeed=4;
static int playerFireRate=400, playerBullets=1;
static float playerBulletSpeed=8.8f;
static float playerRegen=0, playerShieldMax=0, playerShieldHp=0;
static int playerLevel=1, playerXp=0, playerXpNeeded=0;
static int playerScore=0;
static bool playerDashing=false, playerDashCD=false;
static int playerDashTimer=0, playerDashDuration=150, playerDashCooldown=0, playerDashCooldownTime=1500;
static float playerStamina=100, playerMaxStamina=100, playerStaminaRegen=0.15f;
static float playerDashStaminaCost=30;
static bool overdriveActive=false; static int overdriveTimer=0;
static int overdriveDuration=2000, overdriveCooldown=20000, overdriveCooldownTimer=0;
static bool speedBoostActive=false;
static int speedBoostTimer=0, speedBoostDuration=2000, speedBoostCooldown=8000, speedBoostCooldownTimer=0;
static bool piercingNext=false;
static int shotCount=0, bhTimer=0, fireTimer=0;
static bool laserActive=false;
static int laserTimer=0, laserDuration=3000, laserCooldown=5000, laserCooldownTimer=0;
static float laserDamage=100, laserWidth=4;
static bool atropelamento=false;
static Vec2 playerPos(WORLD_W/2, WORLD_H/2);
static float playerAngle=0;
static int playerR=20;
static int idleTimer=0;
static float screenShakeTime=0, screenShakeMag=0;
static int damageFlashTimer=0;
static int playerContactCooldown=0;

static std::vector<Bullet> bullets;
static std::vector<Bullet> enemyBullets;
static std::vector<Particle> particles;
static std::vector<Shockwave> shockwaves;
static std::vector<Bomb> bombs;
static std::vector<BlackHole> blackholes;
static std::vector<Minion> minions;
static Vec2 playerTrail[20];
static int playerTrailCount = 0;

static GraphicsMode graphicsMode = GRAPHICS_MAX;
static ControlMode controlMode = CONTROL_WASD;

static Sound soundShoot, soundHit, soundEnemyDie, soundPlayerHit, soundLevelUp;
static Sound soundDash, soundWaveStart, soundExplosion, soundSniperFire;
static std::map<std::string, Sound> sounds;

static Wave generateSineWave(float freq, float duration, float volume) {
    int sampleRate = 44100;
    int samples = (int)(sampleRate * duration);
    short* data = (short*)malloc(samples * sizeof(short));
    for (int i=0; i<samples; i++) {
        float t = (float)i / sampleRate;
        float val = sinf(2*M_PI*freq*t) * volume * 32767;
        data[i] = (short)clamp(val, -32767, 32767);
    }
    Wave w = {0};
    w.data = data;
    w.frameCount = samples;
    w.sampleRate = sampleRate;
    w.sampleSize = 16;
    w.channels = 1;
    return w;
}

static Sound makeSound(float freq, float dur, float vol, float sweep) {
    int sr = 44100;
    int samples = (int)(sr*dur);
    short* data = (short*)malloc(samples*sizeof(short));
    for (int i=0; i<samples; i++) {
        float t = (float)i/sr;
        float f = freq + sweep*t/dur;
        float val = sinf(2*M_PI*f*t) * vol * 32767 * (1.0f - t/dur);
        data[i] = (short)clamp(val,-32767,32767);
    }
    Wave w = {0};
    w.data = data; w.frameCount = samples;
    w.sampleRate = sr; w.sampleSize = 16; w.channels = 1;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static void initSounds() {
    sounds["shoot"] = makeSound(620,0.06f,0.18f,300);
    sounds["hit"] = makeSound(180,0.09f,0.25f,80);
    sounds["enemyDie"] = makeSound(260,0.14f,0.22f,60);
    sounds["playerHit"] = makeSound(90,0.18f,0.35f,40);
    sounds["levelUp"] = makeSound(440,0.4f,0.35f,220);
    sounds["dash"] = makeSound(350,0.15f,0.28f,200);
    sounds["waveStart"] = makeSound(330,0.5f,0.35f,660);
    sounds["explosion"] = makeSound(55,0.6f,0.55f,20);
    sounds["sniperFire"] = makeSound(950,0.12f,0.3f,-800);
}

static void playSoundName(const char* name) {
    if (sounds.count(name)) PlaySound(sounds[name]);
}

static void spawnParticle(Vec2 pos, Color col, int life=40) {
    if (graphicsMode==GRAPHICS_LOW && particles.size()>60) return;
    Particle p;
    p.pos = pos;
    p.vel = Vec2(randRange(-3,3), randRange(-3,3));
    p.life = life;
    p.color = col;
    particles.push_back(p);
}

static void spawnParticles(Vec2 pos, Color col, int count=8, int life=40) {
    if (graphicsMode==GRAPHICS_LOW) count = std::min(count, 4);
    for (int i=0; i<count; i++) {
        Particle p;
        p.pos = pos;
        p.vel = Vec2(randRange(-3,3), randRange(-3,3));
        p.life = life + rand()%20;
        p.color = col;
        particles.push_back(p);
    }
}

static bool circleRect(Vec2 c, float cr, float rx, float ry, float rw, float rh) {
    float nx = std::max(rx, std::min(c.x, rx+rw));
    float ny = std::max(ry, std::min(c.y, ry+rh));
    float dx = c.x-nx, dy = c.y-ny;
    return (dx*dx+dy*dy) < (cr*cr);
}

static int upgradeCycleTimer = 0;
static bool upgradeCycleFinished = false;
static int upgradeDisplay1 = -1, upgradeDisplay2 = -1, upgradeDisplay3 = -1;

static Vec2 camera(0,0);
static void updateCamera() {
    camera.x = playerPos.x - SCREEN_W/2;
    camera.y = playerPos.y - SCREEN_H/2;
    camera.x = clamp(camera.x, 0, WORLD_W-SCREEN_W);
    camera.y = clamp(camera.y, 0, WORLD_H-SCREEN_H);
}
static float toScreenX(float wx) { return wx - camera.x; }
static float toScreenY(float wy) { return wy - camera.y; }

static AbilityID chosenAbility = ABILITY_NONE;
static int currentBiomeIndex = 0;
static std::vector<Obstacle> obstacles;
static std::vector<Vec2> decorations;
static std::vector<UniqueElement> uniqueElements;
static std::vector<class Enemy*> enemies;
static class Boss* bossPtr = nullptr;
static int score = 0;

struct WaveState {
    int number = 0;
    WavePhase phase = WAVE_NORMAL;
    int minibossesDefeated = 0;
    int enemiesToSpawn = 0;
    int spawnTimer = 0;
    int spawnInterval = 1000;
    bool waitingNextWave = false;
    const char* announcement = "";
    int announcementTimer = 0;
} wave;

static bool devModeUnlocked = false;
static int devWaveNumber = 1;
static bool devBossEnabled = true;
static int devMinionCount = 0;
static bool devDebugBoss = false;
static int devDebugTarget = -1; // -1=none, 0=miniboss1, 1=miniboss2, 2=miniboss3, 3=boss
static char menuInput[32] = "";
static int menuInputLen = 0;

static bool gameStarted = false, paused = false;
static GamePhase gamePhase = PHASE_MENU;

class Enemy {
public:
    Vec2 pos;
    float r, maxHp, hp, speed, damage;
    EnemyType type;
    const char* name;
    float angle, pulsePhase;
    int flashTime;
    
    int shootCooldown, shootTimer;
    float preferredDist;
    float shieldRadius, shieldAmount;
    float shieldHp, shieldHpMax;
    Enemy* shieldLink;
    bool unbreakableShield;
    Enemy* shieldProducer;
    bool chargePhase;
    int chargeTimer, chargeTime;
    bool dashing, dashWarning;
    int dashTimer, dashCooldown, dashDuration;
    float dashDx, dashDy, dashWarningTimer;
    
    int blinkTimer, blinkCooldown;
    const char* blinkStage;
    float laserAngle;
    int chargeDuration;

    Enemy(Vec2 p, EnemyType t) : pos(p), type(t), angle(0), pulsePhase(randf()*M_PI*2),
        flashTime(0), name(""), shieldLink(nullptr), unbreakableShield(false), shieldProducer(nullptr),
        chargePhase(false), chargeTimer(0), chargeTime(0), dashing(false), dashWarning(false),
        dashTimer(0), dashCooldown(0), dashDuration(0), dashDx(0), dashDy(0), dashWarningTimer(0),
        blinkTimer(0), blinkCooldown(0), blinkStage("moving"), laserAngle(0), chargeDuration(0),
        shootCooldown(0), shootTimer(0), preferredDist(0), shieldRadius(0), shieldAmount(0),
        shieldHp(0), shieldHpMax(0) {

        float waveScale = 1 + (wave.number-1)*0.18f + score*0.0004f;
        float waveSpeedMul = 1 + (wave.number-1)*0.08f;
        
        switch(type) {
            case ENEMY_BALL:
                r=16; maxHp=28*waveScale; speed=(1+randf()*0.6f)*waveSpeedMul; damage=8;
                shootCooldown=1400+rand()%600; shootTimer=shootCooldown*randf(); preferredDist=260;
                break;
            case ENEMY_TRIANGLE:
                r=14; maxHp=20*waveScale; speed=(2.6f+randf()*0.8f)*1.5f*waveSpeedMul; damage=14;
                break;
            case ENEMY_SQUARE:
                r=18; maxHp=40*waveScale; speed=(0.9f+randf()*0.4f)*waveSpeedMul; damage=6;
                shieldRadius=140; shieldAmount=0.5f;
                break;
            case ENEMY_SNIPER_COMMON:
                r=16; maxHp=22*waveScale; speed=0.8f*waveSpeedMul; damage=30;
                shootCooldown=1800+rand()%400; shootTimer=shootCooldown*randf();
                break;
            case ENEMY_RUNNER:
                r=14; maxHp=18*waveScale; speed=(2.0f+randf()*0.5f)*waveSpeedMul; damage=25;
                dashCooldown=2200+rand()%600; dashTimer=dashCooldown*randf();
                break;
            case ENEMY_MINIBOSS_SNIPER:
                r=38; maxHp=1500*waveScale; speed=0.6f; damage=55; name="ATIRADOR DE ELITE";
                shootCooldown=300; shootTimer=0;
                shieldHpMax=600*waveScale; shieldHp=shieldHpMax;
                chargeTime=2200; chargeTimer=0;
                break;
            case ENEMY_MINIBOSS_BLINKER:
                r=36; maxHp=1300*waveScale; speed=1.4f; damage=20; name="DISRUPTOR FANTASMA";
                shootCooldown=300; shootTimer=0;
                shieldHpMax=500*waveScale; shieldHp=shieldHpMax;
                blinkCooldown=3500; blinkTimer=0;
                break;
            case ENEMY_MINIBOSS_DASHER:
                r=32; maxHp=1200*waveScale; speed=2.8f; damage=38; name="FURIA VELOZ";
                shootCooldown=300; shootTimer=0;
                shieldHpMax=400*waveScale; shieldHp=shieldHpMax;
                dashCooldown=2000; dashTimer=0;
                break;
            default: break;
        }
        hp = maxHp;
    }
    
    virtual ~Enemy() {}
    
    bool isBoss() const { return type==ENEMY_BOSS; }
    
    void takeDamage(float dmg) {
        if (unbreakableShield && shieldProducer && shieldProducer->hp>0) {
            flashTime=4; return;
        }
        float finalDmg = dmg;
        for (auto& e : enemies) {
            if (e!=this && e->type==ENEMY_SQUARE) {
                float d = dist(e->pos, pos);
                if (d < e->shieldRadius) finalDmg *= (1-e->shieldAmount);
            }
        }
        if (shieldHp > 0) {
            if (finalDmg <= shieldHp) {
                shieldHp -= finalDmg;
                finalDmg = 0;
            } else {
                finalDmg -= shieldHp;
                shieldHp = 0;
            }
        }
        hp -= finalDmg;
        flashTime = 8;
    }
    
    virtual void update(int dt) {
        angle += 0.05f;
        pulsePhase += 0.06f;
        if (flashTime>0) flashTime--;
        
        Vec2 toPlayer = playerPos - pos;
        float d = toPlayer.len();
        if (d<1) d=1;
        
        float moveX=0, moveY=0;
        
        switch(type) {
            case ENEMY_BALL: {
                if (d>preferredDist) { moveX=toPlayer.x/d*speed; moveY=toPlayer.y/d*speed; }
                else if (d<preferredDist-40) { moveX=-toPlayer.x/d*speed; moveY=-toPlayer.y/d*speed; }
                shootTimer += dt;
                if (shootTimer >= shootCooldown) {
                    shootTimer = 0;
                    float a = atan2f(toPlayer.y, toPlayer.x);
                    Bullet b;
                    b.pos = pos; b.r=6;
                    b.vel = Vec2(cosf(a)*5, sinf(a)*5);
                    b.speedMag=5; b.damage=this->damage;
                    enemyBullets.push_back(b);
                }
                break;
            }
            case ENEMY_TRIANGLE: case ENEMY_SQUARE: {
                moveX = toPlayer.x/d*speed;
                moveY = toPlayer.y/d*speed;
                break;
            }
            case ENEMY_SNIPER_COMMON: {
                moveX = toPlayer.x/d*speed; moveY = toPlayer.y/d*speed;
                shootTimer += dt;
                if (shootTimer >= shootCooldown) {
                    shootTimer = 0;
                    float a = atan2f(toPlayer.y, toPlayer.x);
                    Bullet b;
                    b.pos = pos; b.r=6;
                    b.vel = Vec2(cosf(a)*16, sinf(a)*16);
                    b.speedMag=16; b.damage=this->damage;
                    enemyBullets.push_back(b);
                    spawnParticles(pos, {255,136,0,255}, 4, 20);
                    playSoundName("sniperFire");
                }
                break;
            }
            case ENEMY_RUNNER: {
                if (!dashing) {
                    moveX = toPlayer.x/d*speed; moveY = toPlayer.y/d*speed;
                    dashTimer += dt;
                    if (dashTimer >= dashCooldown && d<300) {
                        dashTimer = 0;
                        dashWarning = true;
                        dashWarningTimer = 0;
                        dashDx = toPlayer.x/d; dashDy = toPlayer.y/d;
                    }
                }
                if (dashWarning) {
                    dashWarningTimer += dt;
                    if (dashWarningTimer >= 300) { dashWarning=false; dashing=true; dashDuration=200; }
                }
                if (dashing) {
                    moveX = dashDx*speed*10; moveY = dashDy*speed*10;
                    dashDuration -= dt;
                    if (dashDuration<=0) dashing=false;
                    if (randf()<0.5f) spawnParticle(pos, {255,102,0,255}, 25);
                    if (d < playerR+r+15) { /* contact damage */ }
                }
                break;
            }
            case ENEMY_MINIBOSS_SNIPER: {
                shootTimer += dt;
                if (shootTimer >= shootCooldown) {
                    shootTimer = 0;
                    float baseAngle = atan2f(toPlayer.y, toPlayer.x);
                    for (int i = 0; i < 3; i++) {
                        float spread = (i - 1) * 0.12f;
                        float a = baseAngle + spread;
                        Bullet b;
                        b.pos = pos; b.r = 5;
                        b.vel = Vec2(cosf(a) * 7, sinf(a) * 7);
                        b.speedMag = 7;
                        b.damage = this->damage;
                        enemyBullets.push_back(b);
                    }
                    spawnParticles(pos, {255,136,0,255}, 3, 15);
                }
                if (!chargePhase) {
                    moveX = toPlayer.x/d*speed; moveY = toPlayer.y/d*speed;
                    chargeTimer += dt;
                    laserAngle = atan2f(toPlayer.y, toPlayer.x);
                    if (chargeTimer >= chargeTime) {
                        float a = laserAngle;
                        Bullet b;
                        b.pos = pos; b.r=8;
                        b.vel = Vec2(cosf(a)*12, sinf(a)*12);
                        b.speedMag=12; b.damage=this->damage;
                        enemyBullets.push_back(b);
                        spawnParticles(pos, {255,136,0,255}, 5, 25);
                        playSoundName("sniperFire");
                        chargeTimer = 0;
                    }
                }
                break;
            }
            case ENEMY_MINIBOSS_BLINKER: {
                shootTimer += dt;
                if (shootTimer >= shootCooldown) {
                    shootTimer = 0;
                    float baseAngle = atan2f(toPlayer.y, toPlayer.x);
                    for (int i = 0; i < 3; i++) {
                        float spread = (i - 1) * 0.12f;
                        float a = baseAngle + spread;
                        Bullet b;
                        b.pos = pos; b.r = 5;
                        b.vel = Vec2(cosf(a) * 7, sinf(a) * 7);
                        b.speedMag = 7;
                        b.damage = this->damage;
                        enemyBullets.push_back(b);
                    }
                    spawnParticles(pos, {200,80,255,255}, 3, 15);
                }
                if (strcmp(blinkStage,"moving")==0) {
                    moveX = toPlayer.x/d*speed; moveY = toPlayer.y/d*speed;
                    blinkTimer += dt;
                    if (blinkTimer >= blinkCooldown) {
                        blinkTimer = 0;
                        blinkStage = "warning";
                        Bomb bm;
                        bm.pos = pos; bm.radius=10; bm.maxRadius=200; bm.life=1;
                        bm.exploded=false; bm.timer=0; bm.fuseTime=900; bm.isMini=true;
                        bombs.push_back(bm);
                    }
                } else if (strcmp(blinkStage,"warning")==0) {
                    blinkTimer += dt;
                    if (blinkTimer >= 400) {
                        blinkTimer = 0;
                        float ba = randf()*M_PI*2;
                        pos.x = playerPos.x + cosf(ba)*(70+randf()*80);
                        pos.y = playerPos.y + sinf(ba)*(70+randf()*80);
                        pos.x = clamp(pos.x,60,WORLD_W-60);
                        pos.y = clamp(pos.y,60,WORLD_H-60);
                        blinkStage = "moving";
                        spawnParticles(pos, {200,80,255,255}, 12, 30);
                    }
                }
                break;
            }
            case ENEMY_MINIBOSS_DASHER: {
                shootTimer += dt;
                if (shootTimer >= shootCooldown) {
                    shootTimer = 0;
                    float baseAngle = atan2f(toPlayer.y, toPlayer.x);
                    for (int i = 0; i < 3; i++) {
                        float spread = (i - 1) * 0.12f;
                        float a = baseAngle + spread;
                        Bullet b;
                        b.pos = pos; b.r = 5;
                        b.vel = Vec2(cosf(a) * 7, sinf(a) * 7);
                        b.speedMag = 7;
                        b.damage = this->damage;
                        enemyBullets.push_back(b);
                    }
                    spawnParticles(pos, {255,102,0,255}, 3, 15);
                }
                if (!dashing) {
                    moveX = toPlayer.x/d*speed; moveY = toPlayer.y/d*speed;
                    dashTimer += dt;
                    if (dashTimer >= dashCooldown && d<350) {
                        dashTimer = 0; dashing=true;
                        dashDx = toPlayer.x/d; dashDy = toPlayer.y/d;
                        dashDuration = 280;
                    }
                } else {
                    moveX = dashDx*speed*8; moveY = dashDy*speed*8;
                    dashDuration -= dt;
                    if (dashDuration<=0) dashing=false;
                    if (randf()<0.6f) spawnParticle(pos, {255,102,0,255}, 20);
                    if (d < playerR+r+10) { /* contact damage */ }
                }
                break;
            }
            default: break;
        }
        
        if (type!=ENEMY_SQUARE) {
            float slowFactor=1;
            for (auto& other : enemies) {
                if (other!=this && other->type==ENEMY_SQUARE && other->hp>0) {
                    float ds = dist(other->pos, pos);
                    if (ds < other->shieldRadius) slowFactor=0.5f;
                }
            }
            moveX*=slowFactor; moveY*=slowFactor;
        }
        
        pos.x += moveX; pos.y += moveY;
        pos.x = clamp(pos.x,0,WORLD_W); pos.y = clamp(pos.y,0,WORLD_H);
        
        if (d < playerR+r) { /* contact damage handled in main */ }
    }
    
    virtual void draw() {
        float sx = toScreenX(pos.x), sy = toScreenY(pos.y);
        if (sx<-100||sx>SCREEN_W+100||sy<-100||sy>SCREEN_H+100) return;
        
        Color color;
        switch(type) {
            case ENEMY_BALL: color={255,80,80,255}; break;
            case ENEMY_TRIANGLE: color={255,174,66,255}; break;
            case ENEMY_SQUARE: color={126,214,255,255}; break;
            case ENEMY_SNIPER_COMMON: color={255,136,0,255}; break;
            case ENEMY_RUNNER: color={255,102,0,255}; break;
            case ENEMY_MINIBOSS_SNIPER: color={255,136,0,255}; break;
            case ENEMY_MINIBOSS_BLINKER: color={200,80,255,255}; break;
            case ENEMY_MINIBOSS_DASHER: color={255,68,0,255}; break;
            default: color={200,80,255,255};
        }
        if (flashTime>0) color = WHITE;
        
        if (graphicsMode!=GRAPHICS_LOW) {
            Color glow = color; glow.a = 80;
            DrawCircle(sx, sy, r+6, glow);
        }
        
        DrawCircle(sx, sy, r, color);
        
        float bw = r*2, bh = 5;
        if (shieldHpMax > 0) {
            DrawRectangle(sx-r, sy-r-20, bw, 4, {20,40,80,255});
            DrawRectangle(sx-r, sy-r-20, bw*(shieldHp/shieldHpMax), 4, COL_SHIELD);
        }
        DrawRectangle(sx-r, sy-r-14, bw, bh, {58,10,10,255});
        DrawRectangle(sx-r, sy-r-14, bw*(hp/maxHp), bh, {77,255,122,255});
        
        if (name) {
            DrawText(name, sx-MeasureText(name,13)/2, sy-r-30, 13, WHITE);
        }
    }
};

enum BossPhase { BP_IDLE, BP_COOLDOWN, BP_LASER360, BP_SUPERLASER, BP_TELEPORTBOMB, BP_QUANTUMLASER };
struct BossState {
    Enemy* obj = nullptr;
    BossPhase phase = BP_IDLE;
    int phaseTimer = 0, attackCooldown = 0;
    int bossPhaseIndex = 0;
    std::vector<int> availableAbilities;
    int abilityCycleIndex = 0;
    int shockwaveTimer = 0;
    float damageAccumulated = 0;
    int weaknessWindow = 0;
    float weaknessMultiplier = 1;
    struct { int timer=0; float angle=0; bool charging=true; int chargeDuration=550, fireDuration=900; } superLaser;
    struct { int timer=0; const char* stage="none"; float targetX=0, targetY=0; } teleport;
    struct { int timer=0; const char* phase="forming"; int duration=700, fireDuration=300; } quantumLaser;
} boss;

class Boss : public Enemy {
public:
    Boss(Vec2 p) : Enemy(p, ENEMY_BOSS) {
        r = 70;
        maxHp = BOSS_MAX_HP;
        hp = maxHp;
        speed = 1.8f;
        damage = 22;
    }
    
    void takeDamage(float dmg) {
        float finalDmg = dmg * (boss.weaknessWindow>0 ? boss.weaknessMultiplier : 1);
        hp -= finalDmg;
        flashTime = 8;
        boss.damageAccumulated += finalDmg;
        if (boss.damageAccumulated >= BOSS_DEBUFF_THRESHOLD) {
            boss.damageAccumulated -= BOSS_DEBUFF_THRESHOLD;
        }
        float hpPct = hp/maxHp;
        int targetIdx = 0;
        if (hpPct<=0.25f) targetIdx=3;
        else if (hpPct<=0.50f) targetIdx=2;
        else if (hpPct<=0.75f) targetIdx=1;
        if (targetIdx > boss.bossPhaseIndex) {
            boss.bossPhaseIndex = targetIdx;
            boss.weaknessWindow = 2500;
            boss.weaknessMultiplier = 1.8f;
        }
    }
    
    void update(int dt) override {
        angle += 0.015f;
        if (flashTime>0) flashTime--;
        Vec2 toPlayer = playerPos - pos;
        float d = toPlayer.len();
        if (d<1) d=1;
        if (d>200 && boss.phase != BP_TELEPORTBOMB) {
            pos.x += toPlayer.x/d*speed;
            pos.y += toPlayer.y/d*speed;
        }

        float hpPct = hp / maxHp;
        int maxIdx = hpPct <= 0.25f ? 3 : hpPct <= 0.50f ? 2 : hpPct <= 0.75f ? 1 : 0;
        float cdMul = 1.0f - maxIdx * 0.15f;

        boss.phaseTimer += dt;

        if (boss.phase == BP_COOLDOWN) {
            boss.attackCooldown -= dt;
            if (boss.attackCooldown <= 0) {
                int phases[] = {BP_LASER360, BP_SUPERLASER, BP_TELEPORTBOMB, BP_QUANTUMLASER};
                boss.phase = (BossPhase)phases[rand() % 4];
                boss.phaseTimer = 0;
                if (boss.phase == BP_SUPERLASER) {
                    boss.superLaser.angle = atan2f(toPlayer.y, toPlayer.x);
                    boss.superLaser.charging = true;
                    boss.superLaser.timer = 0;
                }
                if (boss.phase == BP_TELEPORTBOMB) {
                    boss.teleport.stage = "warning";
                    boss.teleport.timer = 0;
                }
                if (boss.phase == BP_QUANTUMLASER) {
                    boss.quantumLaser.phase = "forming";
                    boss.quantumLaser.timer = 0;
                }
            }
        } else if (boss.phase == BP_LASER360) {
            float baseAngle = boss.phaseTimer * 0.002f;
            if (boss.phaseTimer % 200 < 20) {
                for (int i = 0; i < 12; i++) {
                    float a = baseAngle + i * M_PI * 2 / 12;
                    Bullet b;
                    b.pos = pos; b.r = 6;
                    b.vel = Vec2(cosf(a) * 6, sinf(a) * 6);
                    b.speedMag = 6;
                    b.damage = damage * 0.5f;
                    enemyBullets.push_back(b);
                }
            }
            if (boss.phaseTimer > 1200) {
                boss.phase = BP_COOLDOWN;
                boss.attackCooldown = (int)(1500 * cdMul);
            }
        } else if (boss.phase == BP_SUPERLASER) {
            boss.superLaser.timer += dt;
            if (boss.superLaser.charging) {
                float flash = sinf(boss.superLaser.timer * 0.02f);
                if (flash > 0) spawnParticle(pos, {255,255,0,255}, 1);
                if (boss.superLaser.timer >= boss.superLaser.chargeDuration) {
                    boss.superLaser.charging = false;
                    boss.superLaser.timer = 0;
                }
            } else {
                if (randf() < 0.3f) {
                    float spread = (randf() - 0.5f) * 0.15f;
                    float a = boss.superLaser.angle + spread;
                    Bullet b;
                    b.pos = pos + Vec2(cosf(boss.superLaser.angle), sinf(boss.superLaser.angle)) * 50;
                    b.r = 7;
                    b.vel = Vec2(cosf(a) * 10, sinf(a) * 10);
                    b.speedMag = 10;
                    b.damage = damage;
                    enemyBullets.push_back(b);
                }
                if (boss.superLaser.timer >= boss.superLaser.fireDuration) {
                    boss.phase = BP_COOLDOWN;
                    boss.attackCooldown = (int)(1800 * cdMul);
                }
            }
        } else if (boss.phase == BP_TELEPORTBOMB) {
            boss.teleport.timer += dt;
            if (boss.teleport.stage == "warning") {
                if (boss.teleport.timer > 600) {
                    boss.teleport.stage = "teleporting";
                    boss.teleport.timer = 0;
                    float a = randf() * M_PI * 2;
                    float dist = 150 + randf() * 200;
                    boss.teleport.targetX = clamp(playerPos.x + cosf(a) * dist, 100, WORLD_W - 100);
                    boss.teleport.targetY = clamp(playerPos.y + sinf(a) * dist, 100, WORLD_H - 100);
                    for (int i = 0; i < 3; i++) {
                        Bomb bm;
                        bm.pos = Vec2(boss.teleport.targetX, boss.teleport.targetY);
                        bm.radius = 10; bm.maxRadius = 150; bm.life = 1;
                        bm.exploded = false; bm.timer = 0; bm.fuseTime = 500 + i * 200;
                        bm.isMini = true;
                        bombs.push_back(bm);
                    }
                }
            } else if (boss.teleport.stage == "teleporting") {
                if (boss.teleport.timer > 200) {
                    pos.x = boss.teleport.targetX;
                    pos.y = boss.teleport.targetY;
                    spawnParticles(pos, {255,77,255,255}, 15, 40);
                    boss.teleport.stage = "none";
                    boss.phase = BP_COOLDOWN;
                    boss.attackCooldown = (int)(1200 * cdMul);
                }
            }
        } else if (boss.phase == BP_QUANTUMLASER) {
            boss.quantumLaser.timer += dt;
            if (strcmp(boss.quantumLaser.phase, "forming") == 0) {
                if (boss.quantumLaser.timer >= boss.quantumLaser.duration) {
                    boss.quantumLaser.phase = "firing";
                    boss.quantumLaser.timer = 0;
                }
            } else if (strcmp(boss.quantumLaser.phase, "firing") == 0) {
                if (boss.quantumLaser.timer % 100 < 15) {
                    float baseA = atan2f(toPlayer.y, toPlayer.x);
                    for (int i = 0; i < 5; i++) {
                        float spread = (i - 2) * 0.1f;
                        float a = baseA + spread + (randf() - 0.5f) * 0.08f;
                        Bullet b;
                        b.pos = pos;
                        b.r = 5;
                        b.vel = Vec2(cosf(a) * 8, sinf(a) * 8);
                        b.speedMag = 8;
                        b.damage = damage * 0.4f;
                        enemyBullets.push_back(b);
                    }
                }
                if (boss.quantumLaser.timer >= boss.quantumLaser.fireDuration) {
                    boss.phase = BP_COOLDOWN;
                    boss.attackCooldown = (int)(2000 * cdMul);
                    boss.quantumLaser.phase = "forming";
                }
            }
        }
    }
    
    void draw() override {
        if (boss.teleport.stage=="teleporting") return;
        float sx = toScreenX(pos.x), sy = toScreenY(pos.y);
        float heartbeat = 0.85f + sinf(GetTime()*0.008f)*0.15f;
        Color col = flashTime>0 ? WHITE : Color{122,0,48,255};
        float pulseRad = r*heartbeat;
        
        DrawCircle(sx, sy, pulseRad, col);
        float eyeSize = r*0.35f*heartbeat;
        DrawCircle(sx, sy, eyeSize, {255,34,102,255});
        DrawCircle(sx-4, sy-4, eyeSize*0.35f, {255,255,255,128});
        
        for (int i=0; i<4; i++) {
            Color c = i<=boss.bossPhaseIndex ? (Color){255,77,255,255} : (Color){255,255,255,38};
            DrawRing({sx,sy}, r+10, r+14, 90*i+5, 90*(i+1)-5, 0, c);
        }
        float bw = 200, bh = 12;
        DrawRectangle(sx-bw/2, sy-r-20, bw, bh, {20,20,20,255});
        DrawRectangle(sx-bw/2, sy-r-20, bw*(hp/maxHp), bh, {169,0,255,255});
    }
};

static int biomeForWave(int waveNum) {
    return std::min(2, (waveNum-1)/5);
}

static void generateObstacles(int biomeIdx) {
    obstacles.clear(); decorations.clear(); uniqueElements.clear();
    const Biome& b = biomes[biomeIdx];
    int count = 7 + biomeIdx*2;
    for (int i=0; i<count; i++) {
        float w = 80+randf()*160, h = 80+randf()*160;
        float ox = 60+randf()*(WORLD_W-120-w);
        float oy = 60+randf()*(WORLD_H-120-h);
        if (hypotf(ox+w/2-WORLD_W/2, oy+h/2-WORLD_H/2) < 260) continue;
        obstacles.push_back({ox,oy,w,h,100,100});
    }
    for (int i=0; i<b.uniqueCount; i++) {
        float ur = 20+randf()*40;
        float ux = 100+randf()*(WORLD_W-200);
        float uy = 100+randf()*(WORLD_H-200);
        if (hypotf(ux-WORLD_W/2, uy-WORLD_H/2) < 300) continue;
        uniqueElements.push_back({Vec2(ux,uy),ur,b.uniqueType,b.uniqueColor});
    }
    int decorCount = 40+biomeIdx*15;
    if (graphicsMode==GRAPHICS_LOW) decorCount /= 3;
    for (int i=0; i<decorCount; i++) {
        decorations.push_back(Vec2(randf()*WORLD_W, randf()*WORLD_H));
    }
}

static void applyBiomeForCurrentWave() {
    int idx = biomeForWave(std::max(1, wave.number));
    if (idx != currentBiomeIndex || obstacles.empty()) {
        currentBiomeIndex = idx;
        generateObstacles(idx);
    }
}

static Vec2 spawnPosOffscreen() {
    int side = rand()%4;
    if (side==0) return Vec2(randf()*WORLD_W, -40);
    if (side==1) return Vec2(WORLD_W+40, randf()*WORLD_H);
    if (side==2) return Vec2(randf()*WORLD_W, WORLD_H+40);
    return Vec2(-40, randf()*WORLD_H);
}

static void spawnRandomCommonEnemy() {
    Vec2 p = spawnPosOffscreen();
    float roll = randf();
    EnemyType t;
    if (roll<0.25f) t=ENEMY_BALL;
    else if (roll<0.50f) t=ENEMY_TRIANGLE;
    else if (roll<0.70f) t=ENEMY_SQUARE;
    else if (roll<0.85f) t=ENEMY_SNIPER_COMMON;
    else t=ENEMY_RUNNER;
    
    Enemy* e = new Enemy(p, t);
    enemies.push_back(e);
}

static void spawnMiniboss() {
    Vec2 p = spawnPosOffscreen();
    EnemyType types[] = {ENEMY_MINIBOSS_SNIPER, ENEMY_MINIBOSS_BLINKER, ENEMY_MINIBOSS_DASHER};
    EnemyType t = types[wave.minibossesDefeated % 3];
    enemies.push_back(new Enemy(p, t));
}

static void startNextWave();

static void updateWaveLogic(int dt) {
    if (wave.phase == WAVE_NORMAL) {
        if (wave.enemiesToSpawn > 0) {
            wave.spawnTimer += dt;
            if (wave.spawnTimer >= wave.spawnInterval) {
                wave.spawnTimer = 0;
                wave.enemiesToSpawn--;
                spawnRandomCommonEnemy();
            }
        } else if (enemies.empty() && !wave.waitingNextWave) {
            wave.waitingNextWave = true;
            startNextWave();
        }
    } else if (wave.phase == WAVE_MINIBOSS) {
        if (enemies.empty() && !wave.waitingNextWave) {
            wave.waitingNextWave = true;
            wave.minibossesDefeated++;
            startNextWave();
        }
    } else if (wave.phase == WAVE_BOSS) {
        if (enemies.empty() && !wave.waitingNextWave) {
            wave.waitingNextWave = true;
            wave.phase = WAVE_CLEARED;
        }
    }
}

static void startNextWave() {
    wave.waitingNextWave = false;
    
    int nextWave = wave.number + 1;
    
    if (nextWave == 5 || nextWave == 10 || nextWave == 15) {
        wave.number = nextWave;
        wave.phase = WAVE_MINIBOSS;
        spawnMiniboss();
        wave.announcement = TextFormat("Onda %d/%d - MINI-CHEFE!", wave.number, TOTAL_WAVES);
        wave.announcementTimer = 2500;
        applyBiomeForCurrentWave();
    } else if (nextWave <= TOTAL_WAVES) {
        wave.number = nextWave;
        wave.phase = WAVE_NORMAL;
        wave.enemiesToSpawn = 8 + (int)(wave.number*4.5f);
        wave.spawnTimer = 0;
        wave.spawnInterval = std::max(280, 950 - wave.number*40);
        wave.announcement = TextFormat("Onda %d/%d", wave.number, TOTAL_WAVES);
        wave.announcementTimer = 2500;
        applyBiomeForCurrentWave();
        playSoundName("waveStart");
    } else if (wave.minibossesDefeated >= MINIBOSS_COUNT) {
        // boss fight
        if (devBossEnabled) {
            wave.phase = WAVE_BOSS_INTRO;
            Boss* b = new Boss(Vec2(playerPos.x, playerPos.y-400));
            boss.obj = b;
            boss.bossPhaseIndex = 0;
            boss.damageAccumulated = 0;
            boss.weaknessWindow = 0;
            boss.shockwaveTimer = 0;
            boss.teleport.stage = "none";
            enemies.push_back(b);
            wave.phase = WAVE_BOSS;
            boss.phase = BP_COOLDOWN;
            boss.attackCooldown = 900;
        } else {
            wave.phase = WAVE_CLEARED;
            wave.waitingNextWave = false;
        }
    } else {
        wave.phase = WAVE_MINIBOSS;
        spawnMiniboss();
    }
}

static void updateBlackHoleAbility(int dt) {
    if (chosenAbility != ABILITY_BLACKHOLE) return;
    for (int i=blackholes.size()-1; i>=0; i--) {
        auto& bh = blackholes[i];
        bh.timer += dt;
        if (bh.timer >= BH_DURATION) { blackholes.erase(blackholes.begin()+i); continue; }
        for (int ei=enemies.size()-1; ei>=0; ei--) {
            Enemy* e = enemies[ei];
            float d = dist(e->pos, bh.pos);
            if (d < BH_RADIUS + e->r) {
                float pull = 3 * (1 - d/(BH_RADIUS+e->r));
                Vec2 dir = (bh.pos - e->pos).normalized();
                e->pos.x += dir.x*pull; e->pos.y += dir.y*pull;
                if (e->isBoss()) e->takeDamage(BH_DAMAGE*0.3f);
                else e->takeDamage(BH_DAMAGE);
                if (!e->isBoss() && e->hp<=0) {
                    score++;
                    playerXp += 8;
                    spawnParticles(e->pos, {122,0,255,255}, 10);
                    delete e;
                    enemies.erase(enemies.begin()+ei);
                }
            }
        }
    }
    bhTimer += dt;
    if (bhTimer >= BH_COOLDOWN) {
        bhTimer = 0;
        BlackHole bh; bh.pos = playerPos; bh.timer = 0; bh.active = true;
        blackholes.push_back(bh);
    }
}

static void updateOverdrive(int dt) {
    if (chosenAbility != ABILITY_OVERDRIVE) return;
    if (overdriveActive) {
        overdriveTimer += dt;
        if (overdriveTimer >= overdriveDuration) {
            overdriveActive = false;
            overdriveTimer = 0;
            overdriveCooldownTimer = overdriveCooldown;
        }
    }
    if (overdriveCooldownTimer > 0) {
        overdriveCooldownTimer -= dt;
        if (overdriveCooldownTimer < 0) overdriveCooldownTimer = 0;
    }
}

static void updateSpeedBoost(int dt) {
    if (chosenAbility != ABILITY_WINDRUNNER) return;
    if (speedBoostActive) {
        speedBoostTimer += dt;
        if (speedBoostTimer >= speedBoostDuration) {
            speedBoostActive = false;
            speedBoostTimer = 0;
            speedBoostCooldownTimer = speedBoostCooldown;
        }
    }
    if (speedBoostCooldownTimer > 0) {
        speedBoostCooldownTimer -= dt;
        if (speedBoostCooldownTimer < 0) speedBoostCooldownTimer = 0;
    }
}

static void updateLaser(int dt, bool isPaused) {
    if (chosenAbility != ABILITY_LASER) return;
    // auto-activate when ready
    if (!laserActive && laserCooldownTimer<=0) {
        laserActive = true;
        laserTimer = 0;
    }
    if (laserActive) {
        laserTimer += dt;
        if (laserTimer >= laserDuration) {
            laserActive = false;
            laserTimer = 0;
            laserCooldownTimer = laserCooldown;
        } else if (!isPaused) {
            Vector2 mp = GetMousePosition();
            Vec2 target(mp.x + camera.x, mp.y + camera.y);
            Vec2 dir = (target - playerPos).normalized();
            float beamLen = (target - playerPos).len();
            float dmgThisFrame = laserDamage * dt/1000.0f;
            auto damageBeam = [&](Vec2 d) {
                for (int i=enemies.size()-1; i>=0; i--) {
                    auto& e = enemies[i];
                    Vec2 toEnemy = e->pos - playerPos;
                    float t = (toEnemy.x * d.x + toEnemy.y * d.y);
                    if (t < 0 || t > beamLen) continue;
                    Vec2 closest(playerPos.x + d.x*t, playerPos.y + d.y*t);
                    float dist2 = (e->pos - closest).len();
                    if (dist2 < e->r + laserWidth*3) {
                        e->takeDamage(dmgThisFrame);
                        spawnParticle(e->pos, {255,80,80,255});
                        if (e->hp <= 0) {
                            score += 1;
                            playerXp += 8;
                            spawnParticles(e->pos, YELLOW, 10);
                            if (e == boss.obj) boss.obj = nullptr;
                            delete e;
                            enemies.erase(enemies.begin()+i);
                        }
                    }
                }
            };
            damageBeam(dir);
        }
    }
    if (laserCooldownTimer > 0) {
        laserCooldownTimer -= dt;
        if (laserCooldownTimer < 0) laserCooldownTimer = 0;
    }
}

static void redistributeMinionAngles() {
    int n = minions.size();
    if (n == 0) return;
    for (int i = 0; i < n; i++)
        minions[i].angle = (float)i / n * M_PI * 2;
}

static void updateMinions(int dt) {
    for (auto& m : minions) {
        m.angle += m.orbitSpeed;
        Vec2 targetPos(
            playerPos.x + cosf(m.angle)*m.orbitDist,
            playerPos.y + sinf(m.angle)*m.orbitDist
        );
        // minion pos follows target
        m.shootTimer += dt;
        if (m.shootTimer >= m.shootCooldown) {
            m.shootTimer = 0;
            // find nearest enemy
            Enemy* nearest = nullptr;
            float nearDist = 1e9;
            for (auto& e : enemies) {
                float d = dist(targetPos, e->pos);
                if (d < nearDist) { nearDist = d; nearest = e; }
            }
            if (nearest && nearDist < m.shootRange) {
                Vec2 dir = (nearest->pos - targetPos).normalized();
                Bullet b;
                b.pos = targetPos;
                b.vel = dir * playerBulletSpeed * 0.6f;
                b.speedMag = playerBulletSpeed * 0.6f;
                b.r = 4;
                b.tethered = (m.shootCooldown <= 100);
                bullets.push_back(b);
                spawnParticle(targetPos, {255,102,255,255}, 3);
            }
        }
    }
}

static void tryFire(int dt) {
    if (chosenAbility == ABILITY_LASER) return;
    int rate = playerFireRate;
    if (chosenAbility==ABILITY_OVERDRIVE && overdriveActive) rate = playerFireRate/3;
    fireTimer += dt;
    if (fireTimer < rate) return;
    fireTimer = 0;
    
    Vec2 mouse = GetMousePosition();
    Vec2 targetWorld(mouse.x + camera.x, mouse.y + camera.y);
    float baseAngle = angleBetween(playerPos, targetWorld);
    
    // piercing
    if (chosenAbility == ABILITY_PIERCING) {
        shotCount++;
        if (shotCount >= 6) { shotCount = 0; piercingNext = true; }
    }
    bool isPiercing = piercingNext;
    
    for (int i=0; i<playerBullets; i++) {
        float spread = (i - (playerBullets-1)/2.0f) * 0.12f;
        float a = baseAngle + spread;
        bool isHoming = (chosenAbility==ABILITY_BULLMASTER && randf()<0.22f);
        float bSpeed = playerBulletSpeed;
        if (chosenAbility==ABILITY_OVERDRIVE && overdriveActive) bSpeed = playerBulletSpeed*3;
        
        Bullet b;
        b.pos = playerPos;
        b.vel = Vec2(cosf(a)*bSpeed, sinf(a)*bSpeed);
        b.r = isPiercing ? 14 : 5;
        b.speedMag = bSpeed;
        b.homing = isHoming;
        b.piercing = isPiercing;
        bullets.push_back(b);
    }
    if (isPiercing) piercingNext = false;
    playSoundName("shoot");
}

std::vector<Upgrade> upgrades;
std::vector<Upgrade> rareUpgrades;
std::vector<Upgrade> laserUpgrades;
std::vector<Upgrade>* upgradePool = &upgrades;

static void initUpgrades() {
    upgrades = {
        {"+20 Vida Maxima", "Aumenta sua vida maxima e cura 20.", []{
            playerMaxHp+=20; playerHp=std::min(playerHp+20,playerMaxHp);
        }},
        {"+10 Dano", "Suas balas causam mais dano.", []{ playerDamage+=10; }},
        {"+10% Velocidade", "Movimente-se mais rapido.", []{
            playerBaseSpeed+=0.5f;
            if (chosenAbility==ABILITY_WINDRUNNER) playerSpeed=playerBaseSpeed*1.30f;
            else playerSpeed+=0.5f;
        }},
        {"+1 Projetil", "Dispara um projetil adicional.", []{ playerBullets++; }},
        {"+20% Velocidade de Tiro", "Reduz intervalo entre tiros.", []{
            playerFireRate = std::max(80, (int)(playerFireRate*0.8f));
        }},
        {"+1.5 Regeneracao", "Recupera vitalentamente.", []{ playerRegen+=1.5f; }},
        {"+30 Escudo", "Ganha escudo que absorve dano.", []{
            playerShieldMax+=30; playerShieldHp+=30;
        }},
        {"+25% Vel. Projetil", "Balas mais rapidas.", []{ playerBulletSpeed*=1.25f; }},
        {"-20% Recarga Dash", "Dash mais rapido.", []{
            playerDashCooldownTime = std::max(200, (int)(playerDashCooldownTime*0.8f));
        }},
    };
    rareUpgrades = {
        {"Mini-Seguidor", "Adiciona +1 orbe que atira em inimigos.", []{
            Minion m; m.angle=0; m.orbitDist=60; m.orbitSpeed=0.03f;
            m.shootTimer=0; m.shootCooldown=(int)(playerFireRate*0.6f);
            m.damage=playerDamage*0.6f; m.shootRange=400; m.color={255,102,255,255};
            minions.push_back(m);
            redistributeMinionAngles();
        }}
    };
    laserUpgrades = {
        {"+10 Dano Laser", "Aumenta o dano do laser.", []{ laserDamage+=10; }},
        {"+0.5s Duracao Laser", "Laser dura mais tempo.", []{ laserDuration+=500; }},
    };
}

static void applyDevAttributes() {
    if (devModeUnlocked) {
        playerMaxHp = 500;
        playerHp = 500;
        playerDamage = 100;
        playerBulletSpeed = 30;
        playerFireRate = 50;
        playerBullets = 5;
        playerShieldMax = 100;
        playerShieldHp = 100;
    }
}

static void applyAbilityPassives() {
    if (chosenAbility == ABILITY_WINDRUNNER) {
        playerBaseSpeed = 4;
        playerSpeed = playerBaseSpeed * 1.30f;
    } else if (chosenAbility == ABILITY_BULLMASTER) {
        playerBulletSpeed *= 0.5f;
    }
}

static void drawWorld() {
    const Biome& b = biomes[currentBiomeIndex];
    ClearBackground(b.bg);
    
    int gs = 80;
    float startX = -fmodf(camera.x, gs);
    float startY = -fmodf(camera.y, gs);
    Color gridCol = b.grid; gridCol.a = 30;
    for (float x=startX; x<SCREEN_W; x+=gs)
        DrawLine(x, 0, x, SCREEN_H, gridCol);
    for (float y=startY; y<SCREEN_H; y+=gs)
        DrawLine(0, y, SCREEN_W, y, gridCol);

    if (graphicsMode != GRAPHICS_LOW) {
        for (auto& d : decorations) {
            float sx=toScreenX(d.x), sy=toScreenY(d.y);
            if (sx<-20||sx>SCREEN_W+20||sy<-20||sy>SCREEN_H+20) continue;
            DrawCircle(sx, sy, 3, b.decorColor);
        }
    }
    
    if (graphicsMode != GRAPHICS_LOW) {
        for (auto& u : uniqueElements) {
            float sx=toScreenX(u.pos.x), sy=toScreenY(u.pos.y);
            if (sx<-60||sx>SCREEN_W+60||sy<-60||sy>SCREEN_H+60) continue;
            DrawCircle(sx, sy, u.r, u.color);
        }
    }
    
    for (auto& o : obstacles) {
        float sx=toScreenX(o.x), sy=toScreenY(o.y);
        if (sx>SCREEN_W||sy>SCREEN_H||sx+o.w<0||sy+o.h<0) continue;
        DrawRectangle(sx, sy, o.w, o.h, b.obstacleColor);
        DrawRectangleLines(sx, sy, o.w, o.h, b.obstacleEdge);
        // hp bar
        float bw = o.w, bh = 4;
        DrawRectangle(sx, sy-6, bw, bh, {20,20,20,200});
        DrawRectangle(sx, sy-6, bw*(o.hp/o.maxHp), bh, {77,255,122,200});
    }
}

static void drawMinimap() {
    const float mmW = 200, mmH = 200;
    const float mmX = SCREEN_W - mmW - 10, mmY = SCREEN_H - mmH - 10;
    const float sx = mmW / WORLD_W, sy = mmH / WORLD_H;
    DrawRectangle(mmX, mmY, mmW, mmH, {0, 0, 0, 150});
    DrawRectangleLines(mmX, mmY, mmW, mmH, {100, 100, 130, 180});
    for (auto& o : obstacles) {
        float ox = mmX + o.x * sx, oy = mmY + o.y * sy;
        float ow = o.w * sx, oh = o.h * sy;
        if (ow < 1) ow = 1; if (oh < 1) oh = 1;
        DrawRectangle(ox, oy, ow, oh, {60, 60, 80, 180});
    }
    float vx = mmX + camera.x * sx, vy = mmY + camera.y * sy;
    DrawRectangleLines(vx, vy, SCREEN_W * sx, SCREEN_H * sy, {255, 255, 255, 80});
    for (auto& e : enemies) {
        float ex = mmX + e->pos.x * sx, ey = mmY + e->pos.y * sy;
        Color c = e->isBoss() ? Color{255, 50, 50, 220} : Color{255, 100, 100, 180};
        float rad = e->isBoss() ? 4 : 2;
        DrawCircle(ex, ey, rad, c);
    }
    float px = mmX + playerPos.x * sx, py = mmY + playerPos.y * sy;
    DrawCircle(px, py, 4, {42, 157, 244, 255});
    DrawCircleLines(px, py, 5, {255, 255, 255, 100});
}

static void drawHUD() {
    DrawText(TextFormat("Nivel %d | XP: %d/%d", playerLevel, playerXp, playerXpNeeded), 12, 10, 16, WHITE);

    DrawRectangle(12, 30, 230, 20, {42,8,8,255});
    float hpPct = playerHp/playerMaxHp;
    Color hpCol = hpPct > 0.5f ? COL_HP : Color{255,180,0,(unsigned char)(hpPct>0.25f?255:180)};
    if (hpPct <= 0.25f) hpCol = {255,60,60,255};
    DrawRectangle(12, 30, 230*hpPct, 20, hpCol);
    if (hpPct > 0.05f) DrawRectangle(14, 32, 226*hpPct-4, 5, {255,255,255,25});
    DrawRectangleLines(12, 30, 230, 20, {110,20,20,255});
    DrawText(TextFormat("%.0f/%.0f", playerHp, playerMaxHp), 130 - MeasureText(TextFormat("%.0f/%.0f", playerHp, playerMaxHp), 12)/2, 34, 12, WHITE);
    
    int hudY = 55;
    if (playerShieldMax > 0) {
        DrawRectangle(12, hudY, 230, 10, {40,80,120,76});
        DrawRectangle(12, hudY, 230*(playerShieldHp/playerShieldMax), 10, COL_SHIELD);
        DrawRectangleLines(12, hudY, 230, 10, {60,120,180,120});
        hudY += 15;
    }
    
    DrawText(TextFormat("Pontos: %d", score), 12, hudY, 14, WHITE);
    hudY += 18;
    
    DrawText("ST", 12, hudY, 10, {180,180,200,200});
    DrawRectangle(26, hudY, 50, 8, {0,0,0,128});
    DrawRectangle(26, hudY, 50*(playerStamina/playerMaxStamina), 8, COL_STAMINA);
    DrawRectangleLines(26, hudY, 50, 8, {80,120,80,100});
    hudY += 14;
    
    DrawText(TextFormat("Vel: %.1f", playerSpeed), 12, hudY, 11, {180,180,200,200}); hudY += 13;
    DrawText(TextFormat("Tiros: %d", playerBullets), 12, hudY, 11, {180,180,200,200}); hudY += 13;
    DrawText(TextFormat("Int: %dms", playerFireRate), 12, hudY, 11, {180,180,200,200}); hudY += 13;
    DrawText(TextFormat("Dano: %.0f", playerDamage), 12, hudY, 11, {180,180,200,200}); hudY += 13;
    DrawText(TextFormat("V.P: %.1f", playerBulletSpeed), 12, hudY, 11, {180,180,200,200}); hudY += 14;
    
    const char* waveText = "";
    const char* biomeName = biomes[currentBiomeIndex].name;
    Color waveColor = WHITE;
    if (wave.phase == WAVE_NORMAL) {
        waveText = TextFormat("Onda %d/%d | %s", wave.number, TOTAL_WAVES, biomeName);
    } else if (wave.phase == WAVE_MINIBOSS) {
        waveText = TextFormat("Mini-Chefe | %s", biomeName);
        waveColor = {255,200,80,255};
    } else if (wave.phase == WAVE_BOSS) {
        waveText = TextFormat("CHEFE FINAL | %s", biomeName);
        waveColor = {255,60,60,255};
        float pulseW = 0.8f+sinf(GetTime()*0.005f)*0.2f;
        waveColor.a = (unsigned char)(255*pulseW);
    } else if (wave.phase == WAVE_CLEARED) {
        waveText = "VITORIA!";
        waveColor = COL_GOLD;
        waveColor.a = (unsigned char)(200+int(sinf(GetTime()*0.004f)*55));
    }
    
    int tw = MeasureText(waveText, 14);
    int waveX = SCREEN_W - tw - 15;
    DrawRectangle(waveX-4, 8, tw+8, 22, {0,0,0,100});
    DrawText(waveText, waveX, 12, 14, waveColor);
    
    const char* abText = "";
    Color abColor = {200,200,200,255};
    switch(chosenAbility) {
        case ABILITY_BULLMASTER: abText = "Bull Master"; abColor={255,170,50,255}; break;
        case ABILITY_WINDRUNNER: {
            if (speedBoostActive) { abText = "Pes Ligeiros — VENTO"; abColor={150,255,150,255}; }
            else if (speedBoostCooldownTimer>0) { abText = TextFormat("Pes Ligeiros — %ds", speedBoostCooldownTimer/1000+1); abColor={150,200,150,200}; }
            else { abText = "Pes Ligeiros — PRONTO"; abColor={100,255,100,255}; }
            break;
        }
        case ABILITY_PIERCING: abText = "Atravessamento"; abColor={255,100,100,255}; break;
        case ABILITY_BLACKHOLE: abText = "Buraco Negro"; abColor={160,80,255,255}; break;
        case ABILITY_OVERDRIVE: {
            if (overdriveActive) { abText = "Rajada Explosiva — ATIVO"; abColor={255,255,80,255}; }
            else if (overdriveCooldownTimer>0) { abText = TextFormat("Rajada Explosiva — %ds", overdriveCooldownTimer/1000+1); abColor={200,200,100,200}; }
            else { abText = "Rajada Explosiva — PRONTO"; abColor={255,255,100,255}; }
            break;
        }
        case ABILITY_LASER: {
            if (laserActive) { abText = TextFormat("Laser %d%%", (int)(100-100*laserTimer/laserDuration)); abColor={255,50,50,255}; }
            else if (laserCooldownTimer>0) { abText = TextFormat("Recarga %ds", laserCooldownTimer/1000+1); abColor={200,80,80,200}; }
            else { abText = "Laser — PRONTO"; abColor={255,100,100,255}; }
            break;
        }
        default: break;
    }
    if (abText[0]) {
        int abW = MeasureText(abText, 13);
        int abX = SCREEN_W - abW - 15;
        DrawRectangle(abX-4, SCREEN_H-30, abW+8, 22, {0,0,0,120});
        DrawText(abText, abX, SCREEN_H-27, 13, abColor);
        // overdrive duration/cooldown bar
        if (chosenAbility == ABILITY_OVERDRIVE) {
            int barW = abW+8, barH = 4;
            int barX = abX-4, barY = SCREEN_H-8;
            if (overdriveActive) {
                float pct = 1.0f - (float)overdriveTimer/overdriveDuration;
                DrawRectangle(barX, barY, barW, barH, {60,30,30,180});
                DrawRectangle(barX, barY, (int)(barW*pct), barH, {255,255,80,255});
            } else if (overdriveCooldownTimer > 0) {
                float pct = 1.0f - (float)overdriveCooldownTimer/overdriveCooldown;
                DrawRectangle(barX, barY, barW, barH, {30,30,30,180});
                DrawRectangle(barX, barY, (int)(barW*pct), barH, {100,100,60,200});
            }
        }
        if (chosenAbility == ABILITY_LASER) {
            int barW = abW+8, barH = 4;
            int barX = abX-4, barY = SCREEN_H-8;
            if (laserActive) {
                float pct = 1.0f - (float)laserTimer/laserDuration;
                DrawRectangle(barX, barY, barW, barH, {60,20,20,180});
                DrawRectangle(barX, barY, (int)(barW*pct), barH, {255,50,50,255});
            } else if (laserCooldownTimer > 0) {
                float pct = 1.0f - (float)laserCooldownTimer/laserCooldown;
                DrawRectangle(barX, barY, barW, barH, {30,30,30,180});
                DrawRectangle(barX, barY, (int)(barW*pct), barH, {100,50,50,200});
            }
        }
    }
}

static void drawMenu() {
    ClearBackground({10,5,20,255});
    for (int i=0; i<30; i++) {
        float px = (sinf(GetTime()*0.001f*(i%7+2)+i)*0.5f+0.5f) * SCREEN_W;
        float py = (cosf(GetTime()*0.0008f*(i%5+3)+i*2)*0.5f+0.5f) * SCREEN_H;
        DrawCircle(px, py, 2, {(unsigned char)(100+i*5),(unsigned char)(60+i*3),(unsigned char)(150+i*4),40});
    }
    float tp = 0.9f + sinf(GetTime()*0.002f)*0.1f;
    Color tcol = COL_GOLD; tcol.a = (unsigned char)(255*tp);
    int tw = MeasureText("EDGE EXTRUSION", 50);
    DrawText("EDGE EXTRUSION", SCREEN_W/2-tw/2, 150, 50, tcol);

    Color subCol = {180,180,200,(unsigned char)(100 + int(sinf(GetTime()*0.0015f)*40))};
    DrawText("Sobreviva. Evolua. Extrude.", SCREEN_W/2-MeasureText("Sobreviva. Evolua. Extrude.", 18)/2, 210, 18, subCol);
    
    DrawText("WASD para mover", SCREEN_W/2-MeasureText("WASD para mover", 20)/2, 270, 20, WHITE);
    DrawText("Mire com o mouse", SCREEN_W/2-MeasureText("Mire com o mouse", 20)/2, 300, 20, WHITE);
    DrawText("Sobreviva a 15 ondas e derrote o CHEFAO", SCREEN_W/2-MeasureText("Sobreviva a 15 ondas e derrote o CHEFAO", 20)/2, 330, 20, WHITE);

    float ba = 0.5f + sinf(GetTime()*0.003f)*0.5f;
    Color hintCol = {200,200,200,(unsigned char)(ba*255)};
    DrawText("Pressione ESPACO para continuar", SCREEN_W/2-MeasureText("Pressione ESPACO para continuar", 20)/2, 400, 20, hintCol);
    
    if (devModeUnlocked) {
        float ga = 0.8f + sinf(GetTime()*0.004f)*0.2f;
        Color dc = COL_GOLD; dc.a = (unsigned char)(255*ga);
        DrawText("MODO DEV DESBLOQUEADO!", SCREEN_W/2-MeasureText("MODO DEV DESBLOQUEADO!", 24)/2, 450, 24, dc);
        DrawText("Pressione D no lobby para configurar", SCREEN_W/2-MeasureText("Pressione D no lobby para configurar", 16)/2, 480, 16, {200,200,200,180});
    } else {
        char display[40];
        snprintf(display, sizeof(display), "> %s", menuInput);
        int dw = MeasureText(display, 14);
        DrawText(display, SCREEN_W/2-dw/2, 450, 14, {150,150,170,120});
        float ba2 = 0.5f + sinf(GetTime()*0.003f)*0.5f;
        Color bc = {150,150,170,(unsigned char)(ba2*255)};
        DrawText("_", SCREEN_W/2 + dw/2 + 2, 450, 14, bc);
    }
}

static void drawLobby() {
    ClearBackground({15,10,30,255});
    for (int i=0; i<20; i++) {
        float px = (sinf(GetTime()*0.0009f*(i%6+2)+i)*0.5f+0.5f) * SCREEN_W;
        float py = (cosf(GetTime()*0.0007f*(i%4+3)+i*3)*0.5f+0.5f) * SCREEN_H;
        DrawCircle(px, py, 2, {(unsigned char)(80+i*6),(unsigned char)(50+i*3),(unsigned char)(140+i*5),30});
    }
    
    float tp = 0.9f + sinf(GetTime()*0.002f)*0.1f;
    Color tcol = {255,255,255,(unsigned char)(255*tp)};
    DrawText("ESCOLHA SUA HABILIDADE", SCREEN_W/2-MeasureText("ESCOLHA SUA HABILIDADE", 30)/2, 50, 30, tcol);
    DrawText("1-6 para selecionar, ESPACO para comecar", SCREEN_W/2-MeasureText("1-6 para selecionar, ESPACO para comecar", 16)/2, 90, 16, {180,180,180,255});
    
    const char* abNames[] = {"Bull Master","Pes Ligeiros","Atravessamento","Buraco Negro","Rajada Explosiva","Laser"};
    const char* abIcons[] = {"B","W","P","H","R","L"};
    const char* abDescs[] = {"Investida veloz","Velocidade加倍","Tiros perfurantes","Gravidade mortal","Rajada explosiva","Raio continuo"};
    const char* abKeys[] = {"1","2","3","4","5","6"};
    Color abColors[] = {{255,170,50,255},{100,255,100,255},{255,100,100,255},{160,80,255,255},{255,255,80,255},{255,50,50,255}};
    
    int abCount = 6;
    int boxW = 165, boxH = 85, gap = 10;
    int totalW = boxW*abCount + gap*(abCount-1);
    int startX = SCREEN_W/2 - totalW/2;
    int boxY = 150;
    
    for (int i=0; i<abCount; i++) {
        int bx = startX + i*(boxW+gap);
        bool selected = (int)chosenAbility==i;
        Color bg = selected ? Color{50,40,70,230} : Color{20,15,35,200};
        Color border = selected ? abColors[i] : Color{60,50,80,150};
        DrawRectangle(bx, boxY, boxW, boxH, bg);
        DrawRectangleLines(bx, boxY, boxW, boxH, border);
        if (selected) {
            DrawRectangleLines(bx+2, boxY+2, boxW-4, boxH-4, Color{border.r,border.g,border.b,60});
        }
        DrawCircle(bx+20, boxY+20, 12, abColors[i]);
        DrawText(abKeys[i], bx+15, boxY+14, 14, {5,3,15,255});
        DrawText(abIcons[i], bx+42, boxY+12, 18, abColors[i]);
        DrawText(abNames[i], bx+10, boxY+42, 14, selected?COL_GOLD:Color{200,200,200,255});
        DrawText(abDescs[i], bx+10, boxY+62, 11, {150,150,170,200});
    }
    
    if (devModeUnlocked) {
        float ga = 0.8f + sinf(GetTime()*0.004f)*0.2f;
        Color dc = COL_GOLD; dc.a = (unsigned char)(255*ga);
        DrawText("[D] MODO DEV", SCREEN_W/2-MeasureText("[D] MODO DEV", 20)/2, 400, 20, dc);
    }
}

static void drawUpgradeMenu() {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, {5,3,15,220});

    const char* title = "UPGRADE";
    float pulse = 0.9f + sinf(GetTime()*0.003f)*0.1f;
    int titleW = MeasureText(title, 40);
    int titleX = SCREEN_W/2 - titleW/2;
    Color titleColor = {255,215,0,(unsigned char)(255*pulse)};
    DrawText(title, titleX, 120, 40, titleColor);
    for (int i=0; i<2; i++) {
        int lx = i==0 ? titleX-40 : titleX+titleW+10;
        int ly = 148;
        DrawRectangle(lx, ly, 30, 2, COL_GOLD);
    }

    DrawText(TextFormat("Nivel %d", playerLevel), SCREEN_W/2 - MeasureText(TextFormat("Nivel %d", playerLevel), 16)/2, 170, 16, {180,180,180,255});

    int choices[3] = {upgradeDisplay1, upgradeDisplay2, upgradeDisplay3};
    int boxW = 320, boxH = 100;
    int startX = SCREEN_W/2 - (boxW*3 + 30)/2;
    int boxY = 210;

    for (int i=0; i<3; i++) {
        int bx = startX + i*(boxW+15);
        int by = boxY;
        int idx = choices[i];

        bool hovered = false;
        if (upgradeCycleFinished) {
            Vector2 mp = GetMousePosition();
            hovered = (mp.x >= bx && mp.x <= bx+boxW && mp.y >= by && mp.y <= by+boxH);
        }

        Color boxBg = hovered ? Color{40,30,60,230} : Color{20,15,35,220};
        Color boxBorder = hovered ? COL_GOLD : Color{80,70,100,180};

        DrawRectangle(bx, by, boxW, boxH, boxBg);
        DrawRectangleLines(bx, by, boxW, boxH, boxBorder);

        DrawText(TextFormat("%d", i+1), bx+10, by+8, 18, hovered?COL_GOLD:Color{150,140,170,255});

        if (idx >= 0 && idx < (int)upgradePool->size()) {
            Color nameColor = upgradeCycleFinished ? COL_GOLD : Color{120,110,140,200};
            DrawText((*upgradePool)[idx].name, bx+40, by+15, 18, nameColor);
            DrawText((*upgradePool)[idx].desc, bx+15, by+45, 13, {180,180,190,200});
        } else {
            DrawText("???", bx+boxW/2-15, by+boxH/2-10, 20, {80,70,100,180});
        }
    }
    if (upgradeCycleFinished) {
        float hintAlpha = 0.7f + sinf(GetTime()*0.002f)*0.3f;
        Color hintColor = {200,200,210,(unsigned char)(hintAlpha*255)};
        const char* hint = "Clique ou pressione 1, 2, 3 para escolher";
        DrawText(hint, SCREEN_W/2-MeasureText(hint, 15)/2, 360, 15, hintColor);
    } else {
        DrawText("SORTEANDO...", SCREEN_W/2-MeasureText("SORTEANDO...", 18)/2, 360, 18, {150,130,80,(unsigned char)(120 + (int)(sinf(GetTime()*0.006f)*60))});
    }
}

static bool settingsOpen = false;
static void drawPauseMenu() {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, {0,0,0,200});

    if (settingsOpen) {
        float tp = 0.85f + sinf(GetTime()*0.002f)*0.15f;
        Color tcol = {255,255,255,(unsigned char)(255*tp)};
        DrawText("CONTROLES", SCREEN_W/2-MeasureText("CONTROLES", 36)/2, 180, 36, tcol);

        int boxW = 300, boxH = 50, gap = 15;
        int startY = 270;
        Vector2 mp = GetMousePosition();

        const char* modes[2] = {"WASD — Movimento pelo teclado", "Mouse — Move na direcao do cursor"};
        for (int i=0; i<2; i++) {
            int by = startY + i*(boxH+gap);
            int bx = SCREEN_W/2 - boxW/2;
            bool selected = (int)controlMode == i;
            bool hovered = (mp.x>=bx && mp.x<=bx+boxW && mp.y>=by && mp.y<=by+boxH);
            Color bg = selected ? Color{50,60,80,240} : (hovered?Color{40,35,55,240}:Color{25,20,40,220});
            Color border = selected ? COL_GOLD : Color{70,60,90,180};
            DrawRectangle(bx, by, boxW, boxH, bg);
            DrawRectangleLines(bx, by, boxW, boxH, border);
            if (selected) DrawRectangleLines(bx+2, by+2, boxW-4, boxH-4, Color{border.r,border.g,border.b,60});
            DrawText(modes[i], bx+10, by+17, 13, selected?COL_GOLD:(hovered?Color{220,220,220,255}:Color{180,180,180,255}));
        }

        float ba = 0.5f+sinf(GetTime()*0.003f)*0.3f;
        Color hintC = {180,180,200,(unsigned char)(ba*200)};
        DrawText("Clique para selecionar | ESC voltar", SCREEN_W/2-MeasureText("Clique para selecionar | ESC voltar", 14)/2, 420, 14, hintC);
        return;
    }

    float tp = 0.85f + sinf(GetTime()*0.002f)*0.15f;
    Color tcol = {255,255,255,(unsigned char)(255*tp)};
    DrawText("PAUSA", SCREEN_W/2-MeasureText("PAUSA", 46)/2, 180, 46, tcol);

    const char* items[4] = {"Continuar", "Recomecar", "Configuracoes", "Voltar ao Menu"};
    int boxW = 240, boxH = 50, gap = 15;
    int startY = 270;

    Vector2 mp = GetMousePosition();
    for (int i=0; i<4; i++) {
        int by = startY + i*(boxH+gap);
        int bx = SCREEN_W/2 - boxW/2;
        bool hovered = (mp.x>=bx && mp.x<=bx+boxW && mp.y>=by && mp.y<=by+boxH);
        Color bg = hovered ? Color{50,45,70,240} : Color{25,20,40,220};
        Color border = hovered ? COL_GOLD : Color{70,60,90,180};
        DrawRectangle(bx, by, boxW, boxH, bg);
        DrawRectangleLines(bx, by, boxW, boxH, border);
        if (hovered) DrawRectangleLines(bx+2, by+2, boxW-4, boxH-4, Color{border.r,border.g,border.b,50});
        DrawText(items[i], bx+boxW/2-MeasureText(items[i], 18)/2, by+16, 18, hovered?COL_GOLD:Color{200,200,200,255});
    }
}

static void resetGameState() {
    playerPos = Vec2(WORLD_W/2, WORLD_H/2);
    playerHp = playerMaxHp;
    playerShieldHp = playerShieldMax;
    playerStamina = playerMaxStamina;
    playerXp = 0; playerLevel = 1; playerXpNeeded = 0;
    playerDashCooldown = 0; playerContactCooldown = 0;
    if (!devModeUnlocked) { playerBullets = 1; }
    overdriveActive = false; speedBoostActive = false; laserActive = false;
    overdriveTimer = 0; overdriveCooldownTimer = 0;
    speedBoostTimer = 0; speedBoostCooldownTimer = 0;
    laserTimer = 0; laserCooldownTimer = 0;
    piercingNext = false; shotCount = 0; bhTimer = 0; fireTimer = 0;
    for (auto& e : enemies) delete e;
    boss.obj = nullptr;
    bullets.clear(); enemyBullets.clear(); particles.clear();
    shockwaves.clear(); bombs.clear(); blackholes.clear();
    enemies.clear(); minions.clear(); score = 0; playerTrailCount = 0;
    if (devModeUnlocked && devMinionCount > 0) {
        for (int i = 0; i < devMinionCount; i++) {
            Minion m;
            m.angle = (float)i / devMinionCount * M_PI * 2;
            m.orbitDist = 80;
            m.orbitSpeed = 0.03f;
            m.shootTimer = 0;
            m.shootCooldown = 600;
            m.damage = playerDamage * 0.5f;
            m.shootRange = 400;
            m.color = {255,200,50,255};
            minions.push_back(m);
        }
    }
    applyAbilityPassives();
    wave.number = 0; wave.phase = WAVE_NORMAL;
    wave.minibossesDefeated = 0; wave.waitingNextWave = false;
    paused = false;
    settingsOpen = false;
    upgradePool = &upgrades;
}

int main() {
    srand(time(0));
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_FULLSCREEN_MODE);
    InitWindow(SCREEN_W, SCREEN_H, "Edge Extrusion");
    InitAudioDevice();
    SetTargetFPS(60);
    
    initSounds();
    initUpgrades();

        int lastUpgradeChoice1 = -1, lastUpgradeChoice2 = -1, lastUpgradeChoice3 = -1;
        int upgradePickCount = 0;
    
    bool devPanelOpen = false;
    
    gamePhase = PHASE_MENU;
    
    while (!WindowShouldClose()) {
        int dt = (int)(GetFrameTime()*1000);
        if (dt < 1) dt = 1;
        
        if (gamePhase == PHASE_MENU) {
            if (IsKeyPressed(KEY_SPACE)) {
                gamePhase = PHASE_LOBBY;
                chosenAbility = ABILITY_BULLMASTER;
            }
            int c = GetCharPressed();
            while (c > 0) {
                if (menuInputLen < 30 && c >= 32 && c <= 126) {
                    menuInput[menuInputLen++] = (char)c;
                    menuInput[menuInputLen] = 0;
                }
                c = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && menuInputLen > 0) {
                menuInput[--menuInputLen] = 0;
            }
            if (!devModeUnlocked && strcmp(menuInput, "J0gad0r1234dev") == 0) {
                devModeUnlocked = true;
                menuInputLen = 0;
                menuInput[0] = 0;
            }
        } else if (gamePhase == PHASE_LOBBY) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mp = GetMousePosition();
                int abCount = 6;
                int boxW = 165, boxH = 85, gap = 10;
                int totalW = boxW*abCount + gap*(abCount-1);
                int startX = SCREEN_W/2 - totalW/2;
                int boxY = 150;
                for (int i=0; i<abCount; i++) {
                    int bx = startX + i*(boxW+gap);
                    if (mp.x>=bx && mp.x<=bx+boxW && mp.y>=boxY && mp.y<=boxY+boxH)
                        chosenAbility = (AbilityID)(i);
                }
            }
            if (IsKeyPressed(KEY_ONE)) chosenAbility = ABILITY_BULLMASTER;
            if (IsKeyPressed(KEY_TWO)) chosenAbility = ABILITY_WINDRUNNER;
            if (IsKeyPressed(KEY_THREE)) chosenAbility = ABILITY_PIERCING;
            if (IsKeyPressed(KEY_FOUR)) chosenAbility = ABILITY_BLACKHOLE;
            if (IsKeyPressed(KEY_FIVE)) chosenAbility = ABILITY_OVERDRIVE;
            if (IsKeyPressed(KEY_SIX)) chosenAbility = ABILITY_LASER;
            if (IsKeyPressed(KEY_D) && devModeUnlocked) {
                devPanelOpen = !devPanelOpen;
            }
            if (IsKeyPressed(KEY_SPACE) && !devPanelOpen) {
                gamePhase = PHASE_GAME;
                gameStarted = true;
                playerPos = Vec2(WORLD_W/2, WORLD_H/2);
                playerHp = playerMaxHp;
                playerShieldHp = playerShieldMax;
                playerStamina = playerMaxStamina;
                playerXp = 0; playerLevel = 1; playerXpNeeded = 0;
                playerDashCooldown = 0; playerContactCooldown = 0;
                if (!devModeUnlocked) { playerBullets = 1; }
                overdriveActive = false; speedBoostActive = false; laserActive = false;
                overdriveTimer = 0; overdriveCooldownTimer = 0;
                speedBoostTimer = 0; speedBoostCooldownTimer = 0;
                laserTimer = 0; laserCooldownTimer = 0;
                piercingNext = false; shotCount = 0; bhTimer = 0;
                bullets.clear(); enemyBullets.clear(); particles.clear();
                shockwaves.clear(); bombs.clear(); blackholes.clear();
                enemies.clear(); minions.clear(); score = 0; playerTrailCount = 0;
                
                applyAbilityPassives();
                
                if (devModeUnlocked && devMinionCount > 0) {
                    for (int i = 0; i < devMinionCount; i++) {
                        Minion m;
                        m.angle = (float)i / devMinionCount * M_PI * 2;
                        m.orbitDist = 80;
                        m.orbitSpeed = 0.03f;
                        m.shootTimer = 0;
                        m.shootCooldown = 600;
                        m.damage = playerDamage * 0.5f;
                        m.shootRange = 400;
                        m.color = {255,200,50,255};
                        minions.push_back(m);
                    }
                }
                
                wave.number = 0; wave.phase = WAVE_NORMAL;
                wave.minibossesDefeated = 0; wave.waitingNextWave = false;
                
                startNextWave();
                if (devModeUnlocked && devWaveNumber > 1) {
                    while (wave.number < devWaveNumber && wave.number < TOTAL_WAVES) {
                        wave.number++;
                        wave.phase = WAVE_NORMAL;
                        wave.enemiesToSpawn = 8 + (int)(wave.number*4.5f);
                        wave.spawnTimer = 0;
                        wave.spawnInterval = std::max(280, 950 - wave.number*40);
                        wave.announcement = TextFormat("Onda %d/%d", wave.number, TOTAL_WAVES);
                        wave.announcementTimer = 2500;
                        applyBiomeForCurrentWave();
                    }
                    boss.obj = nullptr;
                    for (auto& e : enemies) delete e;
                    enemies.clear();
                }
                if (devModeUnlocked && devDebugTarget >= 0) {
                    int target = devDebugTarget;
                    devDebugTarget = -1;
                    wave.number = TOTAL_WAVES;
                    wave.minibossesDefeated = target == 3 ? MINIBOSS_COUNT : target;
                    wave.enemiesToSpawn = 0;
                    wave.waitingNextWave = false;
                }
            }
        } else if (gamePhase == PHASE_GAME || gamePhase == PHASE_UPGRADE) {
            if (IsKeyPressed(KEY_ENTER)) {
                if (gamePhase == PHASE_UPGRADE) {
                    gamePhase = PHASE_GAME; paused = false;
                } else {
                    paused = !paused;
                    if (!paused) settingsOpen = false;
                }
            }

            if (paused && gamePhase == PHASE_GAME) {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    if (settingsOpen) settingsOpen = false;
                    else { paused = false; }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    Vector2 mp = GetMousePosition();
                    int boxW, boxH, gap = 15, startY;
                    if (settingsOpen) {
                        boxW = 300; boxH = 50; startY = 270;
                        int bx = SCREEN_W/2 - boxW/2;
                        for (int i=0; i<2; i++) {
                            int by = startY + i*(boxH+gap);
                            if (mp.x>=bx && mp.x<=bx+boxW && mp.y>=by && mp.y<=by+boxH) {
                                controlMode = (ControlMode)i;
                            }
                        }
                    } else {
                        boxW = 240; boxH = 50; startY = 270;
                        int bx = SCREEN_W/2 - boxW/2;
                        for (int i=0; i<4; i++) {
                            int by = startY + i*(boxH+gap);
                            if (mp.x>=bx && mp.x<=bx+boxW && mp.y>=by && mp.y<=by+boxH) {
                                if (i==0) { paused = false; } // continuar
                                else if (i==1) { // recomecar
                                    for (auto& e : enemies) delete e;
                                    enemies.clear();
                                    resetGameState();
                                    wave.number = 0; wave.phase = WAVE_NORMAL;
                                    wave.minibossesDefeated = 0; wave.waitingNextWave = false;
                                    startNextWave();
                                    if (devModeUnlocked && devWaveNumber > 1) {
                                        while (wave.number < devWaveNumber && wave.number < TOTAL_WAVES) {
                                            wave.number++;
                                            wave.phase = WAVE_NORMAL;
                                            wave.enemiesToSpawn = 8 + (int)(wave.number*4.5f);
                                            wave.spawnTimer = 0;
                                            wave.spawnInterval = std::max(280, 950 - wave.number*40);
                                            wave.announcement = TextFormat("Onda %d/%d", wave.number, TOTAL_WAVES);
                                            wave.announcementTimer = 2500;
                                            applyBiomeForCurrentWave();
                                        }
                                        boss.obj = nullptr;
                                        for (auto& e : enemies) delete e;
                                        enemies.clear();
                                    }
                                } else if (i==2) { // configuracoes
                                    settingsOpen = true;
                                } else if (i==3) { // voltar ao menu
                                    boss.obj = nullptr;
                                    for (auto& e : enemies) delete e;
                                    enemies.clear();
                                    gamePhase = PHASE_MENU; gameStarted = false;
                                }
                            }
                        }
                    }
                }
                if (IsKeyPressed(KEY_R)) {
                    boss.obj = nullptr;
                    for (auto& e : enemies) delete e;
                    enemies.clear();
                    resetGameState();
                    wave.number = 0; wave.phase = WAVE_NORMAL;
                    wave.minibossesDefeated = 0; wave.waitingNextWave = false;
                    startNextWave();
                    if (devModeUnlocked && devWaveNumber > 1) {
                        while (wave.number < devWaveNumber && wave.number < TOTAL_WAVES) {
                            wave.number++;
                            wave.phase = WAVE_NORMAL;
                            wave.enemiesToSpawn = 8 + (int)(wave.number*4.5f);
                            wave.spawnTimer = 0;
                            wave.spawnInterval = std::max(280, 950 - wave.number*40);
                            wave.announcement = TextFormat("Onda %d/%d", wave.number, TOTAL_WAVES);
                            wave.announcementTimer = 2500;
                            applyBiomeForCurrentWave();
                        }
                    }
                }
            }
            
            if (!paused) {
                float moveX=0, moveY=0;
                float mul = 1;
                if (chosenAbility==ABILITY_WINDRUNNER && speedBoostActive) mul = 2.5f;
                
                if (controlMode == CONTROL_WASD) {
                    if (IsKeyDown(KEY_W)) moveY = -playerSpeed*mul;
                    if (IsKeyDown(KEY_S)) moveY = playerSpeed*mul;
                    if (IsKeyDown(KEY_A)) moveX = -playerSpeed*mul;
                    if (IsKeyDown(KEY_D)) moveX = playerSpeed*mul;
                } else {
                    Vector2 mp = GetMousePosition();
                    Vec2 target(mp.x + camera.x, mp.y + camera.y);
                    Vec2 dir = (target - playerPos).normalized();
                    float dist = (target - playerPos).len();
                    if (dist > 10) { moveX = dir.x * playerSpeed * mul; moveY = dir.y * playerSpeed * mul; }
                }
                
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    if (chosenAbility == ABILITY_OVERDRIVE && !overdriveActive && overdriveCooldownTimer<=0) {
                        overdriveActive = true; overdriveTimer = 0;
                    } else if (chosenAbility == ABILITY_WINDRUNNER && speedBoostCooldownTimer<=0) {
                        speedBoostActive = true; speedBoostTimer = 0; speedBoostCooldownTimer = 8000;
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !playerDashing && playerStamina>=playerDashStaminaCost && playerDashCooldown<=0) {
                    playerDashing = true;
                    playerDashTimer = 0;
                    playerStamina -= playerDashStaminaCost;
                    playerDashCooldown = playerDashCooldownTime;
                    float dx=0, dy=0;
                    if (IsKeyDown(KEY_W)) dy=-1; if (IsKeyDown(KEY_S)) dy=1;
                    if (IsKeyDown(KEY_A)) dx=-1; if (IsKeyDown(KEY_D)) dx=1;
                    if (dx!=0||dy!=0) { float l=hypotf(dx,dy); dx/=l; dy/=l; }
                    else { dx=cosf(playerAngle); dy=sinf(playerAngle); }
                    playerDashTimer = 0;
                    playerDashCD = true;
                }
                if (playerDashing) {
                    playerPos.x += moveX*5;
                    playerPos.y += moveY*5;
                    playerDashTimer += dt;
                    if (playerDashTimer >= playerDashDuration) playerDashing = false;
                }
                
                playerPos.x += moveX;
                playerPos.y += moveY;
                for (auto& o : obstacles) {
                    if (circleRect(playerPos, playerR, o.x,o.y,o.w,o.h)) {
                        float cx = clamp(playerPos.x, o.x, o.x+o.w);
                        float cy = clamp(playerPos.y, o.y, o.y+o.h);
                        float dx = playerPos.x - cx;
                        float dy = playerPos.y - cy;
                        float dist = sqrtf(dx*dx+dy*dy);
                        if (dist < playerR) {
                            float pen = playerR - dist;
                            if (dist > 0.01f) { playerPos.x += dx/dist*pen; playerPos.y += dy/dist*pen; }
                            else { playerPos.x += pen; }
                        }
                    }
                }
                playerPos.x = clamp(playerPos.x, 0, WORLD_W);
                playerPos.y = clamp(playerPos.y, 0, WORLD_H);
                
                if (moveX!=0||moveY!=0) playerAngle = atan2f(moveY, moveX);
                
                if (damageFlashTimer>0) damageFlashTimer--;
                if (playerDashCooldown>0) playerDashCooldown -= dt;
                if (playerStamina<playerMaxStamina) playerStamina = std::min(playerMaxStamina, playerStamina+playerStaminaRegen);
                
                playerHp = std::min(playerMaxHp, playerHp + playerRegen*0.01f);
                if (playerShieldMax>0 && playerShieldHp<playerShieldMax)
                    playerShieldHp = std::min(playerShieldMax, playerShieldHp+0.05f);
                
                updateCamera();
                tryFire(dt);

                for (int i=bullets.size()-1; i>=0; i--) {
                    auto& b = bullets[i];
                    if (graphicsMode!=GRAPHICS_LOW && b.trailCount<6) {
                        b.trail[b.trailCount++] = b.pos;
                    }
                    if (b.homing && chosenAbility==ABILITY_BULLMASTER) {
                        Enemy* target = nullptr; float nd=1e9;
                        for (auto& e : enemies) {
                            float d = dist(b.pos, e->pos);
                            if (d<nd) { nd=d; target=e; }
                        }
                        if (target) {
                            Vec2 dir = (target->pos - b.pos).normalized();
                            b.vel.x += (dir.x*b.speedMag - b.vel.x)*0.12f;
                            b.vel.y += (dir.y*b.speedMag - b.vel.y)*0.12f;
                        }
                    }
                    b.pos = b.pos + b.vel;
                    
                    bool hitWall = false;
                    for (auto& o : obstacles) {
                        if (circleRect(b.pos, b.r, o.x,o.y,o.w,o.h)) { hitWall=true; break; }
                    }
                    if (hitWall) { bullets.erase(bullets.begin()+i); continue; }
                    
                    bool bulletHit = false;
                    for (int ei=enemies.size()-1; ei>=0; ei--) {
                        auto& e = enemies[ei];
                        if (dist(b.pos, e->pos) < b.r + e->r) {
                            bulletHit = true;
                            e->takeDamage(playerDamage);
                            if (!b.piercing) { bullets.erase(bullets.begin()+i); }
                            else { b.piercingHits++; if(b.piercingHits>=b.piercingMaxHits) bullets.erase(bullets.begin()+i); }
                            
                            spawnParticles(e->pos, ORANGE, 5);
                            
                            if (e->hp <= 0) {
                                score += 1;
                                playerXp += 8;
                                spawnParticles(e->pos, YELLOW, 10);
                                if (e == boss.obj) boss.obj = nullptr;
                                delete e;
                                enemies.erase(enemies.begin()+ei);
                            }
                            break;
                        }
                    }
                    if (bulletHit) continue;
                    
                    if (b.pos.x<-100||b.pos.x>WORLD_W+100||b.pos.y<-100||b.pos.y>WORLD_H+100)
                        bullets.erase(bullets.begin()+i);
                }
                
                for (int i=enemyBullets.size()-1; i>=0; i--) {
                    auto& b = enemyBullets[i];
                    b.pos = b.pos + b.vel;
                    if (dist(b.pos, playerPos) < b.r + playerR) {
                        int dmg = 10;
                        if (playerShieldHp > 0) {
                            if (dmg <= playerShieldHp) {
                                playerShieldHp -= dmg;
                            } else {
                                dmg -= playerShieldHp;
                                playerShieldHp = 0;
                                playerHp -= dmg;
                            }
                        } else {
                            playerHp -= dmg;
                        }
                        damageFlashTimer = 12;
                        if (playerHp <= 0) { playerHp=0; gamePhase=PHASE_GAMEOVER; damageFlashTimer=0; }
                        enemyBullets.erase(enemyBullets.begin()+i);
                        continue;
                    }
                    if (b.pos.x<-100||b.pos.x>WORLD_W+100||b.pos.y<-100||b.pos.y>WORLD_H+100)
                        enemyBullets.erase(enemyBullets.begin()+i);
                }
                
                if (playerContactCooldown > 0) playerContactCooldown -= dt;
                for (auto& e : enemies) e->update(dt);
                if (playerContactCooldown <= 0) {
                    for (auto& e : enemies) {
                        float d = dist(playerPos, e->pos);
                        if (d < playerR + e->r) {
                            float contactDmg = e->damage * 0.3f;
                            if (playerShieldHp > 0) {
                                if (contactDmg <= playerShieldHp) {
                                    playerShieldHp -= contactDmg;
                                } else {
                                    contactDmg -= playerShieldHp;
                                    playerShieldHp = 0;
                                    playerHp -= contactDmg;
                                }
                            } else {
                                playerHp -= contactDmg;
                            }
                            spawnParticles(playerPos, {255,50,50,255}, 6, 20);
                            playerContactCooldown = 500;
                            damageFlashTimer = 12;
                            if (playerHp <= 0) { playerHp=0; gamePhase=PHASE_GAMEOVER; damageFlashTimer=0; }
                            break;
                        }
                    }
                }
                
                for (int i=particles.size()-1; i>=0; i--) {
                    particles[i].pos = particles[i].pos + particles[i].vel;
                    particles[i].life--;
                    if (particles[i].life<=0) particles.erase(particles.begin()+i);
                }
                if (graphicsMode==GRAPHICS_LOW && particles.size()>60)
                    particles.erase(particles.begin(), particles.begin()+(particles.size()-60));
                else if (graphicsMode==GRAPHICS_MAX && particles.size()>500)
                    particles.erase(particles.begin(), particles.begin()+(particles.size()-500));
                
                if (playerXp >= playerXpNeeded) {
                    playerXp = 0;
                    playerLevel++;
                    playerXpNeeded = playerXpNeeded == 0 ? 50 : (int)(playerXpNeeded * 1.1f);
                    paused = true;
                    gamePhase = PHASE_UPGRADE;
                    std::vector<Upgrade>& pool = (chosenAbility == ABILITY_LASER) ? laserUpgrades : upgrades;
                    bool useRare = !rareUpgrades.empty() && randf()<0.20f && chosenAbility != ABILITY_LASER;
                    std::vector<Upgrade>& finalPool = useRare ? rareUpgrades : pool;
                    upgradePool = &finalPool;
                    int total = (int)finalPool.size();
                    if (total < 1) { upgradePool = &upgrades; total = (int)upgrades.size(); }
                    if (total < 3) {
                        lastUpgradeChoice1 = 0; lastUpgradeChoice2 = total>1?1:0; lastUpgradeChoice3 = 0;
                    } else {
                        lastUpgradeChoice1 = rand()%total;
                        do { lastUpgradeChoice2 = rand()%total; } while (lastUpgradeChoice2 == lastUpgradeChoice1);
                        do { lastUpgradeChoice3 = rand()%total; } while (lastUpgradeChoice3 == lastUpgradeChoice1 || lastUpgradeChoice3 == lastUpgradeChoice2);
                    }
                    upgradePickCount = 0;
                    upgradeCycleTimer = 0;
                    upgradeCycleFinished = false;
                    upgradeDisplay1 = lastUpgradeChoice1;
                    upgradeDisplay2 = lastUpgradeChoice2;
                    upgradeDisplay3 = lastUpgradeChoice3;
                }
                
                updateWaveLogic(dt);
                updateMinions(dt);
            }
            
            updateOverdrive(dt);
            updateSpeedBoost(dt);
            updateBlackHoleAbility(dt);
            updateLaser(dt, paused);
            
            if (gamePhase == PHASE_UPGRADE) {
                if (!upgradeCycleFinished) {
                    upgradeCycleTimer += dt;
                    if (upgradeCycleTimer > 2000) {
                        upgradeCycleFinished = true;
                        upgradeDisplay1 = lastUpgradeChoice1;
                        upgradeDisplay2 = lastUpgradeChoice2;
                        upgradeDisplay3 = lastUpgradeChoice3;
                    } else {
                        int cycleSpeed = 80;
                        if (upgradeCycleTimer / cycleSpeed % 2 == 0) {
                            int sz = upgradePool->size();
                            int a = rand()%sz, b = rand()%sz, c = rand()%sz;
                            if (sz >= 3) {
                                while (b == a) b = rand()%sz;
                                while (c == a || c == b) c = rand()%sz;
                            }
                            upgradeDisplay1 = a; upgradeDisplay2 = b; upgradeDisplay3 = c;
                        }
                    }
                }
                Vector2 mp = GetMousePosition();
                int boxW = 320, boxH = 100;
                int startX = SCREEN_W/2 - (boxW*3 + 30)/2;
                int boxY = 210;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    for (int i=0; i<3; i++) {
                        int bx = startX + i*(boxW+15);
                        if (mp.x>=bx && mp.x<=bx+boxW && mp.y>=boxY && mp.y<=boxY+boxH) {
                            int choice = i==0?lastUpgradeChoice1:(i==1?lastUpgradeChoice2:lastUpgradeChoice3);
                            if (choice>=0 && choice<(int)upgradePool->size()) (*upgradePool)[choice].apply();
                            gamePhase = PHASE_GAME; paused = false;
                            break;
                        }
                    }
                }
                if (IsKeyPressed(KEY_ONE)) {
                    if (lastUpgradeChoice1>=0 && lastUpgradeChoice1<(int)upgradePool->size()) (*upgradePool)[lastUpgradeChoice1].apply();
                    gamePhase = PHASE_GAME; paused = false;
                } else if (IsKeyPressed(KEY_TWO)) {
                    if (lastUpgradeChoice2>=0 && lastUpgradeChoice2<(int)upgradePool->size()) (*upgradePool)[lastUpgradeChoice2].apply();
                    gamePhase = PHASE_GAME; paused = false;
                } else if (IsKeyPressed(KEY_THREE)) {
                    if (lastUpgradeChoice3>=0 && lastUpgradeChoice3<(int)upgradePool->size()) (*upgradePool)[lastUpgradeChoice3].apply();
                    gamePhase = PHASE_GAME; paused = false;
                }
            }
            
            if (playerHp <= 0) {
                gamePhase = PHASE_GAMEOVER;
                gameStarted = false;
            }
        } else if (gamePhase == PHASE_GAMEOVER) {
            if (IsKeyPressed(KEY_SPACE)) {
                gamePhase = PHASE_MENU;
                playerHp = playerMaxHp;
            }
        }
        
        BeginDrawing();
        
        if (gamePhase == PHASE_MENU) {
            drawMenu();
        } else if (gamePhase == PHASE_LOBBY) {
            drawLobby();
            if (devPanelOpen) {
                DrawRectangle(0,0,SCREEN_W,SCREEN_H, {10,5,20,240});
                int y = 60;
                DrawText("PAINEL DEV", SCREEN_W/2-MeasureText("PAINEL DEV", 36)/2, y, 36, COL_GOLD);
                y += 50;
                Vector2 mp = GetMousePosition();
                bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                auto btn = [&](int bx, int by2, int bw, int bh, const char* label) -> bool {
                    if (clicked && mp.x>=bx && mp.x<=bx+bw && mp.y>=by2 && mp.y<=by2+bh)
                        return true;
                    DrawRectangle(bx, by2, bw, bh, {80,40,40,255});
                    DrawText(label, bx+6, by2+2, 14, WHITE);
                    return false;
                };
                int colW = 220, rowH = 30;
                int startX = SCREEN_W/2 - (3*colW)/2;
                for (int i=0; i<11; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int x = startX + col*colW;
                    int y2 = y + row*rowH;
                    const char* name;
                    char valTxt[32];
                    int bx;
                    switch (i) {
                        case 0: name="Vida Max"; snprintf(valTxt,32,"%.0f",playerMaxHp);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerMaxHp>20) playerMaxHp-=25;
                            if (btn(bx+22,y2,20,20,"+") && playerMaxHp<500) playerMaxHp+=25;
                            break;
                        case 1: name="Dano"; snprintf(valTxt,32,"%.0f",playerDamage);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerDamage>1) playerDamage-=5;
                            if (btn(bx+22,y2,20,20,"+") && playerDamage<200) playerDamage+=5;
                            break;
                        case 2: name="Vel Tiro"; snprintf(valTxt,32,"%.1f",playerBulletSpeed);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerBulletSpeed>1) playerBulletSpeed-=1;
                            if (btn(bx+22,y2,20,20,"+") && playerBulletSpeed<30) playerBulletSpeed+=1;
                            break;
                        case 3: name="Freq Tiro"; snprintf(valTxt,32,"%dms",playerFireRate);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerFireRate>50) playerFireRate-=50;
                            if (btn(bx+22,y2,20,20,"+") && playerFireRate<500) playerFireRate+=50;
                            break;
                        case 4: name="Tiros"; snprintf(valTxt,32,"%d",playerBullets);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerBullets>1) playerBullets-=1;
                            if (btn(bx+22,y2,20,20,"+") && playerBullets<10) playerBullets+=1;
                            break;
                        case 5: name="Escudo"; snprintf(valTxt,32,"%.0f",playerShieldMax);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerShieldMax>0) playerShieldMax-=10;
                            if (btn(bx+22,y2,20,20,"+") && playerShieldMax<300) playerShieldMax+=10;
                            break;
                        case 6: name="Velocidade"; snprintf(valTxt,32,"%.1f",playerBaseSpeed);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerBaseSpeed>0.5f) playerBaseSpeed-=0.5f;
                            if (btn(bx+22,y2,20,20,"+") && playerBaseSpeed<10) playerBaseSpeed+=0.5f;
                            break;
                        case 7: name="Onda Inicial"; snprintf(valTxt,32,"%d",devWaveNumber);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && devWaveNumber>1) devWaveNumber--;
                            if (btn(bx+22,y2,20,20,"+") && devWaveNumber<TOTAL_WAVES) devWaveNumber++;
                            break;
                        case 8: name="Boss"; snprintf(valTxt,32,"%s",devBossEnabled?"Sim":"Nao");
                            bx = x+160;
                            if (btn(bx,y2,60,20,devBossEnabled?"Sim":"Nao")) devBossEnabled = !devBossEnabled;
                            break;
                        case 9: name="Regeneracao"; snprintf(valTxt,32,"%.1f",playerRegen);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && playerRegen>0) playerRegen-=0.5f;
                            if (btn(bx+22,y2,20,20,"+") && playerRegen<20) playerRegen+=0.5f;
                            break;
                        case 10: name="Minions"; snprintf(valTxt,32,"%d",devMinionCount);
                            bx = x+160; if (btn(bx,y2,20,20,"-") && devMinionCount>0) devMinionCount--;
                            if (btn(bx+22,y2,20,20,"+") && devMinionCount<10) devMinionCount++;
                            break;
                    }
                    DrawText(name, x, y2, 14, WHITE);
                    DrawText(valTxt, x+90, y2, 14, COL_GOLD);
                }
                const char* targetNames[] = {"MINI 1","MINI 2","MINI 3","BOSS"};
                int bw = 90, gap = 10;
                int totalW = bw * 4 + gap * 3;
                int btnStartX = SCREEN_W / 2 - totalW / 2;
                for (int i = 0; i < 4; i++) {
                    bool active = devDebugTarget == i;
                    if (btn(btnStartX + i * (bw + gap), SCREEN_H - 100, bw, 25, active ? TextFormat("[%s]", targetNames[i]) : targetNames[i]) && !gameStarted)
                        devDebugTarget = active ? -1 : i;
                }
                playerHp = std::min(playerHp, playerMaxHp);
                playerShieldHp = std::min(playerShieldHp, playerShieldMax);
                DrawText("Pressione D para fechar", SCREEN_W/2-MeasureText("Pressione D para fechar", 16)/2, SCREEN_H-40, 16, {180,180,180,255});
            }
        } else if (gamePhase == PHASE_GAME || gamePhase == PHASE_UPGRADE) {
            drawWorld();
            
            for (auto& b : bullets) {
                float sx=toScreenX(b.pos.x), sy=toScreenY(b.pos.y);
                if (graphicsMode!=GRAPHICS_LOW && b.trailCount>0) {
                    for (int t=0; t<b.trailCount; t++) {
                        float alpha = (t+1.0f)/b.trailCount*0.3f;
                        float rad = b.r*(t+1.0f)/b.trailCount*0.6f;
                        Color tc = b.tethered?Color{100,255,200,255}:(b.piercing?RED:(b.homing?COL_HOMING:COL_BULLET));
                        tc.a = (unsigned char)(alpha*255);
                        float tx=toScreenX(b.trail[t].x), ty=toScreenY(b.trail[t].y);
                        DrawCircle(tx, ty, rad, tc);
                    }
                }
                Color bc = b.piercing?RED:(b.homing?COL_HOMING:COL_BULLET);
                if (b.tethered) {
                    bc = {100,255,200,255};
                    float psx = toScreenX(playerPos.x), psy = toScreenY(playerPos.y);
                    DrawLineEx({sx,sy}, {psx,psy}, 1.5f, {100,255,200,40});
                }
                DrawCircle(sx, sy, b.r, bc);
            }
            for (auto& b : enemyBullets) {
                float sx=toScreenX(b.pos.x), sy=toScreenY(b.pos.y);
                DrawCircle(sx, sy, b.r, RED);
            }
            for (auto& e : enemies) e->draw();
            for (auto& p : particles) {
                float sx=toScreenX(p.pos.x), sy=toScreenY(p.pos.y);
                float alpha = p.life/40.0f;
                Color c = p.color; c.a = (unsigned char)(alpha*255);
                DrawRectangle(sx, sy, 4, 4, c);
            }
            
            if (graphicsMode!=GRAPHICS_LOW) {
                if (playerTrailCount<20) playerTrail[playerTrailCount++] = playerPos;
                else {
                    for (int t=0; t<19; t++) playerTrail[t] = playerTrail[t+1];
                    playerTrail[19] = playerPos;
                }
                for (int t=0; t<playerTrailCount; t++) {
                    float alpha = (t+1.0f)/playerTrailCount*0.2f;
                    float rad = playerR*(t+1.0f)/playerTrailCount*0.5f;
                    float tx = toScreenX(playerTrail[t].x), ty = toScreenY(playerTrail[t].y);
                    Color tc = {42,157,244,(unsigned char)(alpha*255)};
                    DrawCircle(tx, ty, rad, tc);
                }
            }
            
            float psx=toScreenX(playerPos.x), psy=toScreenY(playerPos.y);
            if (playerShieldHp>0) DrawCircleLines(psx, psy, playerR+8, {120,200,255,180});
            if (chosenAbility==ABILITY_OVERDRIVE && overdriveActive) {
                float p = 0.7f+sinf(GetTime()*0.015f)*0.3f;
                DrawCircleLines(psx, psy, playerR+18+sinf(GetTime()*0.01f)*4, {255,255,68,(unsigned char)(p*100)});
            }
            if (chosenAbility==ABILITY_WINDRUNNER && speedBoostActive) {
                float wp = 0.6f+sinf(GetTime()*0.02f)*0.3f;
                DrawCircleLines(psx, psy, playerR+14+sinf(GetTime()*0.012f)*3, {200,255,200,(unsigned char)(wp*90)});
            }
            if (graphicsMode!=GRAPHICS_LOW) {
                Vector2 mouse = GetMousePosition();
                DrawLineEx({psx, psy}, mouse, 1.5f, {42,157,244,30});
            }
            if (chosenAbility == ABILITY_LASER && laserActive) {
                Vector2 mouse = GetMousePosition();
                Vec2 target(mouse.x + camera.x, mouse.y + camera.y);
                float pulse = 0.7f + sinf(GetTime()*0.01f)*0.3f;
                Color core = {255,50,50,(unsigned char)(255*pulse)};
                Color glow = {255,0,0,(unsigned char)(80*pulse)};
                Vec2 endPos = target;
                float ex = toScreenX(endPos.x), ey = toScreenY(endPos.y);
                DrawLineEx({psx, psy}, {ex, ey}, laserWidth*3, glow);
                DrawLineEx({psx, psy}, {ex, ey}, laserWidth, core);
            }
            Color playerCol = damageFlashTimer>0 ? WHITE : Color{42,157,244,255};
            Color innerCol = damageFlashTimer>0 ? Color{255,200,200,255} : Color{155,232,255,255};
            if (graphicsMode!=GRAPHICS_LOW) {
                DrawCircle(psx, psy, playerR+6, {playerCol.r,playerCol.g,playerCol.b,40});
                DrawCircle(psx, psy, playerR+12, {playerCol.r,playerCol.g,playerCol.b,15});
            }
            DrawCircle(psx, psy, playerR, playerCol);
            DrawCircle(psx-4, psy-4, playerR*0.45f, innerCol);
            
            for (auto& m : minions) {
                float mx = toScreenX(playerPos.x + cosf(m.angle)*m.orbitDist);
                float my = toScreenY(playerPos.y + sinf(m.angle)*m.orbitDist);
                if (m.shootCooldown <= 100) {
                    DrawLineEx({mx,my}, {psx,psy}, 1.5f, {m.color.r,m.color.g,m.color.b,60});
                }
                DrawCircle(mx, my, 8, m.color);
                DrawCircle(mx-2, my-2, 3, {255,255,255,128});
            }
            
            for (auto& bh : blackholes) {
                float bx=toScreenX(bh.pos.x), by=toScreenY(bh.pos.y);
                float prog = (float)bh.timer/BH_DURATION;
                float rad = BH_RADIUS*(0.3f+prog*0.7f);
                if (graphicsMode!=GRAPHICS_LOW) {
                    DrawCircleLines(bx, by, rad, {120,0,255,(unsigned char)(80+sinf(prog*10)*25)});
                    DrawCircle(bx, by, rad*0.7f, {60,0,120,(unsigned char)(50*(1-prog))});
                } else {
                    DrawCircleLines(bx, by, rad, {120,0,255,100});
                }
            }
            
            drawHUD();
            drawMinimap();

            if (gamePhase == PHASE_UPGRADE) {
                drawUpgradeMenu();
            }
            
            if (paused && gamePhase == PHASE_GAME) {
                drawPauseMenu();
            }
        } else if (gamePhase == PHASE_GAMEOVER) {
            ClearBackground({10,5,20,255});
            for (int i=0; i<15; i++) {
                float px = (sinf(GetTime()*0.002f*(i%7+2)+i)*0.5f+0.5f)*SCREEN_W;
                float py = (cosf(GetTime()*0.0015f*(i%5+3)+i*2)*0.5f+0.5f)*SCREEN_H;
                DrawCircle(px, py, 2, {255,(unsigned char)(50+i*8),(unsigned char)(30+i*4),40});
            }
            float goPulse = 0.8f+sinf(GetTime()*0.003f)*0.2f;
            Color goCol = {255,50,50,(unsigned char)(255*goPulse)};
            int goW = MeasureText("GAME OVER", 50);
            DrawText("GAME OVER", SCREEN_W/2-goW/2, 180, 50, goCol);
            DrawRectangle(SCREEN_W/2-goW/2, 235, goW, 2, Color{255,50,50,100});
            
            Color scoreCol = COL_GOLD;
            DrawText(TextFormat("Pontuacao: %d", score), SCREEN_W/2-MeasureText(TextFormat("Pontuacao: %d", score), 28)/2, 270, 28, scoreCol);
            DrawText(TextFormat("Nivel alcancado: %d", playerLevel), SCREEN_W/2-MeasureText(TextFormat("Nivel alcancado: %d", playerLevel), 18)/2, 310, 18, {180,180,180,255});
            
            float ba = 0.4f+sinf(GetTime()*0.003f)*0.4f;
            Color hintC = {180,180,200,(unsigned char)(ba*255)};
            DrawText("ESPACO para voltar ao menu", SCREEN_W/2-MeasureText("ESPACO para voltar ao menu", 18)/2, 380, 18, hintC);
        }
        
        if (damageFlashTimer > 0 && (gamePhase == PHASE_GAME || gamePhase == PHASE_UPGRADE)) {
            float alpha = (damageFlashTimer / 12.0f) * 50;
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, {255, 0, 0, (unsigned char)alpha});
        }
        
        EndDrawing();
    }
    
    for (auto& e : enemies) delete e;
    for (auto& s : sounds) UnloadSound(s.second);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
