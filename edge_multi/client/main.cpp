#include <iostream>
#include <cstring>
#include <cstdio>
#include <enet/enet.h>
#include "raylib.h"
#include "client/renderer.h"
#include "common/net.h"

static ENetPeer* peer = nullptr;
static RenderState renderState;
static uint16_t inputSeq = 0;

static bool parseSnapshot(const uint8_t* data, size_t len, SnapshotPacket& snap) {
    const uint8_t* p = data;
    p++; // skip type
    snap.seq = readU16(p);
    snap.playerCount = readU8(p);
    for (int i = 0; i < snap.playerCount && i < 8; i++) {
        auto& pl = snap.players[i];
        pl.id = readU8(p);
        pl.x = readFloat(p); pl.y = readFloat(p);
        pl.hp = readFloat(p); pl.maxHp = readFloat(p);
        pl.shieldHp = readFloat(p); pl.shieldMax = readFloat(p);
        pl.level = readU8(p); pl.xp = readU16(p); pl.xpNeeded = readU16(p);
        pl.stamina = readFloat(p); pl.maxStamina = readFloat(p);
        pl.bulletCount = readU8(p); pl.fireRate = readFloat(p);
    }
    snap.enemyCount = readU16(p);
    for (int i = 0; i < snap.enemyCount && i < 256; i++) {
        auto& e = snap.enemies[i];
        e.id = readU16(p); e.type = readU8(p);
        e.x = readFloat(p); e.y = readFloat(p);
        e.hp = readFloat(p); e.maxHp = readFloat(p);
        e.shieldHp = readFloat(p); e.shieldMax = readFloat(p);
        e.r = readFloat(p);
        e.r_ = readU8(p); e.g_ = readU8(p); e.b_ = readU8(p);
    }
    snap.bulletCount = readU16(p);
    for (int i = 0; i < snap.bulletCount && i < 512; i++) {
        auto& b = snap.bullets[i];
        b.id = readU16(p);
        b.x = readFloat(p); b.y = readFloat(p);
        b.r = readFloat(p);
        b.r_ = readU8(p); b.g_ = readU8(p); b.b_ = readU8(p);
        b.enemy = readU8(p) != 0;
    }
    snap.waveNumber = readU8(p); snap.totalWaves = readU8(p);
    snap.score = readU16(p);
    snap.gameStarted = readU8(p) != 0;
    snap.gameOver = readU8(p) != 0;
    snap.bossPhaseIndex = readU8(p);
    snap.bossHp = readFloat(p); snap.bossMaxHp = readFloat(p);
    snap.minibossesLeft = readU8(p);
    return true;
}

static void sendInput() {
    if (!peer) return;
    InputPacket in;
    in.seq = inputSeq++;
    in.up = IsKeyDown(KEY_W);
    in.down = IsKeyDown(KEY_S);
    in.left = IsKeyDown(KEY_A);
    in.right = IsKeyDown(KEY_D);
    in.shoot = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.dash = IsKeyDown(KEY_SPACE);

    Vector2 mp = GetMousePosition();
    // convert to world-space aim direction
    float wx = mp.x - GetScreenWidth()/2;
    float wy = mp.y - GetScreenHeight()/2;
    float wl = sqrtf(wx*wx + wy*wy);
    if (wl > 1) { in.aimX = wx/wl; in.aimY = wy/wl; }
    else { in.aimX = 1; in.aimY = 0; }

    in.upgradeChoice = -1;
    if (IsKeyPressed(KEY_ONE)) in.upgradeChoice = 0;
    else if (IsKeyPressed(KEY_TWO)) in.upgradeChoice = 1;
    else if (IsKeyPressed(KEY_THREE)) in.upgradeChoice = 2;

    std::vector<uint8_t> buf;
    writeU8(buf, C2S_INPUT);
    writeU16(buf, in.seq);
    uint8_t keys = (in.up?1:0) | (in.down?2:0) | (in.left?4:0) | (in.right?8:0) |
                   (in.shoot?16:0) | (in.dash?32:0);
    writeU8(buf, keys);
    writeFloat(buf, in.aimX);
    writeFloat(buf, in.aimY);
    writeU8(buf, (uint8_t)std::max(-1, std::min(2, in.upgradeChoice)));

    ENetPacket* packet = enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(peer, 0, packet);
}

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 25565;

    if (enet_initialize() != 0) {
        std::cerr << "ENet init failed\n";
        return 1;
    }
    atexit(enet_deinitialize);

    ENetHost* client = enet_host_create(nullptr, 1, 1, 0, 0);
    if (!client) {
        std::cerr << "Failed to create client\n";
        return 1;
    }

    ENetAddress address;
    enet_address_set_host(&address, host);
    address.port = port;

    peer = enet_host_connect(client, &address, 1, 0);
    if (!peer) {
        std::cerr << "Failed to connect to " << host << ":" << port << "\n";
        return 1;
    }

    std::cout << "Connecting to " << host << ":" << port << "...\n";

    bool connected = false;
    uint32_t lastSend = 0;

    initRenderer();

    while (!WindowShouldClose()) {
        ENetEvent event;
        while (enet_host_service(client, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    std::cout << "Connected!\n";
                    connected = true;
                    renderState.ready = true;
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (event.packet->dataLength > 0 && event.packet->data[0] == S2C_SNAPSHOT) {
                        parseSnapshot(event.packet->data, event.packet->dataLength, renderState.snap);
                        // find our player index
                        // (simplified: first player is us for now)
                        renderState.playerCount = renderState.snap.playerCount;
                    }
                    enet_packet_destroy(event.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    std::cout << "Disconnected\n";
                    connected = false;
                    break;
                default: break;
            }
        }

        if (connected) {
            uint32_t now = enet_time_get();
            if (now - lastSend >= 16) { // ~60 Hz input
                sendInput();
                enet_host_flush(client);
                lastSend = now;
            }
        }

        drawFrame(renderState);
    }

    if (peer) enet_peer_disconnect(peer, 0);
    enet_host_destroy(client);
    return 0;
}
