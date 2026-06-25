#include "edge_network.h"
#include <enet/enet.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// ---- serialisation helpers ----
static void w8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
static void w16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
static void wf(std::vector<uint8_t>& b, float v) {
    uint32_t uv; memcpy(&uv, &v, 4);
    b.push_back(uv&0xFF); b.push_back((uv>>8)&0xFF);
    b.push_back((uv>>16)&0xFF); b.push_back((uv>>24)&0xFF);
}
static uint8_t r8(const uint8_t*& p) { return *p++; }
static uint16_t r16(const uint8_t*& p) { uint16_t v=p[0]|(p[1]<<8); p+=2; return v; }
static float rf(const uint8_t*& p) {
    uint32_t uv=p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24); p+=4; float v; memcpy(&v,&uv,4); return v;
}

// ---- state ----
static ENetHost* gServer = nullptr;
static ENetHost* gClient = nullptr;
static ENetPeer* gPeer = nullptr;
static bool gIsHost = false;
static bool gIsClient = false;
static bool gConnected = false;
static uint8_t gLocalPlayerId = 0;
static std::vector<ENetPeer*> gClients;
static Snapshot gLastSnap;
static bool gHasSnap = false;
static bool gClientSentInput = false;

bool netInit() {
    if (enet_initialize() != 0) return false;
    atexit(enet_deinitialize);
    return true;
}

void netCleanup() {
    netDisconnect();
    if (gServer) { enet_host_destroy(gServer); gServer = nullptr; }
    if (gClient) { enet_host_destroy(gClient); gClient = nullptr; }
    gIsHost = gIsClient = gConnected = false;
    gClients.clear();
}

bool netHostStart(int port) {
    netCleanup();
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    gServer = enet_host_create(&addr, 8, 1, 0, 0);
    if (!gServer) return false;
    gIsHost = true;
    gConnected = true;
    gLocalPlayerId = 0;
    return true;
}

bool netConnect(const char* host, int port) {
    netCleanup();
    gClient = enet_host_create(nullptr, 1, 1, 0, 0);
    if (!gClient) return false;
    ENetAddress addr;
    enet_address_set_host(&addr, host);
    addr.port = port;
    gPeer = enet_host_connect(gClient, &addr, 1, 0);
    if (!gPeer) return false;
    gIsClient = true;
    return true;
}

void netDisconnect() {
    if (gPeer) { enet_peer_disconnect(gPeer, 0); gPeer = nullptr; }
    for (auto* c : gClients) enet_peer_disconnect(c, 0);
    gClients.clear();
    gConnected = false;
    gIsHost = gIsClient = false;
}

bool netIsHost() { return gIsHost; }
bool netIsClient() { return gIsClient; }
bool netIsConnected() { return gConnected; }
int  netGetClientCount() { return (int)gClients.size(); }
void netSetLocalPlayerId(uint8_t id) { gLocalPlayerId = id; }
uint8_t netGetLocalPlayerId() { return gLocalPlayerId; }
bool netClientSentInput() { bool v = gClientSentInput; gClientSentInput = false; return v; }

