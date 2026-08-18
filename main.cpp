// ═══════════════════════════════════════════════════════════════
// OASIS — A Desert Survival Raycasting Game
// Player starts in a house in a vast desert, must find oases
// to replenish thirst before it runs out.  Compiled to WASM
// via Emscripten so it runs in any browser, including phones.
// ═══════════════════════════════════════════════════════════════

#include <emscripten/emscripten.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// ── Configuration ─────────────────────────────────────────────
static const int SCR_W = 640;
static const int SCR_H = 400;
static const int MAP_SZ = 80;          // 80×80 tile world

// ── Tile types ────────────────────────────────────────────────
enum Tile : uint8_t {
    T_EMPTY  = 0,
    T_HOUSE  = 1,
    T_ROCK   = 2,
    T_CACTUS = 3,
    T_OASIS  = 4,
    T_RUIN   = 5,
};

// ── Colour helper (RGBA bytes in memory on little-endian) ─────
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}

static inline uint32_t lerp32(uint32_t c0, uint32_t c1, double t) {
    uint8_t r = (uint8_t)((c0 & 0xFF) + (((c1 & 0xFF) - (c0 & 0xFF)) * t));
    uint8_t g = (uint8_t)(((c0 >> 8) & 0xFF) + ((((c1 >> 8) & 0xFF) - ((c0 >> 8) & 0xFF)) * t));
    uint8_t b = (uint8_t)(((c0 >> 16) & 0xFF) + ((((c1 >> 16) & 0xFF) - ((c0 >> 16) & 0xFF)) * t));
    return rgb(r, g, b);
}

// ── Global state ──────────────────────────────────────────────
static uint8_t  wmap[MAP_SZ][MAP_SZ];
static uint32_t fb[SCR_W * SCR_H];

// Player
static double pX, pY, pA;          // position x/y, angle
static double thirst;
static int    score;

// Input (set from JS each frame)
static float iMX, iMY, iTurn;

// Day/night cycle
static double gameTime;

// ── Walkability check ─────────────────────────────────────────
static bool canWalk(double x, double y) {
    if (x < 0.15 || x >= MAP_SZ - 0.15 || y < 0.15 || y >= MAP_SZ - 0.15) return false;
    uint8_t t = wmap[(int)x][(int)y];
    return t == T_EMPTY || t == T_OASIS;
}

