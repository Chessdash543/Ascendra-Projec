#include <iostream>
#include <cstring>
#include <enet/enet.h>
#include "server/world.h"

static World world;
static uint8_t nextPlayerId = 0;

static void broadcastSnapshot(ENetHost* server) {
    static SnapshotPacket snap;
    world.buildSnapshot(snap);

    std::vector<uint8_t> buf;
    writeU8(buf, S2C_SNAPSHOT);
    writeU16(buf, snap.seq);
    writeU8(buf, snap.playerCount);
    for (int i = 0; i < snap.playerCount; i++) {
        auto& p = snap.players[i];
        writeU8(buf, p.id);
        writeFloat(buf, p.x); writeFloat(buf, p.y);
        writeFloat(buf, p.hp); writeFloat(buf, p.maxHp);
        writeFloat(buf, p.shieldHp); writeFloat(buf, p.shieldMax);
        writeU8(buf, p.level); writeU16(buf, p.xp); writeU16(buf, p.xpNeeded);
        writeFloat(buf, p.stamina); writeFloat(buf, p.maxStamina);
        writeU8(buf, p.bulletCount); writeFloat(buf, p.fireRate);
    }
    writeU16(buf, snap.enemyCount);
    for (int i = 0; i < snap.enemyCount; i++) {
        auto& e = snap.enemies[i];
        writeU16(buf, e.id); writeU8(buf, e.type);
        writeFloat(buf, e.x); writeFloat(buf, e.y);
        writeFloat(buf, e.hp); writeFloat(buf, e.maxHp);
        writeFloat(buf, e.shieldHp); writeFloat(buf, e.shieldMax);
        writeFloat(buf, e.r);
        writeU8(buf, e.r_); writeU8(buf, e.g_); writeU8(buf, e.b_);
    }
    writeU16(buf, snap.bulletCount);
    for (int i = 0; i < snap.bulletCount; i++) {
        auto& b = snap.bullets[i];
        writeU16(buf, b.id);
        writeFloat(buf, b.x); writeFloat(buf, b.y);
        writeFloat(buf, b.r);
        writeU8(buf, b.r_); writeU8(buf, b.g_); writeU8(buf, b.b_);
        writeU8(buf, b.enemy ? 1 : 0);
    }
    writeU8(buf, snap.waveNumber); writeU8(buf, snap.totalWaves);
    writeU16(buf, snap.score);
    writeU8(buf, snap.gameStarted ? 1 : 0);
    writeU8(buf, snap.gameOver ? 1 : 0);
    writeU8(buf, snap.bossPhaseIndex);
    writeFloat(buf, snap.bossHp); writeFloat(buf, snap.bossMaxHp);
    writeU8(buf, snap.minibossesLeft);

    ENetPacket* packet = enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(server, 0, packet);
}

static void handleInput(ENetPeer* peer, const uint8_t* data, size_t len) {
    if (len < 8) return;
    const uint8_t* p = data;
    p++; // skip type
    uint16_t seq = readU16(p);
    uint8_t keys = readU8(p);
    InputPacket input;
    input.seq = seq;
    input.up = keys & 1;
    input.down = (keys >> 1) & 1;
    input.left = (keys >> 2) & 1;
    input.right = (keys >> 3) & 1;
    input.shoot = (keys >> 4) & 1;
    input.dash = (keys >> 5) & 1;
    input.aimX = readFloat(p);
    input.aimY = readFloat(p);
    input.upgradeChoice = (int8_t)readU8(p);

    uint8_t playerId = (uintptr_t)peer->data;
    world.applyInput(playerId, input);
}

int main(int argc, char** argv) {
    if (enet_initialize() != 0) {
        std::cerr << "ENet init failed\n";
        return 1;
    }
    atexit(enet_deinitialize);

    int port = argc > 1 ? atoi(argv[1]) : 25565;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    ENetHost* server = enet_host_create(&address, 8, 1, 0, 0);
    if (!server) {
        std::cerr << "Failed to create server on port " << port << "\n";
        return 1;
    }

    std::cout << "Server started on port " << port << "\n";
    world.init();
    world.gameStarted = true;

    uint32_t lastTick = enet_time_get();
    uint32_t lastSnap = enet_time_get();
    int tickAccum = 0;
    const int TICK_MS = 1000 / 60;

    while (true) {
        ENetEvent event;
        while (enet_host_service(server, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    uint8_t id = nextPlayerId++;
                    event.peer->data = (void*)(uintptr_t)id;
                    world.addPlayer(id);
                    std::cout << "Player " << (int)id << " connected\n";
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                    if (event.packet->dataLength > 0 && event.packet->data[0] == C2S_INPUT) {
                        handleInput(event.peer, event.packet->data, event.packet->dataLength);
                    }
                    enet_packet_destroy(event.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT: {
                    uint8_t id = (uintptr_t)event.peer->data;
                    world.removePlayer(id);
                    std::cout << "Player " << (int)id << " disconnected\n";
                    break;
                }
                default: break;
            }
        }

        uint32_t now = enet_time_get();
        tickAccum += (now - lastTick);
        lastTick = now;

        while (tickAccum >= TICK_MS) {
            world.tick(TICK_MS);
            tickAccum -= TICK_MS;
        }

        // snapshots at 20 Hz
        if (now - lastSnap >= 50) {
            broadcastSnapshot(server);
            lastSnap = now;
        }
    }

    enet_host_destroy(server);
    return 0;
}
