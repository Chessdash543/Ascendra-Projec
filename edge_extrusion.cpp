// Edge Extrusion — C++ / Raylib port
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

// ----------------------------------------------------------------
// CONSTANTES
// ----------------------------------------------------------------
const int SCREEN_W = 1280;
const int SCREEN_H = 720;
const int WORLD_W = 2600;
const int WORLD_H = 1800;
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
const char* BOSS_NAME = "REI CARMESIM, O DEVORADOR";

// ----------------------------------------------------------------
// AUXILIAR
// ----------------------------------------------------------------
static inline float randf() { return (float)rand() / (float)RAND_MAX; }
static inline float randRange(float a, float b) { return a + randf() * (b - a); }
static inline float lerp(float a, float b, float t) { return a + (b-a)*t; }
static inline float clamp(float v, float mn, float mx) { return v<mn?mn:(v>mx?mx:v); }

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
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

// ----------------------------------------------------------------
// CORES DO JOGO
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// ENUMS / TYPES
// ----------------------------------------------------------------
enum EnemyType {
    ENEMY_BALL, ENEMY_TRIANGLE, ENEMY_SQUARE, ENEMY_SNIPER_COMMON,
    ENEMY_RUNNER, ENEMY_MINIBOSS_SNIPER, ENEMY_MINIBOSS_BLINKER,
    ENEMY_MINIBOSS_DASHER, ENEMY_BOSS
};
enum AbilityID {
    ABILITY_NONE=-1, ABILITY_BULLMASTER, ABILITY_WINDRUNNER,
    ABILITY_PIERCING, ABILITY_BLACKHOLE, ABILITY_OVERDRIVE
};
enum GamePhase { PHASE_MENU, PHASE_LOBBY, PHASE_GAME, PHASE_UPGRADE, PHASE_GAMEOVER };
enum WavePhase { WAVE_NORMAL, WAVE_MINIBOSS, WAVE_BOSS_INTRO, WAVE_BOSS, WAVE_CLEARED };
enum GraphicsMode { GRAPHICS_MAX, GRAPHICS_LOW };

// ----------------------------------------------------------------
// ESTRUTURAS
// ----------------------------------------------------------------
struct Particle {
    Vec2 pos, vel;
    int life;
    Color color;
};

struct Bullet {
    Vec2 pos, vel;
    float r, speedMag;
    bool homing, piercing;
    int piercingHits, piercingMaxHits;
    std::vector<Vec2> trail;
    bool active;
    
    Bullet() : r(5), speedMag(0), homing(false), piercing(false),
               piercingHits(0), piercingMaxHits(2), active(true) {}
};

struct Minion {
    float angle, orbitDist, orbitSpeed;
    int shootTimer, shootCooldown;
    float damage;
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
     hexToCol(0x2e3548), hexToCol(0x5a6a9a), 0, hexToCol(0xaa5555), 5},
    {"Pantano Venenoso", hexToCol(0x10201a), {140,255,170,10}, hexToCol(0x1f5c3f),
     hexToCol(0x16331f), hexToCol(0x3fae6e), 1, hexToCol(0x66ff66), 6},
    {"Caverna Carmesim", hexToCol(0x220e10), {255,100,100,11}, hexToCol(0x5c1f24),
     hexToCol(0x3a1518), hexToCol(0xb03a3a), 2, hexToCol(0xff4400), 4}
};

// ----------------------------------------------------------------
// UPGRADES
// ----------------------------------------------------------------
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
static int playerLevel=1, playerXp=0, playerXpNeeded=30;
static int playerScore=0;
static bool playerDashing=false, playerDashCD=false;
static int playerDashTimer=0, playerDashDuration=150, playerDashCooldown=0, playerDashCooldownTime=1500;
static float playerStamina=100, playerMaxStamina=100, playerStaminaRegen=0.15f;
static float playerDashStaminaCost=30;
static bool overdriveActive=false, overdriveTimer=0;
static int overdriveDuration=2000, overdriveCooldown=20000, overdriveCooldownTimer=0;
static bool speedBoostActive=false;
static int speedBoostTimer=0, speedBoostDuration=2000, speedBoostCooldown=8000, speedBoostCooldownTimer=0;
static bool piercingNext=false;
static int shotCount=0, bhTimer=0;
static bool atropelamento=false;
static Vec2 playerPos(WORLD_W/2, WORLD_H/2);
static float playerAngle=0;
static int playerR=20;
static int idleTimer=0;
static float screenShakeTime=0, screenShakeMag=0;
static int damageFlashTimer=0;