// ── World generation ──────────────────────────────────────────
static void generateWorld() {
    memset(wmap, T_EMPTY, sizeof(wmap));
    srand(42);

    // ── Large rock formations ──
    for (int i = 0; i < 120; i++) {
        int rx = rand() % MAP_SZ;
        int ry = rand() % MAP_SZ;
        int sz = 1 + rand() % 4;
        for (int dx = 0; dx < sz; dx++)
            for (int dy = 0; dy < sz; dy++) {
                int x = rx + dx, y = ry + dy;
                if (x < MAP_SZ && y < MAP_SZ &&
                    (abs(x - MAP_SZ / 2) > 7 || abs(y - MAP_SZ / 2) > 7))
                    wmap[x][y] = T_ROCK;
            }
    }

    // ── Scattered cacti ──
    for (int i = 0; i < 180; i++) {
        int cx = rand() % MAP_SZ, cy = rand() % MAP_SZ;
        if (wmap[cx][cy] == T_EMPTY &&
            (abs(cx - MAP_SZ / 2) > 7 || abs(cy - MAP_SZ / 2) > 7))
            wmap[cx][cy] = T_CACTUS;
    }

    // ── Small stone ruins ──
    for (int i = 0; i < 12; i++) {
        int bx = rand() % (MAP_SZ - 6) + 3;
        int by = rand() % (MAP_SZ - 6) + 3;
        if (abs(bx - MAP_SZ / 2) < 8 && abs(by - MAP_SZ / 2) < 8) continue;
        int bw = 2 + rand() % 2, bh = 2 + rand() % 2;
        for (int dx = 0; dx < bw; dx++)
            for (int dy = 0; dy < bh; dy++)
                if (wmap[bx + dx][by + dy] == T_EMPTY)
                    wmap[bx + dx][by + dy] = T_RUIN;
        // doorway
        wmap[bx + bw / 2][by + bh - 1] = T_EMPTY;
    }

    // ── Oases (10 scattered locations) ──
    int oases[][2] = {
        {12, 12}, {62, 14}, {14, 62}, {64, 62},
        {28, 8},  {8, 28},  {68, 36}, {36, 68},
        {20, 40}, {55, 22}
    };
    for (auto &o : oases)
        for (int dx = 0; dx < 2; dx++)
            for (int dy = 0; dy < 2; dy++) {
                int x = o[0] + dx, y = o[1] + dy;
                if (x < MAP_SZ && y < MAP_SZ && wmap[x][y] == T_EMPTY)
                    wmap[x][y] = T_OASIS;
            }

    // ── The house (centre of map, 11×11 exterior) ──
    int hx = MAP_SZ / 2 - 5, hy = MAP_SZ / 2 - 5;
    for (int i = 0; i < 11; i++) {
        wmap[hx + i][hy]     = T_HOUSE;   // north wall
        wmap[hx + i][hy + 10] = T_HOUSE;  // south wall
        wmap[hx][hy + i]     = T_HOUSE;   // west wall
        wmap[hx + 10][hy + i] = T_HOUSE;  // east wall
    }
    // Door (south wall, centre, double-wide)
    wmap[hx + 4][hy + 10] = T_EMPTY;
    wmap[hx + 5][hy + 10] = T_EMPTY;
    // Windows (north wall)
    wmap[hx + 3][hy] = T_EMPTY;
    wmap[hx + 7][hy] = T_EMPTY;
    // Interior: desk
    wmap[hx + 3][hy + 3] = T_HOUSE;
    wmap[hx + 4][hy + 3] = T_HOUSE;
    // Interior: shelf
    wmap[hx + 7][hy + 3] = T_HOUSE;
    // Interior: bed
    wmap[hx + 2][hy + 7] = T_HOUSE;
    wmap[hx + 3][hy + 7] = T_HOUSE;
    wmap[hx + 2][hy + 8] = T_HOUSE;
    wmap[hx + 3][hy + 8] = T_HOUSE;

    // Player starts inside the house, facing south (toward door)
    pX = hx + 5.5;
    pY = hy + 7.5;
    pA = M_PI / 2;   // facing south
    thirst = 100.0;
    score = 0;
    gameTime = 0;
}