void netTick() {
    if (gServer) {
        ENetEvent ev;
        while (enet_host_service(gServer, &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                uint8_t id = 1 + (uint8_t)gClients.size();
                ev.peer->data = (void*)(uintptr_t)id;
                gClients.push_back(ev.peer);
                printf("Cliente %d conectou\n", id);
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                // client sent input
                if (ev.packet->dataLength > 0 && ev.packet->data[0] == PKT_INPUT) {
                    gClientSentInput = true;
                    // store for use in game update TODO
                }
                enet_packet_destroy(ev.packet);
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                auto it = std::find(gClients.begin(), gClients.end(), ev.peer);
                if (it != gClients.end()) gClients.erase(it);
                printf("Cliente desconectou\n");
            }
        }
    }
    if (gClient) {
        ENetEvent ev;
        while (enet_host_service(gClient, &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                gConnected = true;
                printf("Conectado ao servidor!\n");
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                if (ev.packet->dataLength > 0 && ev.packet->data[0] == PKT_SNAPSHOT) {
                    const uint8_t* p = ev.packet->data + 1;
                    gLastSnap.seq = r16(p);
                    gLastSnap.playerCount = r8(p);
                    for (int i = 0; i < gLastSnap.playerCount && i < 8; i++) {
                        auto& pl = gLastSnap.players[i];
                        pl.id = r8(p); pl.x = rf(p); pl.y = rf(p);
                        pl.hp = rf(p); pl.maxHp = rf(p);
                        pl.shieldHp = rf(p); pl.shieldMax = rf(p);
                        pl.level = r8(p);
                    }
                    gLastSnap.enemyCount = r16(p);
                    for (int i = 0; i < gLastSnap.enemyCount && i < 256; i++) {
                        auto& e = gLastSnap.enemies[i];
                        e.id = r16(p); e.type = r8(p);
                        e.x = rf(p); e.y = rf(p);
                        e.hp = rf(p); e.maxHp = rf(p);
                        e.shieldHp = rf(p); e.shieldMax = rf(p);
                        e.r = rf(p);
                        e.r_ = r8(p); e.g_ = r8(p); e.b_ = r8(p);
                    }
                    gLastSnap.bulletCount = r16(p);
                    for (int i = 0; i < gLastSnap.bulletCount && i < 512; i++) {
                        auto& b = gLastSnap.bullets[i];
                        b.x = rf(p); b.y = rf(p); b.r = rf(p);
                        b.enemy = r8(p) != 0;
                    }
                    gLastSnap.waveNumber = r8(p);
                    gLastSnap.score = r16(p);
                    gLastSnap.gameOver = r8(p) != 0;
                    gLastSnap.bossHp = rf(p);
                    gLastSnap.bossMaxHp = rf(p);
                    gLastSnap.bossPhaseIndex = r8(p);
                    gHasSnap = true;
                }
                enet_packet_destroy(ev.packet);
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                gConnected = false;
                printf("Desconectado do servidor\n");
            }
        }
    }
}

void netSendInput(const InputPacket& inp) {
    if (!gPeer || !gConnected) return;
    std::vector<uint8_t> buf;
    w8(buf, PKT_INPUT);
    w16(buf, inp.seq);
    uint8_t k = (inp.up?1:0)|(inp.down?2:0)|(inp.left?4:0)|(inp.right?8:0)|(inp.shoot?16:0);
    w8(buf, k);
    wf(buf, inp.aimX); wf(buf, inp.aimY);
    ENetPacket* pk = enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(gPeer, 0, pk);
    enet_host_flush(gClient);
}

bool netReceiveSnapshot(Snapshot& snap) {
    if (!gHasSnap) return false;
    snap = gLastSnap;
    gHasSnap = false;
    return true;
}

void netBroadcastSnapshot(const Snapshot& snap) {
    if (!gServer) return;
    std::vector<uint8_t> buf;
    w8(buf, PKT_SNAPSHOT);
    w16(buf, snap.seq);
    w8(buf, snap.playerCount);
    for (int i = 0; i < snap.playerCount && i < 8; i++) {
        auto& p = snap.players[i];
        w8(buf, p.id); wf(buf, p.x); wf(buf, p.y);
        wf(buf, p.hp); wf(buf, p.maxHp);
        wf(buf, p.shieldHp); wf(buf, p.shieldMax);
        w8(buf, p.level);
    }
    w16(buf, snap.enemyCount);
    for (int i = 0; i < snap.enemyCount && i < 256; i++) {
        auto& e = snap.enemies[i];
        w16(buf, e.id); w8(buf, e.type);
        wf(buf, e.x); wf(buf, e.y);
        wf(buf, e.hp); wf(buf, e.maxHp);
        wf(buf, e.shieldHp); wf(buf, e.shieldMax);
        wf(buf, e.r);
        w8(buf, e.r_); w8(buf, e.g_); w8(buf, e.b_);
    }
    w16(buf, snap.bulletCount);
    for (int i = 0; i < snap.bulletCount && i < 512; i++) {
        auto& b = snap.bullets[i];
        wf(buf, b.x); wf(buf, b.y); wf(buf, b.r);
        w8(buf, b.enemy?1:0);
    }
    w8(buf, snap.waveNumber);
    w16(buf, snap.score);
    w8(buf, snap.gameOver?1:0);
    wf(buf, snap.bossHp); wf(buf, snap.bossMaxHp);
    w8(buf, snap.bossPhaseIndex);

    ENetPacket* pk = enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(gServer, 0, pk);
}