static std::vector<Bullet> bullets;
static std::vector<Bullet> enemyBullets;
static std::vector<Particle> particles;
static std::vector<Shockwave> shockwaves;
static std::vector<Bomb> bombs;
static std::vector<BlackHole> blackholes;
static std::vector<Minion> minions;

static GraphicsMode graphicsMode = GRAPHICS_MAX;

// ----------------------------------------------------------------
// SOUND (procedural simples com Wave sintetica)
// ----------------------------------------------------------------
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
    w.sampleCount = samples;
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
    w.data = data; w.sampleCount = samples;
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

// ----------------------------------------------------------------
// PARTICLES
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// COLLISION
// ----------------------------------------------------------------
static bool circleRect(Vec2 c, float cr, float rx, float ry, float rw, float rh) {
    float nx = std::max(rx, std::min(c.x, rx+rw));
    float ny = std::max(ry, std::min(c.y, ry+rh));
    float dx = c.x-nx, dy = c.y-ny;
    return (dx*dx+dy*dy) < (cr*cr);
}

// ----------------------------------------------------------------
// CAMERA / SCREEN
// ----------------------------------------------------------------
static Vec2 camera(0,0);
static void updateCamera() {
    camera.x = playerPos.x - SCREEN_W/2;
    camera.y = playerPos.y - SCREEN_H/2;
    camera.x = clamp(camera.x, 0, WORLD_W-SCREEN_W);
    camera.y = clamp(camera.y, 0, WORLD_H-SCREEN_H);
}
static float toScreenX(float wx) { return wx - camera.x; }
static float toScreenY(float wy) { return wy - camera.y; }

// ----------------------------------------------------------------
// GAME STATE
// ----------------------------------------------------------------
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

static bool gameStarted = false, paused = false;
static GamePhase gamePhase = PHASE_MENU;

// ----------------------------------------------------------------
// ENEMY CLASS
// ----------------------------------------------------------------
class Enemy {
public:
    Vec2 pos;
    float r, maxHp, hp, speed, damage;
    EnemyType type;
    const char* name;
    float angle, pulsePhase;
    int flashTime;
    
    // specific
    int shootCooldown, shootTimer;
    float preferredDist;
    float shieldRadius, shieldAmount;
    Enemy* shieldLink;
    bool unbreakableShield;
    Enemy* shieldProducer;
    bool chargePhase;
    int chargeTimer, chargeTime;
    bool dashing, dashWarning;
    int dashTimer, dashCooldown, dashDuration;
    float dashDx, dashDy, dashWarningTimer;
    
    // miniboss
    int blinkTimer, blinkCooldown;
    const char* blinkStage;
    float laserAngle;
    int chargeDuration;

    Enemy(Vec2 p, EnemyType t) : pos(p), type(t), angle(0), pulsePhase(randf()*M_PI*2),
        flashTime(0), name(""), shieldLink(nullptr), unbreakableShield(false), shieldProducer(nullptr),
        chargePhase(false), chargeTimer(0), chargeTime(0), dashing(false), dashWarning(false),
        dashTimer(0), dashCooldown(0), dashDuration(0), dashDx(0), dashDy(0), dashWarningTimer(0),
        blinkTimer(0), blinkCooldown(0), blinkStage("moving"), laserAngle(0), chargeDuration(0),
        shootCooldown(0), shootTimer(0), preferredDist(0), shieldRadius(0), shieldAmount(0) {

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
                r=38; maxHp=700*waveScale; speed=0.6f; damage=55; name="ATIRADOR DE ELITE";
                chargeTime=2200; chargeTimer=0;
                break;
            case ENEMY_MINIBOSS_BLINKER:
                r=36; maxHp=600*waveScale; speed=1.4f; damage=20; name="DISRUPTOR FANTASMA";
                blinkCooldown=3500; blinkTimer=0;
                break;
            case ENEMY_MINIBOSS_DASHER:
                r=32; maxHp=500*waveScale; speed=2.8f; damage=38; name="FURIA VELOZ";
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
        hp -= finalDmg;
        flashTime = 8;
    }
    
