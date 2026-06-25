#include "renderer.h"
#include <cstdio>

void initRenderer() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_FULLSCREEN_MODE);
    InitWindow(1280, 720, "Edge Extrusion MP");
    SetTargetFPS(60);
}

void drawFrame(RenderState& state) {
    auto& snap = state.snap;

    // follow local player
    if (state.playerCount > 0 && state.localPlayerIndex < snap.playerCount) {
        auto& p = snap.players[state.localPlayerIndex];
        state.camera.target = {p.x, p.y};
        state.camera.offset = {GetScreenWidth()/2.0f, GetScreenHeight()/2.0f};
        state.camera.rotation = 0;
        state.camera.zoom = 1.0f;
    }

    BeginDrawing();
    ClearBackground({10, 10, 20, 255});

    BeginMode2D(state.camera);

    // floor grid
    float gridSize = 100;
    for (float x = fmod(-state.camera.target.x, gridSize); x < GetScreenWidth(); x += gridSize) {
        DrawLineV({x, 0}, {x, (float)GetScreenHeight()}, {20,20,40,255});
    }
    for (float y = fmod(-state.camera.target.y, gridSize); y < GetScreenHeight(); y += gridSize) {
        DrawLineV({0, y}, {(float)GetScreenWidth(), y}, {20,20,40,255});
    }

    // enemies
    for (int i = 0; i < snap.enemyCount; i++) {
        auto& e = snap.enemies[i];
        Color c = {e.r_, e.g_, e.b_, 255};

        Vec2 pos(e.x, e.y);
        Vec2 spos = pos; // already in world coords

        if (e.type <= 4) {
            DrawCircleV({spos.x, spos.y}, e.r, c);
        } else if (e.type <= 7) {
            DrawCircleV({spos.x, spos.y}, e.r, c);
            DrawCircleLines(spos.x, spos.y, e.r+3, (Color){255,255,255,80});
            // shield bar
            if (e.shieldMax > 0) {
                float bw = e.r * 2;
                DrawRectangle(spos.x-bw/2, spos.y-e.r-12, bw, 4, {20,40,80,255});
                float pct = e.shieldMax > 0 ? e.shieldHp/e.shieldMax : 0;
                DrawRectangle(spos.x-bw/2, spos.y-e.r-12, bw*pct, 4, {77,200,255,255});
            }
        } else if (e.type == 8) {
            float heartbeat = 0.85f + sinf(GetTime()*0.008f)*0.15f;
            float pulseRad = e.r * heartbeat;
            DrawCircleV({spos.x, spos.y}, pulseRad, c);
            DrawCircleV({spos.x, spos.y}, e.r*0.35f*heartbeat, {255,34,102,255});
            for (int j = 0; j < 4; j++) {
                Color ring = j <= snap.bossPhaseIndex ? (Color){255,77,255,255} : (Color){255,255,255,38};
                DrawRing({spos.x, spos.y}, e.r+10, e.r+14, 90*j+5, 90*(j+1)-5, 0, ring);
            }
        }

        // HP bar
        if (e.hp < e.maxHp) {
            float bw = e.r * 2;
            DrawRectangle(spos.x-bw/2, spos.y-e.r-8, bw, 4, {20,20,20,255});
            DrawRectangle(spos.x-bw/2, spos.y-e.r-8, bw*(e.hp/e.maxHp), 4, (e.type==8)?(Color){169,0,255,255} : RED);
        }
    }

    // bullets
    for (int i = 0; i < snap.bulletCount; i++) {
        auto& b = snap.bullets[i];
        Color c = b.enemy ? (Color){255,100,100,255} : (Color){80,200,255,255};
        DrawCircleV({b.x, b.y}, b.r, c);
    }

    // players
    for (int i = 0; i < snap.playerCount; i++) {
        auto& p = snap.players[i];
        Color c = (i == state.localPlayerIndex) ? GREEN : (Color){100,200,255,255};
        DrawCircleV({p.x, p.y}, 12, c);
        DrawCircleV({p.x, p.y}, 8, (Color){255,255,255,200});
        if (p.shieldMax > 0) {
            DrawCircleLines(p.x, p.y, 16, {120,200,255,180});
        }
        // HP bar
        float bw = 30;
        DrawRectangle(p.x-bw/2, p.y-24, bw, 4, {20,20,20,255});
        DrawRectangle(p.x-bw/2, p.y-24, bw*(p.hp/p.maxHp), 4, GREEN);
    }

    EndMode2D();

    // HUD
    if (snap.playerCount > 0 && state.localPlayerIndex < snap.playerCount) {
        auto& p = snap.players[state.localPlayerIndex];
        char buf[256];
        snprintf(buf, sizeof(buf), "HP: %.0f/%.0f  LVL %d  XP: %d/%d  Onda %d/%d",
                 p.hp, p.maxHp, p.level, p.xp, p.xpNeeded, snap.waveNumber, snap.totalWaves);
        DrawText(buf, 10, 10, 16, WHITE);
        snprintf(buf, sizeof(buf), "Score: %d  Players: %d",
                 snap.score, snap.playerCount);
        DrawText(buf, 10, 30, 16, WHITE);
    }

    if (snap.gameOver) {
        const char* txt = "GAME OVER";
        int tw = MeasureText(txt, 48);
        DrawText(txt, GetScreenWidth()/2-tw/2, GetScreenHeight()/2-24, 48, RED);
    }

    // upgrade choice indicator
    if (snap.playerCount > 0 && state.localPlayerIndex < snap.playerCount) {
        auto& p = snap.players[state.localPlayerIndex];
        if (p.xp >= p.xpNeeded && p.xpNeeded > 0) {
            const char* txt = "APERTE 1-3 PARA ESCOLHER UPGRADE";
            int tw = MeasureText(txt, 20);
            DrawText(txt, GetScreenWidth()/2-tw/2, GetScreenHeight()/2-40, 20, GOLD);
        }
    }

    EndDrawing();
}