// ── Raycasting renderer ───────────────────────────────────────
static void renderFrame() {
    double fov = M_PI / 3.0;   // 60° field of view

    // Day/night: subtle colour shift based on time
    double dayPhase = fmod(gameTime / 120.0, 1.0);   // 2-minute full cycle
    double brightness = 0.7 + 0.3 * fabs(sin(dayPhase * M_PI));

    // Sky palette
    uint32_t skyTop    = lerp32(rgb(20, 30, 80),  rgb(135, 185, 235), brightness);
    uint32_t skyHoriz  = lerp32(rgb(60, 40, 30),  rgb(215, 190, 150), brightness);

    for (int x = 0; x < SCR_W; x++) {
        double rayA = pA - fov / 2.0 + ((double)x / SCR_W) * fov;
        double rdx = cos(rayA), rdy = sin(rayA);

        // DDA setup
        int mapX = (int)pX, mapY = (int)pY;
        double ddx = (rdx == 0) ? 1e30 : fabs(1.0 / rdx);
        double ddy = (rdy == 0) ? 1e30 : fabs(1.0 / rdy);
        double sdx, sdy;
        int sx, sy;

        if (rdx < 0) { sx = -1; sdx = (pX - mapX) * ddx; }
        else          { sx =  1; sdx = (mapX + 1.0 - pX) * ddx; }
        if (rdy < 0) { sy = -1; sdy = (pY - mapY) * ddy; }
        else          { sy =  1; sdy = (mapY + 1.0 - pY) * ddy; }

        // March
        int side = 0, hit = 0;
        uint8_t tile = 0;
        for (int step = 0; step < 120; step++) {
            if (sdx < sdy) { sdx += ddx; mapX += sx; side = 0; }
            else            { sdy += ddy; mapY += sy; side = 1; }
            if (mapX < 0 || mapX >= MAP_SZ || mapY < 0 || mapY >= MAP_SZ) break;
            tile = wmap[mapX][mapY];
            if (tile != T_EMPTY && tile != T_OASIS) { hit = 1; break; }
        }

        double pd;
        if (hit)
            pd = (side == 0) ? sdx - ddx : sdy - ddy;
        else
            pd = 40.0;
        if (pd < 0.01) pd = 0.01;

        int lineH = (int)(SCR_H / pd);
        int drawTop = -lineH / 2 + SCR_H / 2;
        int drawBot =  lineH / 2 + SCR_H / 2;
        int clampT = drawTop < 0 ? 0 : drawTop;
        int clampB = drawBot >= SCR_H ? SCR_H - 1 : drawBot;

        // Wall colour per tile type
        uint32_t wc;
        if (hit) {
            switch (tile) {
                case T_HOUSE:
                    wc = side ? rgb(175, 155, 115) : rgb(200, 180, 140); break;
                case T_ROCK:
                    wc = side ? rgb(145, 118, 82) : rgb(170, 145, 108); break;
                case T_CACTUS:
                    wc = side ? rgb(28, 100, 28) : rgb(52, 135, 52); break;
                case T_RUIN:
                    wc = side ? rgb(135, 115, 90) : rgb(160, 140, 110); break;
                default:
                    wc = side ? rgb(155, 135, 105) : rgb(180, 160, 128);
            }

            // Distance fog toward desert haze
            double fog = fmin(pd / 22.0, 1.0);
            uint8_t wr = wc & 0xFF, wg = (wc >> 8) & 0xFF, wb = (wc >> 16) & 0xFF;
            wr = (uint8_t)(wr + (215 - wr) * fog);
            wg = (uint8_t)(wg + (192 - wg) * fog);
            wb = (uint8_t)(wb + (155 - wb) * fog);
            wc = rgb((uint8_t)(wr * brightness), (uint8_t)(wg * brightness), (uint8_t)(wb * brightness));
        }

        // ── Draw column ──
        // Sky
        for (int y = 0; y < clampT; y++) {
            double t = (double)y / (SCR_H * 0.5);
            fb[y * SCR_W + x] = lerp32(skyTop, skyHoriz, t);
        }
        // Wall
        for (int y = clampT; y <= clampB; y++)
            fb[y * SCR_W + x] = wc;
        // Floor (desert sand with depth gradient)
        for (int y = clampB + 1; y < SCR_H; y++) {
            double t = (double)(y - SCR_H * 0.5) / (SCR_H * 0.5);
            uint8_t fr = (uint8_t)((230 - 50 * t) * brightness);
            uint8_t fg = (uint8_t)((212 - 50 * t) * brightness);
            uint8_t fb_ = (uint8_t)((168 - 45 * t) * brightness);
            fb[y * SCR_W + x] = rgb(fr, fg, fb_);
        }
    }

    // ── Minimap (top-right corner, 110×110 px, 14 tiles) ──
    int mmS = 110, mmT = 14;
    int mmX0 = SCR_W - mmS - 10, mmY0 = 10;
    double mmScale = (double)mmS / mmT;

    // Background
    for (int y = 0; y < mmS; y++)
        for (int x = 0; x < mmS; x++)
            fb[(mmY0 + y) * SCR_W + mmX0 + x] = rgb(20, 16, 10, 180);

    for (int y = 0; y < mmS; y++) {
        for (int x = 0; x < mmS; x++) {
            double wx = pX - mmT / 2.0 + (double)x / mmScale;
            double wy = pY - mmT / 2.0 + (double)y / mmScale;
            int mx = (int)wx, my = (int)wy;
            if (mx < 0 || mx >= MAP_SZ || my < 0 || my >= MAP_SZ) continue;

            uint32_t c;
            switch (wmap[mx][my]) {
                case T_EMPTY:  c = rgb(205, 188, 152); break;
                case T_HOUSE:  c = rgb(175, 155, 115); break;
                case T_ROCK:   c = rgb(130, 108, 72);  break;
                case T_CACTUS: c = rgb(42, 115, 42);    break;
                case T_OASIS:  c = rgb(55, 130, 225);   break;
                case T_RUIN:   c = rgb(145, 125, 95);   break;
                default:       c = rgb(100, 100, 100);
            }
            fb[(mmY0 + y) * SCR_W + mmX0 + x] = c;
        }
    }

    // Player dot
    int pdx = mmS / 2, pdy = mmS / 2;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 4)
                fb[(mmY0 + pdy + dy) * SCR_W + mmX0 + pdx + dx] = rgb(255, 60, 60);

    // Direction line
    for (int i = 3; i < 12; i++) {
        int lx = pdx + (int)(cos(pA) * i);
        int ly = pdy + (int)(sin(pA) * i);
        if (lx >= 0 && lx < mmS && ly >= 0 && ly < mmS)
            fb[(mmY0 + ly) * SCR_W + mmX0 + lx] = rgb(255, 220, 80);
    }

    // Minimap border
    for (int i = 0; i < mmS; i++) {
        fb[mmY0 * SCR_W + mmX0 + i] = rgb(120, 95, 60);
        fb[(mmY0 + mmS - 1) * SCR_W + mmX0 + i] = rgb(120, 95, 60);
        fb[(mmY0 + i) * SCR_W + mmX0] = rgb(120, 95, 60);
        fb[(mmY0 + i) * SCR_W + mmX0 + mmS - 1] = rgb(120, 95, 60);
    }

    // ── Compass (top centre) ──
    const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int dirIdx = (int)round(pA / (M_PI / 4.0)) & 7;
    // Just draw a small coloured indicator: N = red, others = dim
    // (Full text compass would need a font; we use the minimap + dir line instead)
}