    void update(int dt) {
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
        
        // slow from squares
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
    
    void draw() {
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
        
        // glow
        if (graphicsMode!=GRAPHICS_LOW) {
            Color glow = color; glow.a = 80;
            DrawCircle(sx, sy, r+6, glow);
        }
        
        DrawCircle(sx, sy, r, color);
        
        // barra de vida
        float bw = r*2, bh = 5;
        DrawRectangle(sx-r, sy-r-14, bw, bh, {58,10,10,255});
        DrawRectangle(sx-r, sy-r-14, bw*(hp/maxHp), bh, {77,255,122,255});
        
        if (name) {
            DrawText(name, sx-MeasureText(name,13)/2, sy-r-28, 13, WHITE);
        }
    }
};

// ----------------------------------------------------------------
// BOSS
// ----------------------------------------------------------------
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
            // apply debuff
        }
        // check phase transition
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
        // movement
        Vec2 toPlayer = playerPos - pos;
        float d = toPlayer.len();
        if (d<1) d=1;
        if (d>200) {
            pos.x += toPlayer.x/d*speed;
            pos.y += toPlayer.y/d*speed;
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
        
        // phase indicator
        for (int i=0; i<4; i++) {
            Color c = i<=boss.bossPhaseIndex ? (Color){255,77,255,255} : (Color){255,255,255,38};
            DrawRing({sx,sy}, r+10, r+14, 90*i+5, 90*(i+1)-5, 0, c);
        }
        // hp bar
        float bw = 200, bh = 12;
        DrawRectangle(sx-bw/2, sy-r-20, bw, bh, {20,20,20,255});
        DrawRectangle(sx-bw/2, sy-r-20, bw*(hp/maxHp), bh, {169,0,255,255});
    }
};

// ----------------------------------------------------------------
// BIOME / OBSTACLES
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// SPAWN
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// WAVE LOGIC
// ----------------------------------------------------------------
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
            wave.number++;
            if (wave.number <= TOTAL_WAVES) {
                startNextWave();
            }
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
    
    if (wave.number < TOTAL_WAVES) {
        wave.number++;
        wave.phase = WAVE_NORMAL;
        wave.enemiesToSpawn = 8 + (int)(wave.number*4.5f);
        wave.spawnTimer = 0;
        wave.spawnInterval = std::max(280, 950 - wave.number*40);
        wave.announcement = TextFormat("Onda %d/%d", wave.number, TOTAL_WAVES);
        wave.announcementTimer = 2500;
        applyBiomeForCurrentWave();
        playSoundName("waveStart");
    } else if (wave.minibossesDefeated < MINIBOSS_COUNT) {
        wave.phase = WAVE_MINIBOSS;
        spawnMiniboss();
    } else {
        // boss fight
        wave.phase = WAVE_BOSS_INTRO;
        // create boss
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
    }
}

// ----------------------------------------------------------------
// BLACK HOLE
// ----------------------------------------------------------------
static void updateBlackHoleAbility(int dt) {
    if (chosenAbility != ABILITY_BLACKHOLE) return;
    for (int i=blackholes.size()-1; i>=0; i--) {
        auto& bh = blackholes[i];
        bh.timer += dt;
        if (bh.timer >= BH_DURATION) { blackholes.erase(blackholes.begin()+i); continue; }
        for (auto& e : enemies) {
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
                    // remove from enemies
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

// ----------------------------------------------------------------
// OVERDRIVE
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// SPEED BOOST (Windrunner)
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// MINIONS
// ----------------------------------------------------------------
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
            if (nearest) {
                Vec2 dir = (nearest->pos - targetPos).normalized();
                Bullet b;
                b.pos = targetPos;
                b.vel = dir * playerBulletSpeed;
                b.speedMag = playerBulletSpeed;
                b.r = 4;
                bullets.push_back(b);
                spawnParticle(targetPos, {255,102,255,255}, 3);
            }
        }
    }
}

// ----------------------------------------------------------------
// PLAYER COMBAT
// ----------------------------------------------------------------
static void tryFire(int dt) {
    static int fireTimer = 0;
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

// ----------------------------------------------------------------
// UPGRADES
// ----------------------------------------------------------------
std::vector<Upgrade> upgrades;
std::vector<Upgrade> rareUpgrades;
static bool devModeUnlocked = false;

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
        {"Mini-Seguidor", "Orbe que atira em inimigos.", []{
            Minion m; m.angle=0; m.orbitDist=60; m.orbitSpeed=0.03f;
            m.shootTimer=0; m.shootCooldown=800;
            m.damage=playerDamage*0.6f; m.color={255,102,255,255};
            minions.push_back(m);
        }}
    };
}

