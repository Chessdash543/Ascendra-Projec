#pragma once
#include "raylib.h"
#include "common/net.h"
#include "common/math.h"

struct RenderState {
    SnapshotPacket snap;
    int playerCount;
    int localPlayerIndex = 0;
    bool ready = false;
    Camera2D camera;
};

void initRenderer();
void drawFrame(RenderState& state);