// ── Game update ───────────────────────────────────────────────
static void updateGame(double dt) {
    gameTime += dt;

    double moveSpd = 3.8 * dt;
    double rotSpd  = 2.8 * dt;

    // Rotation
    pA += iTurn * rotSpd;

    // Movement vector (forward/back + strafe)
    double dx = 0, dy = 0;
    dx += cos(pA) * iMY * moveSpd;
    dy += sin(pA) * iMY * moveSpd;
    dx += cos(pA + M_PI / 2) * iMX * moveSpd;
    dy += sin(pA + M_PI / 2) * iMX * moveSpd;

    // Sliding collision (check X and Y independently)
    double r = 0.22;
    double nx = pX + dx, ny = pY + dy;
    if (canWalk(nx + r, pY) && canWalk(nx - r, pY) &&
        canWalk(nx, pY + r) && canWalk(nx, pY - r))
        pX = nx;
    if (canWalk(pX + r, ny) && canWalk(pX - r, ny) &&
        canWalk(pX, ny + r) && canWalk(pX, ny - r))
        pY = ny;

    // Oasis check — restore thirst and score
    uint8_t under = wmap[(int)pX][(int)pY];
    if (under == T_OASIS) {
        if (thirst < 100.0) {
            thirst = 100.0;
            score++;
        }
    }

    // Thirst decay (~40 seconds from full to dead)
    thirst -= 2.5 * dt;
    if (thirst < 0.0) thirst = 0.0;

    // Death → respawn in house
    if (thirst <= 0.0) {
        int hx = MAP_SZ / 2 - 5, hy = MAP_SZ / 2 - 5;
        pX = hx + 5.5;
        pY = hy + 7.5;
        pA = M_PI / 2;
        thirst = 100.0;
    }
}

// ═══════════════════════════════════════════════════════════════
//  C interface exposed to JavaScript via Emscripten
// ═══════════════════════════════════════════════════════════════
extern "C" {
    EMSCRIPTEN_KEEPALIVE void    init()           { generateWorld(); }
    EMSCRIPTEN_KEEPALIVE void    update_game(double dt) { updateGame(dt); }
    EMSCRIPTEN_KEEPALIVE void    render_game()    { renderFrame(); }
    EMSCRIPTEN_KEEPALIVE uint8_t *get_framebuffer() { return (uint8_t *)fb; }
    EMSCRIPTEN_KEEPALIVE double  get_thirst()     { return thirst; }
    EMSCRIPTEN_KEEPALIVE double  get_x()          { return pX; }
    EMSCRIPTEN_KEEPALIVE double  get_y()          { return pY; }
    EMSCRIPTEN_KEEPALIVE double  get_angle()      { return pA; }
    EMSCRIPTEN_KEEPALIVE int     get_score()      { return score; }
    EMSCRIPTEN_KEEPALIVE double  get_time()       { return gameTime; }
    EMSCRIPTEN_KEEPALIVE void    set_input(float mx, float my, float turn) {
        iMX = mx; iMY = my; iTurn = turn;
    }
}

int main() { return 0; }