static void applyDevAttributes() {
    // Read from dev panel sliders (global vars)
    // For simplicity, dev mode just boosts stats
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

// ----------------------------------------------------------------
// DRAW HELPERS
// ----------------------------------------------------------------
static void drawWorld() {
    const Biome& b = biomes[currentBiomeIndex];
    ClearBackground(b.bg);
    
    // grid
    int gs = 80;
    float startX = -fmodf(camera.x, gs);
    float startY = -fmodf(camera.y, gs);
    Color gridCol = b.grid; gridCol.a = 30;
    for (float x=startX; x<SCREEN_W; x+=gs)
        DrawLine(x, 0, x, SCREEN_H, gridCol);
    for (float y=startY; y<SCREEN_H; y+=gs)
        DrawLine(0, y, SCREEN_W, y, gridCol);
    
    // decorations
    if (graphicsMode != GRAPHICS_LOW) {
        for (auto& d : decorations) {
            float sx=toScreenX(d.x), sy=toScreenY(d.y);
            if (sx<-20||sx>SCREEN_W+20||sy<-20||sy>SCREEN_H+20) continue;
            DrawCircle(sx, sy, 3, b.decorColor);
        }
    }
    
    // unique elements
    if (graphicsMode != GRAPHICS_LOW) {
        for (auto& u : uniqueElements) {
            float sx=toScreenX(u.pos.x), sy=toScreenY(u.pos.y);
            if (sx<-60||sx>SCREEN_W+60||sy<-60||sy>SCREEN_H+60) continue;
            DrawCircle(sx, sy, u.r, u.color);
        }
    }
    
    // obstacles
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

static void drawHUD() {
    // HP bar
    DrawText(TextFormat("Nivel: %d | XP: %d/%d", playerLevel, playerXp, playerXpNeeded), 12, 10, 18, WHITE);
    DrawRectangle(12, 34, 230, 18, {42,8,8,255});
    DrawRectangle(12, 34, 230*(playerHp/playerMaxHp), 18, COL_HP);
    DrawRectangleLines(12, 34, 230, 18, {110,20,20,255});
    
    if (playerShieldMax > 0) {
        DrawRectangle(12, 56, 230, 8, {40,80,120,76});
        DrawRectangle(12, 56, 230*(playerShieldHp/playerShieldMax), 8, COL_SHIELD);
    }
    
    DrawText(TextFormat("Pontos: %d", score), 12, 68, 18, WHITE);
    
    // stamina
    DrawRectangle(12, 92, 60, 6, {0,0,0,128});
    DrawRectangle(12, 92, 60*(playerStamina/playerMaxStamina), 6, COL_STAMINA);
    
    // wave info
    const char* waveText = "";
    const char* biomeName = biomes[currentBiomeIndex].name;
    if (wave.phase == WAVE_NORMAL)
        waveText = TextFormat("Onda %d/%d | %s", wave.number, TOTAL_WAVES, biomeName);
    else if (wave.phase == WAVE_MINIBOSS)
        waveText = TextFormat("Mini-Chefe | %s", biomeName);
    else if (wave.phase == WAVE_BOSS)
        waveText = TextFormat("CHEFE FINAL | %s", biomeName);
    else if (wave.phase == WAVE_CLEARED)
        waveText = "VITORIA!";
    
    int tw = MeasureText(waveText, 18);
    DrawText(waveText, SCREEN_W - tw - 12, 10, 18, WHITE);
    
    // ability badge
    const char* abText = "";
    switch(chosenAbility) {
        case ABILITY_BULLMASTER: abText = "Bull Master"; break;
        case ABILITY_WINDRUNNER: {
            if (speedBoostActive) abText = "Pes Ligeiros — VENTO";
            else if (speedBoostCooldownTimer>0)
                abText = TextFormat("Pes Ligeiros — %ds", speedBoostCooldownTimer/1000+1);
            else abText = "Pes Ligeiros — PRONTO";
            break;
        }
        case ABILITY_PIERCING: abText = "Atravessamento"; break;
        case ABILITY_BLACKHOLE: abText = "Buraco Negro"; break;
        case ABILITY_OVERDRIVE: {
            if (overdriveActive) abText = "Rajada Explosiva — ATIVO";
            else if (overdriveCooldownTimer>0)
                abText = TextFormat("Rajada Explosiva — %ds", overdriveCooldownTimer/1000+1);
            else abText = "Rajada Explosiva — PRONTO";
            break;
        }
        default: break;
    }
    if (abText[0]) DrawText(abText, 12, 108, 14, {200,200,200,255});
}

static void drawMenu() {
    ClearBackground({10,5,20,255});
    DrawText("EDGE EXTRUSION", SCREEN_W/2-MeasureText("EDGE EXTRUSION", 50)/2, 150, 50, COL_GOLD);
    DrawText("WASD para mover", SCREEN_W/2-MeasureText("WASD para mover", 20)/2, 250, 20, WHITE);
    DrawText("Mire com o mouse", SCREEN_W/2-MeasureText("Mire com o mouse", 20)/2, 280, 20, WHITE);
    DrawText("Sobreviva a 15 ondas e derrote o CHEFAO", SCREEN_W/2-MeasureText("Sobreviva a 15 ondas e derrote o CHEFAO", 20)/2, 310, 20, WHITE);
    DrawText("Pressione ESPACO para continuar", SCREEN_W/2-MeasureText("Pressione ESPACO para continuar", 20)/2, 380, 20, {200,200,200,200});
    
    if (devModeUnlocked) {
        DrawText("MODO DEV DESBLOQUEADO!", SCREEN_W/2-MeasureText("MODO DEV DESBLOQUEADO!", 24)/2, 420, 24, COL_GOLD);
    }
}

static void drawLobby() {
    ClearBackground({15,10,30,255});
    DrawText("ESCOLHA SUA HABILIDADE", SCREEN_W/2-MeasureText("ESCOLHA SUA HABILIDADE", 30)/2, 60, 30, WHITE);
    DrawText("1-5 para selecionar, ESPACO para comecar", SCREEN_W/2-MeasureText("1-5 para selecionar, ESPACO para comecar", 16)/2, 100, 16, {180,180,180,255});
    
    const char* abNames[] = {"Bull Master","Pes Ligeiros","Atravessamento","Buraco Negro","Rajada Explosiva"};
    const char* abIcons[] = {"[Bull]","[Wind]","[Pierce]","[BlackH]","[Overdr]"};
    for (int i=0; i<5; i++) {
        Color c = (int)chosenAbility==i ? COL_GOLD : WHITE;
        DrawText(TextFormat("%d: %s %s", i+1, abIcons[i], abNames[i]), 
                 SCREEN_W/2-200, 150+i*40, 20, c);
    }
    
    if (devModeUnlocked) {
        DrawText("[D] MODO DEV", SCREEN_W/2-MeasureText("[D] MODO DEV", 20)/2, 400, 20, COL_GOLD);
    }
}

static void drawUpgradeMenu() {
    ClearBackground({10,5,20,200});
    DrawText("ESCOLHA UM UPGRADE", SCREEN_W/2-MeasureText("ESCOLHA UM UPGRADE", 30)/2, 200, 30, WHITE);
    // simulated 3 choices
    for (int i=0; i<3; i++) {
        int idx = rand() % upgrades.size();
        DrawText(upgrades[idx].name, SCREEN_W/2-150, 270+i*50, 20, COL_GOLD);
        DrawText(upgrades[idx].desc, SCREEN_W/2-150, 295+i*50, 14, {180,180,180,255});
    }
    DrawText("Pressione 1, 2 ou 3 para escolher", SCREEN_W/2-MeasureText("Pressione 1, 2 ou 3 para escolher", 16)/2, 450, 16, {180,180,180,255});
}

// ----------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------
int main() {
    srand(time(0));
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Edge Extrusion");
    InitAudioDevice();
    SetTargetFPS(60);
    
    initSounds();
    initUpgrades();
    
    // UI state for upgrade selection
    int lastUpgradeChoice1 = -1, lastUpgradeChoice2 = -1, lastUpgradeChoice3 = -1;
    int upgradePickCount = 0;
    
    // dev panel
    bool devPanelOpen = false;
    
    gamePhase = PHASE_MENU;
    
    while (!WindowShouldClose()) {
        int dt = (int)(GetFrameTime()*1000);
        if (dt < 1) dt = 1;
        
        // ---- INPUT ----
        if (gamePhase == PHASE_MENU) {
            if (IsKeyPressed(KEY_SPACE)) {
                gamePhase = PHASE_LOBBY;
                chosenAbility = ABILITY_BULLMASTER;
            }
            if (IsKeyPressed(KEY_D) && devModeUnlocked) {
                // dev panel toggle
            }
        } else if (gamePhase == PHASE_LOBBY) {
            if (IsKeyPressed(KEY_ONE)) chosenAbility = ABILITY_BULLMASTER;
            if (IsKeyPressed(KEY_TWO)) chosenAbility = ABILITY_WINDRUNNER;
            if (IsKeyPressed(KEY_THREE)) chosenAbility = ABILITY_PIERCING;
            if (IsKeyPressed(KEY_FOUR)) chosenAbility = ABILITY_BLACKHOLE;
            if (IsKeyPressed(KEY_FIVE)) chosenAbility = ABILITY_OVERDRIVE;
            if (IsKeyPressed(KEY_D) && devModeUnlocked) {
                devPanelOpen = !devPanelOpen;
            }
            if (IsKeyPressed(KEY_SPACE) && !devPanelOpen) {
                gamePhase = PHASE_GAME;
                gameStarted = true;
                // reset player
                playerPos = Vec2(WORLD_W/2, WORLD_H/2);
                playerHp = playerMaxHp;
                playerShieldHp = playerShieldMax;
                playerStamina = playerMaxStamina;
                playerXp = 0; playerLevel = 1; playerXpNeeded = 30;
                playerBullets = 1; playerDashCooldown = 0;
                overdriveActive = false; speedBoostActive = false;
                piercingNext = false; shotCount = 0; bhTimer = 0;
                bullets.clear(); enemyBullets.clear(); particles.clear();
                shockwaves.clear(); bombs.clear(); blackholes.clear();
                enemies.clear(); minions.clear(); score = 0;
                
                if (devModeUnlocked) applyDevAttributes();
                applyAbilityPassives();
                
                wave.number = 0; wave.phase = WAVE_NORMAL;
                wave.minibossesDefeated = 0; wave.waitingNextWave = false;
                
                startNextWave();
            }
        } else if (gamePhase == PHASE_GAME) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                paused = !paused;
            }
            
            // right click abilities
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (chosenAbility == ABILITY_OVERDRIVE && !overdriveActive && overdriveCooldownTimer<=0) {
                    overdriveActive = true; overdriveTimer = 0;
                }
                if (chosenAbility == ABILITY_WINDRUNNER && !speedBoostActive && speedBoostCooldownTimer<=0) {
                    speedBoostActive = true; speedBoostTimer = 0;
                }
            }
            
            if (!paused) {
                // ---- UPDATE ----
                float moveX=0, moveY=0;
                float mul = 1;
                if (chosenAbility==ABILITY_WINDRUNNER && speedBoostActive) mul = 2.5f;
                
                if (IsKeyDown(KEY_W)) moveY = -playerSpeed*mul;
                if (IsKeyDown(KEY_S)) moveY = playerSpeed*mul;
                if (IsKeyDown(KEY_A)) moveX = -playerSpeed*mul;
                if (IsKeyDown(KEY_D)) moveX = playerSpeed*mul;
                
                // dash
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !playerDashing && playerStamina>=playerDashStaminaCost && playerDashCooldown<=0) {
                    playerDashing = true;
                    playerDashTimer = 0;
                    playerStamina -= playerDashStaminaCost;
                    playerDashCooldown = playerDashCooldownTime;
                    // direction
                    float dx=0, dy=0;
                    if (IsKeyDown(KEY_W)) dy=-1; if (IsKeyDown(KEY_S)) dy=1;
                    if (IsKeyDown(KEY_A)) dx=-1; if (IsKeyDown(KEY_D)) dx=1;
                    if (dx!=0||dy!=0) { float l=hypotf(dx,dy); dx/=l; dy/=l; }
                    else { dx=cosf(playerAngle); dy=sinf(playerAngle); }
                    playerDashTimer = 0;
                    // store dash direction
                    playerDashCD = true;
                }
                if (playerDashing) {
                    // simplified dash
                    playerPos.x += moveX*5;
                    playerPos.y += moveY*5;
                    playerDashTimer += dt;
                    if (playerDashTimer >= playerDashDuration) playerDashing = false;
                }
                
                playerPos.x += moveX;
                playerPos.y += moveY;
                playerPos.x = clamp(playerPos.x, 0, WORLD_W);
                playerPos.y = clamp(playerPos.y, 0, WORLD_H);
                
                if (moveX!=0||moveY!=0) playerAngle = atan2f(moveY, moveX);
                
                if (playerDashCooldown>0) playerDashCooldown -= dt;
                if (playerStamina<playerMaxStamina) playerStamina = std::min(playerMaxStamina, playerStamina+playerStaminaRegen);
                
                // regen
                playerHp = std::min(playerMaxHp, playerHp + playerRegen*0.01f);
                if (playerShieldMax>0 && playerShieldHp<playerShieldMax)
                    playerShieldHp = std::min(playerShieldMax, playerShieldHp+0.05f);
                
                updateCamera();
                updateOverdrive(dt);
                updateSpeedBoost(dt);
                updateMinions(dt);
                updateBlackHoleAbility(dt);
                tryFire(dt);
                
                // update bullets
                for (int i=bullets.size()-1; i>=0; i--) {
                    auto& b = bullets[i];
                    // trail
                    if (graphicsMode!=GRAPHICS_LOW) {
                        b.trail.push_back(b.pos);
                        if (b.trail.size()>6) b.trail.erase(b.trail.begin());
                    }
                    // homing
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
                    
                    // wall collision
                    bool hitWall = false;
                    for (auto& o : obstacles) {
                        if (circleRect(b.pos, b.r, o.x,o.y,o.w,o.h)) { hitWall=true; break; }
                    }
                    if (hitWall) { bullets.erase(bullets.begin()+i); continue; }
                    
                    // enemy collision
                    for (int ei=enemies.size()-1; ei>=0; ei--) {
                        auto& e = enemies[ei];
                        if (dist(b.pos, e->pos) < b.r + e->r) {
                            e->takeDamage(playerDamage);
                            if (!b.piercing) { bullets.erase(bullets.begin()+i); }
                            else { b.piercingHits++; if(b.piercingHits>=b.piercingMaxHits) bullets.erase(bullets.begin()+i); }
                            
                            spawnParticles(e->pos, ORANGE, 5);
                            
                            if (e->hp <= 0) {
                                score += 1;
                                playerXp += 8;
                                spawnParticles(e->pos, YELLOW, 10);
                                delete e;
                                enemies.erase(enemies.begin()+ei);
                            }
                            break;
                        }
                    }
                    
                    // out of bounds
                    if (b.pos.x<-100||b.pos.x>WORLD_W+100||b.pos.y<-100||b.pos.y>WORLD_H+100)
                        bullets.erase(bullets.begin()+i);
                }
                
                // enemy bullets
                for (int i=enemyBullets.size()-1; i>=0; i--) {
                    auto& b = enemyBullets[i];
                    b.pos = b.pos + b.vel;
                    if (dist(b.pos, playerPos) < b.r + playerR) {
                        // apply damage
                        playerHp -= 10;
                        if (playerHp <= 0) { playerHp=0; gamePhase=PHASE_GAMEOVER; }
                        enemyBullets.erase(enemyBullets.begin()+i);
                        continue;
                    }
                    if (b.pos.x<-100||b.pos.x>WORLD_W+100||b.pos.y<-100||b.pos.y>WORLD_H+100)
                        enemyBullets.erase(enemyBullets.begin()+i);
                }
                
                // update enemies
                for (auto& e : enemies) e->update(dt);
                
                // update particles
                for (int i=particles.size()-1; i>=0; i--) {
                    particles[i].pos = particles[i].pos + particles[i].vel;
                    particles[i].life--;
                    if (particles[i].life<=0) particles.erase(particles.begin()+i);
                }
                if (graphicsMode==GRAPHICS_LOW && particles.size()>60)
                    particles.erase(particles.begin(), particles.begin()+(particles.size()-60));
                
                // xp check
                if (playerXp >= playerXpNeeded) {
                    playerXp = 0;
                    playerLevel++;
                    playerXpNeeded = (int)(playerXpNeeded*1.35f);
                    paused = true;
                    gamePhase = PHASE_UPGRADE;
                    // generate choices
                    lastUpgradeChoice1 = rand()%upgrades.size();
                    lastUpgradeChoice2 = rand()%upgrades.size();
                    lastUpgradeChoice3 = rand()%upgrades.size();
                    upgradePickCount = 0;
                }
                
                updateWaveLogic(dt);
            }
            
            // upgrade menu handled in input
            if (gamePhase == PHASE_UPGRADE) {
                if (IsKeyPressed(KEY_ONE)) {
                    if (lastUpgradeChoice1>=0) upgrades[lastUpgradeChoice1].apply();
                    gamePhase = PHASE_GAME; paused = false;
                } else if (IsKeyPressed(KEY_TWO)) {
                    if (lastUpgradeChoice2>=0) upgrades[lastUpgradeChoice2].apply();
                    gamePhase = PHASE_GAME; paused = false;
                } else if (IsKeyPressed(KEY_THREE)) {
                    if (lastUpgradeChoice3>=0) upgrades[lastUpgradeChoice3].apply();
                    gamePhase = PHASE_GAME; paused = false;
                }
            }
            
            // game over
            if (playerHp <= 0) {
                gamePhase = PHASE_GAMEOVER;
                gameStarted = false;
            }
        } else if (gamePhase == PHASE_GAMEOVER) {
            if (IsKeyPressed(KEY_SPACE)) {
                // restart
                gamePhase = PHASE_MENU;
                playerHp = playerMaxHp;
            }
        }
        
        // ---- DRAW ----
        BeginDrawing();
        
        if (gamePhase == PHASE_MENU) {
            drawMenu();
        } else if (gamePhase == PHASE_LOBBY) {
            drawLobby();
            if (devPanelOpen) {
                DrawRectangle(0,0,SCREEN_W,SCREEN_H, {10,5,20,230});
                DrawText("MODO DEV", SCREEN_W/2-MeasureText("MODO DEV", 30)/2, 80, 30, COL_GOLD);
                DrawText("Atributos maximizados ativados!", SCREEN_W/2-MeasureText("Atributos maximizados ativados!", 18)/2, 130, 18, WHITE);
                DrawText("Pressione D para fechar", SCREEN_W/2-MeasureText("Pressione D para fechar", 16)/2, 200, 16, {180,180,180,255});
            }
        } else if (gamePhase == PHASE_GAME || gamePhase == PHASE_UPGRADE) {
            drawWorld();
            
            // draw entities
            for (auto& b : bullets) {
                float sx=toScreenX(b.pos.x), sy=toScreenY(b.pos.y);
                // trail
                if (graphicsMode!=GRAPHICS_LOW) {
                    for (int t=0; t<(int)b.trail.size(); t++) {
                        float alpha = (t+1.0f)/b.trail.size()*0.3f;
                        float rad = b.r*(t+1.0f)/b.trail.size()*0.6f;
                        Color tc = b.piercing?RED:(b.homing?COL_HOMING:COL_BULLET);
                        tc.a = (unsigned char)(alpha*255);
                        float tx=toScreenX(b.trail[t].x), ty=toScreenY(b.trail[t].y);
                        DrawCircle(tx, ty, rad, tc);
                    }
                }
                Color bc = b.piercing?RED:(b.homing?COL_HOMING:COL_BULLET);
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
            
            // player
            float psx=toScreenX(playerPos.x), psy=toScreenY(playerPos.y);
            // shield visual
            if (playerShieldHp>0) DrawCircleLines(psx, psy, playerR+8, {120,200,255,180});
            // overdrive aura
            if (chosenAbility==ABILITY_OVERDRIVE && overdriveActive) {
                float p = 0.7f+sinf(GetTime()*0.015f)*0.3f;
                DrawCircleLines(psx, psy, playerR+18+sinf(GetTime()*0.01f)*4, {255,255,68,(unsigned char)(p*100)});
            }
            // speed boost aura
            if (chosenAbility==ABILITY_WINDRUNNER && speedBoostActive) {
                float wp = 0.6f+sinf(GetTime()*0.02f)*0.3f;
                DrawCircleLines(psx, psy, playerR+14+sinf(GetTime()*0.012f)*3, {200,255,200,(unsigned char)(wp*90)});
            }
            DrawCircle(psx, psy, playerR, {42,157,244,255});
            DrawCircle(psx-4, psx-4, playerR*0.45f, {155,232,255,255});
            
            // minions
            for (auto& m : minions) {
                float mx = toScreenX(playerPos.x + cosf(m.angle)*m.orbitDist);
                float my = toScreenY(playerPos.y + sinf(m.angle)*m.orbitDist);
                DrawCircle(mx, my, 8, m.color);
                DrawCircle(mx-2, my-2, 3, {255,255,255,128});
            }
            
            // black holes
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
            
            // upgrade menu overlay
            if (gamePhase == PHASE_UPGRADE) {
                drawUpgradeMenu();
            }
            
            // pause overlay
            if (paused && gamePhase == PHASE_GAME) {
                DrawRectangle(0,0,SCREEN_W,SCREEN_H, {0,0,0,180});
                DrawText("PAUSA", SCREEN_W/2-MeasureText("PAUSA", 40)/2, SCREEN_H/2-40, 40, WHITE);
                DrawText("ESC para continuar", SCREEN_W/2-MeasureText("ESC para continuar", 18)/2, SCREEN_H/2+10, 18, {180,180,180,255});
            }
        } else if (gamePhase == PHASE_GAMEOVER) {
            ClearBackground({10,5,20,255});
            DrawText("GAME OVER", SCREEN_W/2-MeasureText("GAME OVER", 50)/2, 200, 50, RED);
            DrawText(TextFormat("Pontuacao: %d", score), SCREEN_W/2-MeasureText(TextFormat("Pontuacao: %d", score), 24)/2, 280, 24, WHITE);
            DrawText("ESPACO para voltar ao menu", SCREEN_W/2-MeasureText("ESPACO para voltar ao menu", 18)/2, 350, 18, {180,180,180,255});
        }
        
        EndDrawing();
    }
    
    // cleanup
    for (auto& e : enemies) delete e;
    for (auto& s : sounds) UnloadSound(s.second);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
