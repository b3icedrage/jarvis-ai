// ═══════════════════════════════════════════════════════════════════
// OASIS v4 — 3D Desert Survival Raycasting Game
//
// Flat brown desert, one house with roof/ceiling and animated door,
// scattered water oases. Enhanced 3D lighting, textures, and fog.
//
// Compiled to WebAssembly via Emscripten → runs in any browser.
// ═══════════════════════════════════════════════════════════════════

#include <emscripten/emscripten.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdlib>

// ── Screen & Texture ──────────────────────────────────────────
static const int SW = 640, SH = 400;
static const int TEX = 64;
static const int MAP = 80;

// ── Tiles ─────────────────────────────────────────────────────
enum Tile : uint8_t {
    T_Empty = 0,
    T_Wall  = 1,
    T_Floor = 2,
    T_Door  = 3,
    T_Water = 4,
    T_Furn  = 5
};

// ── Globals ───────────────────────────────────────────────────
// 0=wall, 1=int-floor, 2=furniture, 3=desert ground, 4=ceiling, 5=door
static uint32_t textures[6][TEX * TEX];
static uint8_t  wmap[MAP][MAP];
static uint32_t fb[SW * SH];
static double   zbuf[SW];

// Player
static double pX, pY, pA;
static double thirst = 100;
static int    score = 0;
static float  iMX, iMY, iTurn;
static double gTime = 0;
static double bobPhase = 0;
static int    sprinting = 0;

// Door
static double doorOpen = 0.0;   // 0=closed, 1=open
static double doorTarget = 0.0; // target state

// House bounds (for ceiling detection)
static int houseX0, houseY0, houseX1, houseY1;

// Oasis positions
struct Oasis { double x, y; };
static Oasis oasisList[30];
static int   numOases = 0;

// Particles
struct Part { float x, y, vx, vy, life; uint32_t col; };
static const int MAXP = 80;
static Part parts[MAXP];

// Sound signal
static int soundSignal = 0;

// ── Helpers ───────────────────────────────────────────────────
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}
static inline uint8_t h8(int x, int y) {
    int n = x * 374761393 + y * 668265263;
    n = (n ^ (n >> 13)) * 1274126177;
    return (uint8_t)((n ^ (n >> 16)) & 0xFF);
}
static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}
static inline double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

// Desert fog
static inline uint32_t fogC(uint32_t c, double d) {
    double f = clampd(d / 16.0, 0.0, 1.0);
    f = f * f;
    uint8_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    r = (uint8_t)(r + (218 - r) * f);
    g = (uint8_t)(g + (196 - g) * f);
    b = (uint8_t)(b + (158 - b) * f);
    return rgb(r, g, b);
}

// Walkability — FIXED: now includes T_Floor
static bool canWalk(double x, double y) {
    if (x < 0.25 || x >= MAP - 0.25 || y < 0.25 || y >= MAP - 0.25) return false;
    uint8_t t = wmap[(int)x][(int)y];
    return t == T_Empty || t == T_Door || t == T_Water || t == T_Floor;
}

// Check if position is inside house interior
static bool isInsideHouse(double x, double y) {
    int ix = (int)x, iy = (int)y;
    return ix > houseX0 && ix < houseX1 && iy > houseY0 && iy < houseY1;
}

// ═══════════════════════════════════════════════════════════════
//  PROCEDURAL TEXTURE GENERATION
// ═══════════════════════════════════════════════════════════════
static void genTextures() {
    for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
        uint8_t n  = h8(x, y);
        uint8_t n2 = h8(x * 3 + 7, y * 5 + 13);
        uint8_t n3 = h8(x * 7 + 31, y * 11 + 97);

        // ── 0: Sandstone wall with mortar + bevel ──
        {
            int by = y % 16, bx = x % 32;
            bool mortarV = (bx == 0) || (bx == 16 && by >= 8);
            bool mortarH = (by == 0) || (by == 1 && n > 200);
            bool isMortar = mortarV || mortarH;
            double bevelX = 0, bevelY = 0;
            if (!isMortar) {
                int lx = (bx == 0) ? 16 : bx;
                bevelX = (lx < 8) ? (8.0 - lx) / 8.0 : (lx - 8.0) / 8.0;
                bevelY = (by < 8) ? (8.0 - by) / 8.0 : (by - 8.0) / 8.0;
                bevelX = 1.0 - bevelX * 0.4;
                bevelY = 1.0 - bevelY * 0.3;
            }
            double lighting = bevelX * bevelY;
            uint8_t r, g, b;
            if (isMortar) {
                r = 165 + n / 12; g = 148 + n / 14; b = 120 + n / 16;
            } else {
                r = (uint8_t)((210 + n / 8 - 10) * lighting);
                g = (uint8_t)((188 + n / 10 - 8) * lighting);
                b = (uint8_t)((152 + n / 12 - 6) * lighting);
                if (n2 > 230) { r -= 12; g -= 10; b -= 8; }
                if (n3 > 245) { r += 8; g += 6; b += 4; }
            }
            textures[0][y * TEX + x] = rgb(
                (uint8_t)clampd(r, 0, 255),
                (uint8_t)clampd(g, 0, 255),
                (uint8_t)clampd(b, 0, 255));
        }

        // ── 1: Interior floor — stone tiles with grout ──
        {
            int tx = x % 32, ty = y % 32;
            bool grout = (tx == 0 || ty == 0 || tx == 1 || ty == 1);
            double dx = (tx - 16.0) / 16.0, dy = (ty - 16.0) / 16.0;
            double tileShade = 1.0 - (dx * dx + dy * dy) * 0.15;
            uint8_t r, g, b;
            if (grout) {
                r = 90 + n / 10; g = 82 + n / 12; b = 70 + n / 14;
            } else {
                double base = tileShade;
                r = (uint8_t)(clampd((160 + n / 6) * base, 0, 255));
                g = (uint8_t)(clampd((148 + n / 8) * base, 0, 255));
                b = (uint8_t)(clampd((130 + n / 10) * base, 0, 255));
                if (n2 > 240) { r -= 15; g -= 12; b -= 8; }
            }
            textures[1][y * TEX + x] = rgb(r, g, b);
        }

        // ── 2: Furniture — dark wood grain ──
        {
            double grain = sin((double)y * 0.5 + n * 0.012) * 0.15 + 0.85;
            double grain2 = sin((double)x * 0.3 + y * 0.7) * 0.08 + 0.92;
            double ex = (x < 2) ? (2 - x) / 2.0 : (x >= 62) ? (x - 62) / 2.0 : 0;
            double ey = (y < 2) ? (2 - y) / 2.0 : (y >= 62) ? (y - 62) / 2.0 : 0;
            double edgeShade = 1.0 - (ex + ey) * 0.4;
            double sh = grain * grain2 * edgeShade;
            uint8_t r = (uint8_t)(clampd((135 * sh + n / 12), 0, 255));
            uint8_t g = (uint8_t)(clampd((95 * sh + n / 14), 0, 255));
            uint8_t b = (uint8_t)(clampd((45 * sh + n / 18), 0, 255));
            if (y % 8 == 0) { r -= 20; g -= 15; b -= 10; }
            textures[2][y * TEX + x] = rgb(r, g, b);
        }

        // ── 3: Desert sand — flat brown with pebbles ──
        {
            double pebble = 0;
            if (n2 > 225) {
                int px = x % 8, py = y % 8;
                double pd = sqrt((double)(px - 4) * (px - 4) + (double)(py - 4) * (py - 4));
                if (pd < 2.5) pebble = 1.0 - pd / 2.5;
            }
            double ripple = sin((double)x * 0.3 + (double)y * 0.1) * 0.04;
            double base = 1.0 + ripple;
            uint8_t r = (uint8_t)(clampd((195 + n / 6 - 8) * base - pebble * 25, 0, 255));
            uint8_t g = (uint8_t)(clampd((165 + n / 8 - 6) * base - pebble * 18, 0, 255));
            uint8_t b = (uint8_t)(clampd((110 + n / 10 - 5) * base - pebble * 10, 0, 255));
            textures[3][y * TEX + x] = rgb(r, g, b);
        }

        // ── 4: Ceiling — dark plaster/wood beams ──
        {
            // Cross-beam pattern
            bool beamX = (x % 32 < 3);
            bool beamY = (y % 32 < 3);
            bool isBeam = beamX || beamY;

            // Plaster between beams
            double grain = sin((double)y * 0.3 + x * 0.2 + n * 0.01) * 0.06 + 0.94;

            uint8_t r, g, b;
            if (isBeam) {
                // Dark wood beam
                double gBeam = sin((double)y * 0.8 + n * 0.015) * 0.1 + 0.9;
                r = (uint8_t)(clampd(95 * gBeam + n / 14, 0, 255));
                g = (uint8_t)(clampd(72 * gBeam + n / 16, 0, 255));
                b = (uint8_t)(clampd(40 * gBeam + n / 20, 0, 255));
            } else {
                // Plaster
                r = (uint8_t)(clampd((145 + n / 10) * grain, 0, 255));
                g = (uint8_t)(clampd((130 + n / 12) * grain, 0, 255));
                b = (uint8_t)(clampd((105 + n / 14) * grain, 0, 255));
                if (n2 > 235) { r -= 10; g -= 8; b -= 6; }
            }
            textures[4][y * TEX + x] = rgb(r, g, b);
        }

        // ── 5: Door — wooden planks with iron studs ──
        {
            // Vertical planks
            int plankX = x % 16;
            bool plankEdge = (plankX == 0 || plankX == 15);
            bool plankMid = (plankX == 7 || plankX == 8);

            // Horizontal cross-bar
            bool crossBar = (y >= 24 && y <= 28) || (y >= 44 && y <= 48);

            // Iron studs
            bool stud = false;
            int sx = x % 32, sy = y % 32;
            if ((sx == 8 || sx == 24) && (sy == 12 || sy == 20 || sy == 36 || sy == 44)) {
                double sd = sqrt((double)(sx - (sx < 16 ? 8 : 24)) * (sx - (sx < 16 ? 8 : 24))
                               + (double)(sy - (sy < 28 ? (sy < 16 ? 12 : 20) : (sy < 40 ? 36 : 44)))
                               * (sy - (sy < 28 ? (sy < 16 ? 12 : 20) : (sy < 40 ? 36 : 44))));
                if (sd < 2.5) stud = true;
            }

            double grain = sin((double)y * 0.6 + n * 0.01) * 0.08 + 0.92;
            uint8_t r, g, b;

            if (stud) {
                // Iron stud — dark metallic
                double shine = 1.0 - ((double)(sx % 32 - 8) * (sx % 32 - 8) + (double)(sy % 32 - (sy < 28 ? 12 : 36)) * (sy % 32 - (sy < 28 ? 12 : 36))) / 6.25;
                if (shine < 0) shine = 0;
                r = (uint8_t)(clampd(60 + 40 * shine + n / 16, 0, 255));
                g = (uint8_t)(clampd(55 + 35 * shine + n / 18, 0, 255));
                b = (uint8_t)(clampd(50 + 30 * shine + n / 20, 0, 255));
            } else if (crossBar) {
                // Horizontal cross-bar
                r = (uint8_t)(clampd(75 * grain + n / 14, 0, 255));
                g = (uint8_t)(clampd(55 * grain + n / 16, 0, 255));
                b = (uint8_t)(clampd(30 * grain + n / 20, 0, 255));
            } else if (plankEdge || plankMid) {
                // Plank gap (dark line)
                r = (uint8_t)clampd(55 + n / 16, 0, 255);
                g = (uint8_t)clampd(42 + n / 18, 0, 255);
                b = (uint8_t)clampd(22 + n / 22, 0, 255);
            } else {
                // Wood plank
                r = (uint8_t)(clampd((120 + n / 8) * grain, 0, 255));
                g = (uint8_t)(clampd((88 + n / 10) * grain, 0, 255));
                b = (uint8_t)(clampd((45 + n / 14) * grain, 0, 255));
            }
            textures[5][y * TEX + x] = rgb(r, g, b);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  WORLD GENERATION
// ═══════════════════════════════════════════════════════════════
static void genWorld() {
    memset(wmap, T_Empty, sizeof(wmap));
    numOases = 0;
    doorOpen = 0.0;
    doorTarget = 0.0;
    srand(42);

    // ── The house (11×11 exterior, centered) ──
    int hx = MAP / 2 - 5, hy = MAP / 2 - 5;
    houseX0 = hx; houseY0 = hy;
    houseX1 = hx + 10; houseY1 = hy + 10;

    // Walls
    for (int i = 0; i < 11; i++) {
        wmap[hx + i][hy]      = T_Wall;  // north
        wmap[hx + i][hy + 10] = T_Wall;  // south
        wmap[hx][hy + i]      = T_Wall;  // west
        wmap[hx + 10][hy + i] = T_Wall;  // east
    }

    // Door — south wall center (2 wide)
    wmap[hx + 4][hy + 10] = T_Door;
    wmap[hx + 5][hy + 10] = T_Door;

    // Windows — north wall
    wmap[hx + 3][hy] = T_Door;
    wmap[hx + 7][hy] = T_Door;
    // Window — east wall
    wmap[hx + 10][hy + 4] = T_Door;

    // Interior floor
    for (int iy = 1; iy < 10; iy++)
        for (int ix = 1; ix < 10; ix++)
            wmap[hx + ix][hy + iy] = T_Floor;

    // Furniture
    wmap[hx + 2][hy + 2] = T_Furn;
    wmap[hx + 3][hy + 2] = T_Furn;
    wmap[hx + 2][hy + 3] = T_Furn;
    wmap[hx + 7][hy + 2] = T_Furn;
    wmap[hx + 8][hy + 2] = T_Furn;
    wmap[hx + 2][hy + 7] = T_Furn;
    wmap[hx + 3][hy + 7] = T_Furn;
    wmap[hx + 2][hy + 8] = T_Furn;
    wmap[hx + 3][hy + 8] = T_Furn;

    // ── Oases ──
    int oasisPos[][2] = {
        {8,8},{65,8},{8,65},{65,65},{25,15},{55,12},{12,40},{68,40},
        {30,50},{50,55},{20,30},{60,28},{15,60},{55,62},{40,10},{40,70},
        {10,20},{70,55},{35,35},{45,45}
    };
    for (int i = 0; i < 20; i++) {
        int ox = oasisPos[i][0], oy = oasisPos[i][1];
        if (abs(ox - MAP / 2) < 8 && abs(oy - MAP / 2) < 8) continue;
        for (int dx = 0; dx < 2; dx++)
            for (int dy = 0; dy < 2; dy++) {
                int wx = ox + dx, wy = oy + dy;
                if (wx >= 0 && wx < MAP && wy >= 0 && wy < MAP)
                    wmap[wx][wy] = T_Water;
            }
        if (numOases < 30) {
            oasisList[numOases].x = ox + 1.0;
            oasisList[numOases].y = oy + 1.0;
            numOases++;
        }
    }

    // Player spawns inside house
    pX = hx + 5.5; pY = hy + 8.0;
    pA = -M_PI / 2;
    thirst = 100; score = 0; gTime = 0; bobPhase = 0;

    for (int i = 0; i < MAXP; i++) {
        parts[i].x = (float)(rand() % SW);
        parts[i].y = (float)(rand() % SH);
        parts[i].vx = 0.2f + (rand() % 80) / 100.0f;
        parts[i].vy = (rand() % 20 - 10) / 100.0f;
        parts[i].life = (float)(rand() % 300);
        parts[i].col = rgb(200 + rand() % 30, 175 + rand() % 25, 130 + rand() % 20, 100);
    }
}

// ═══════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════

static inline int getBob() {
    double speed = sqrt(iMX * iMX + iMY * iMY);
    if (speed < 0.1) return 0;
    return (int)(sin(bobPhase * 10.0) * 3.5 * speed);
}

static void renderFrame() {
    double fov = M_PI / 3.0;
    double tanHalf = tan(fov / 2.0);

    // Day/night
    double dayP = fmod(gTime / 120.0, 1.0);
    double sunAngle = dayP * M_PI;
    double bright = 0.6 + 0.4 * sin(sunAngle);

    // Sun position
    int sunSX = (int)(SW * 0.1 + SW * 0.8 * dayP);
    int sunSY = (int)(SH * 0.45 - SH * 0.35 * sin(sunAngle));
    double sunR = 12.0 + 3.0 * sin(gTime * 0.5);

    int bob = getBob();
    double lightDirX = cos(sunAngle - M_PI * 0.3);
    double lightDirY = 0.3;

    bool playerIndoors = isInsideHouse(pX, pY);

    for (int x = 0; x < SW; x++) {
        double rayA = pA - fov / 2.0 + ((double)x / SW) * fov;
        double rdx = cos(rayA), rdy = sin(rayA);

        // DDA
        int mx = (int)pX, my = (int)pY;
        double ddx = rdx == 0 ? 1e30 : fabs(1.0 / rdx);
        double ddy = rdy == 0 ? 1e30 : fabs(1.0 / rdy);
        double sdx, sdy;
        int sx, sy;
        if (rdx < 0) { sx = -1; sdx = (pX - mx) * ddx; }
        else          { sx =  1; sdx = (mx + 1.0 - pX) * ddx; }
        if (rdy < 0) { sy = -1; sdy = (pY - my) * ddy; }
        else          { sy =  1; sdy = (my + 1.0 - pY) * ddy; }

        int side = 0, hit = 0;
        uint8_t tile = 0;
        for (int step = 0; step < 128; step++) {
            if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
            else            { sdy += ddy; my += sy; side = 1; }
            if (mx < 0 || mx >= MAP || my < 0 || my >= MAP) break;
            tile = wmap[mx][my];
            if (tile == T_Wall || tile == T_Furn) { hit = 1; break; }
            // Door: only blocks rays when closed
            if (tile == T_Door && doorOpen < 0.85) { hit = 1; break; }
        }

        double pd;
        double wallHitX, wallHitY;
        if (hit) {
            pd = side == 0 ? sdx - ddx : sdy - ddy;
            if (pd < 0.01) pd = 0.01;
            if (side == 0) { wallHitX = mx + (sx < 0 ? 1.0 : 0.0); wallHitY = pY + pd * rdy; }
            else           { wallHitX = pX + pd * rdx; wallHitY = my + (sy < 0 ? 1.0 : 0.0); }
        } else {
            pd = 50.0;
            wallHitX = pX + pd * rdx;
            wallHitY = pY + pd * rdy;
        }

        int lh = (int)(SH / pd);
        int dT = -lh / 2 + SH / 2 - bob;
        int dB =  lh / 2 + SH / 2 - bob;
        int cT = dT < 0 ? 0 : dT;
        int cB = dB >= SH ? SH - 1 : dB;

        // Texture X
        double wallX;
        if (hit) {
            wallX = side == 0 ? (wallHitY - floor(wallHitY)) : (wallHitX - floor(wallHitX));
        } else {
            wallX = 0;
        }
        int texX = (int)(wallX * TEX) & (TEX - 1);
        if ((side == 0 && rdx > 0) || (side == 1 && rdy < 0)) texX = TEX - texX - 1;

        // ── Sky OR Ceiling ──
        if (playerIndoors) {
            // Ceiling: mirror of floor casting, going upward
            for (int y = 0; y < cT; y++) {
                // Ceiling distance (mirror of floor formula)
                double rowD = (double)SH / (2.0 * (SH / 2.0 - y + bob));
                if (rowD < 0.1) rowD = 0.1;
                double cx = pX + rowD * rdx;
                double cy = pY + rowD * rdy;
                int tx = ((int)(cx * TEX)) & (TEX - 1);
                int ty = ((int)(cy * TEX)) & (TEX - 1);

                uint32_t cc = textures[4][ty * TEX + tx]; // ceiling texture
                uint8_t cr = cc & 0xFF;
                uint8_t cg = (cc >> 8) & 0xFF;
                uint8_t cb = (cc >> 16) & 0xFF;

                // Indoor lighting (dimmer than floor — ceiling is darker)
                double ceilLight = clampd(0.55 - rowD / 30.0, 0.2, 0.55) * bright;
                cr = (uint8_t)clampd(cr * ceilLight, 0, 255);
                cg = (uint8_t)clampd(cg * ceilLight, 0, 255);
                cb = (uint8_t)clampd(cb * ceilLight, 0, 255);

                fb[y * SW + x] = fogC(rgb(cr, cg, cb), rowD);
            }
        } else {
            // Sky with gradient + sun
            for (int y = 0; y < cT; y++) {
                double t = (double)y / (SH * 0.5);
                double tSmooth = t * t * (3.0 - 2.0 * t);
                uint8_t sr = (uint8_t)clampd(lerp(45, 220, tSmooth) * bright, 0, 255);
                uint8_t sg = (uint8_t)clampd(lerp(70, 195, tSmooth) * bright, 0, 255);
                uint8_t sb = (uint8_t)clampd(lerp(140, 165, tSmooth) * bright, 0, 255);

                double dsx = x - sunSX, dsy = y - sunSY;
                double sunDist = sqrt(dsx * dsx + dsy * dsy);
                if (sunDist < sunR * 4.0) {
                    double glow = 1.0 - sunDist / (sunR * 4.0);
                    glow = glow * glow;
                    sr = (uint8_t)clampd(sr + 255 * glow * bright, 0, 255);
                    sg = (uint8_t)clampd(sg + 200 * glow * bright, 0, 255);
                    sb = (uint8_t)clampd(sb + 100 * glow * bright, 0, 255);
                }
                if (sunDist < sunR) {
                    double sf = 1.0 - sunDist / sunR;
                    sf = sf * sf;
                    sr = (uint8_t)clampd(lerp(sr, 255, sf), 0, 255);
                    sg = (uint8_t)clampd(lerp(sg, 240, sf), 0, 255);
                    sb = (uint8_t)clampd(lerp(sb, 180, sf), 0, 255);
                }
                fb[y * SW + x] = rgb(sr, sg, sb);
            }
        }

        // ── Wall rendering ──
        if (hit) {
            uint32_t *tex;
            if (tile == T_Furn) tex = textures[2];
            else if (tile == T_Door) tex = textures[5];
            else tex = textures[0];

            for (int y = cT; y <= cB; y++) {
                int texY = (int)(((double)(y - dT) / lh) * TEX) & (TEX - 1);
                uint32_t tc = tex[texY * TEX + texX];

                uint8_t r = tc & 0xFF;
                uint8_t g = (tc >> 8) & 0xFF;
                uint8_t b = (tc >> 16) & 0xFF;

                double sideMul = side ? 0.72 : 1.0;
                double heightT = (double)(y - dT) / lh;
                double heightMul = 0.75 + 0.25 * (1.0 - heightT);
                double aoMul = 1.0;
                if (heightT > 0.85) aoMul = 1.0 - (heightT - 0.85) * 3.0;

                // Door transparency when opening
                if (tile == T_Door && doorOpen > 0.05) {
                    double alpha = 1.0 - doorOpen;
                    if (alpha < 0.05) alpha = 0.05;
                    // Blend door texture with a dark background
                    r = (uint8_t)(r * alpha * sideMul * heightMul * aoMul * bright);
                    g = (uint8_t)(g * alpha * sideMul * heightMul * aoMul * bright);
                    b = (uint8_t)(b * alpha * sideMul * heightMul * aoMul * bright);
                } else {
                    double lightMul = sideMul * heightMul * aoMul * bright;
                    r = (uint8_t)clampd(r * lightMul, 0, 255);
                    g = (uint8_t)clampd(g * lightMul, 0, 255);
                    b = (uint8_t)clampd(b * lightMul, 0, 255);
                }

                fb[y * SW + x] = fogC(rgb(r, g, b), pd);
            }
        }

        // ── Floor ──
        for (int y = cB + 1; y < SH; y++) {
            double rowD = (double)SH / (2.0 * (y - SH / 2.0 + bob));
            double fx = pX + rowD * rdx;
            double fy = pY + rowD * rdy;
            int tx = ((int)(fx * TEX)) & (TEX - 1);
            int ty = ((int)(fy * TEX)) & (TEX - 1);

            uint32_t fc = textures[3][ty * TEX + tx];
            uint8_t fr = fc & 0xFF;
            uint8_t fg = (fc >> 8) & 0xFF;
            uint8_t fb_ = (fc >> 16) & 0xFF;

            // Inside house: use interior floor texture
            if (isInsideHouse(fx, fy)) {
                fc = textures[1][ty * TEX + tx];
                fr = fc & 0xFF;
                fg = (fc >> 8) & 0xFF;
                fb_ = (fc >> 16) & 0xFF;
                double floorLight = clampd(0.65 - rowD / 30.0, 0.25, 0.65) * bright;
                fr = (uint8_t)clampd(fr * floorLight, 0, 255);
                fg = (uint8_t)clampd(fg * floorLight, 0, 255);
                fb_ = (uint8_t)clampd(fb_ * floorLight, 0, 255);
            } else {
                double floorLight = clampd(1.0 - rowD / 25.0, 0.3, 1.0) * bright;
                double fdot = (rdx * lightDirX + rdy * lightDirY);
                floorLight *= (0.9 + 0.1 * fdot);
                fr = (uint8_t)clampd(fr * floorLight, 0, 255);
                fg = (uint8_t)clampd(fg * floorLight, 0, 255);
                fb_ = (uint8_t)clampd(fb_ * floorLight, 0, 255);
            }

            fb[y * SW + x] = fogC(rgb(fr, fg, fb_), rowD);
        }

        zbuf[x] = pd;
    }

    // ── Oasis water ──
    for (int oi = 0; oi < numOases; oi++) {
        Oasis &o = oasisList[oi];
        double odx = o.x - pX, ody = o.y - pY;
        double dirX = cos(pA), dirY = sin(pA);
        double plX = -sin(pA) * tanHalf, plY = cos(pA) * tanHalf;
        double invDet = 1.0 / (plX * dirY - dirX * plY);
        double tX = invDet * (dirY * odx - dirX * ody);
        double tY = invDet * (-plY * odx + plX * ody);
        if (tY <= 0.3) continue;
        int scrX = (int)(SW / 2.0 * (1.0 + tX / tY));
        double dist = sqrt(odx * odx + ody * ody);
        int poolRadius = (int)(30.0 / tY);
        if (poolRadius < 3) continue;

        for (int sy = -poolRadius / 3; sy <= poolRadius / 3; sy++) {
            int screenY = SH / 2 - bob + (int)(poolRadius * 0.35) + sy;
            if (screenY < 0 || screenY >= SH) continue;
            for (int sx = -poolRadius; sx <= poolRadius; sx++) {
                int screenX = scrX + sx;
                if (screenX < 0 || screenX >= SW) continue;
                double ex = (double)sx / poolRadius;
                double ey = (double)sy / (poolRadius / 3.0);
                double ed = ex * ex + ey * ey;
                if (ed > 1.0) continue;
                double rowD = (double)SH / (2.0 * (screenY - SH / 2.0 + bob));
                if (rowD > dist * 0.8) continue;
                if (tY >= zbuf[screenX]) continue;

                double ripple1 = sin(ed * 8.0 - gTime * 4.0) * 0.12;
                double ripple2 = sin(ed * 5.0 + gTime * 3.0) * 0.08;
                double ripple = 1.0 + ripple1 + ripple2;
                double edgeFade = 1.0 - smoothstep(ed);
                uint8_t wr = (uint8_t)clampd((30 + 20 * ripple) * edgeFade * bright, 0, 255);
                uint8_t wg = (uint8_t)clampd((100 + 50 * ripple) * edgeFade * bright, 0, 255);
                uint8_t wb = (uint8_t)clampd((180 + 40 * ripple) * edgeFade * bright, 0, 255);

                double spec = sin(ed * 12.0 - gTime * 6.0 + pA);
                if (spec > 0.85 && ed < 0.3) {
                    double sf = (spec - 0.85) / 0.15;
                    wr = (uint8_t)clampd(wr + 100 * sf, 0, 255);
                    wg = (uint8_t)clampd(wg + 90 * sf, 0, 255);
                    wb = (uint8_t)clampd(wb + 60 * sf, 0, 255);
                }

                uint32_t existing = fb[screenY * SW + screenX];
                uint8_t er = existing & 0xFF;
                uint8_t eg = (existing >> 8) & 0xFF;
                uint8_t eb = (existing >> 16) & 0xFF;
                double blend = edgeFade * 0.85;
                uint8_t rr = (uint8_t)(er + (wr - er) * blend);
                uint8_t rg = (uint8_t)(eg + (wg - eg) * blend);
                uint8_t rb = (uint8_t)(eb + (wb - eb) * blend);
                fb[screenY * SW + screenX] = fogC(rgb(rr, rg, rb), dist);
            }
        }
    }

    // ── Sand particles ──
    for (int i = 0; i < MAXP; i++) {
        Part &p = parts[i];
        if (p.life <= 0) continue;
        int px_ = (int)p.x, py_ = (int)p.y;
        if (px_ >= 0 && px_ < SW && py_ >= 0 && py_ < SH) {
            uint8_t a = (uint8_t)(p.life > 50 ? 100 : p.life * 2);
            fb[py_ * SW + px_] = rgb(200, 180, 140, a);
            if (px_ + 1 < SW)
                fb[py_ * SW + px_ + 1] = rgb(190, 170, 130, a / 2);
        }
    }

    // ── Minimap ──
    int mmS = 100, mmT = 12;
    int mmX0 = SW - mmS - 8, mmY0 = 8;
    double mmSc = (double)mmS / mmT;
    for (int y = 0; y < mmS; y++)
        for (int xx = 0; xx < mmS; xx++)
            fb[(mmY0 + y) * SW + mmX0 + xx] = rgb(20, 16, 10, 180);
    for (int y = 0; y < mmS; y++)
        for (int xx = 0; xx < mmS; xx++) {
            int wx = (int)(pX - mmT / 2.0 + xx / mmSc);
            int wy = (int)(pY - mmT / 2.0 + y / mmSc);
            if (wx < 0 || wx >= MAP || wy < 0 || wy >= MAP) continue;
            uint32_t c;
            switch (wmap[wx][wy]) {
                case T_Empty: c = rgb(190, 165, 125); break;
                case T_Wall:  c = rgb(185, 165, 130); break;
                case T_Floor: c = rgb(150, 138, 118); break;
                case T_Door:  c = doorOpen > 0.5 ? rgb(160, 130, 80) : rgb(100, 72, 35); break;
                case T_Water: c = rgb(40, 110, 200);   break;
                case T_Furn:  c = rgb(110, 80, 40);    break;
                default:      c = rgb(180, 160, 120);
            }
            fb[(mmY0 + y) * SW + mmX0 + xx] = c;
        }
    int pdx = mmS / 2, pdy = mmS / 2;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 4)
                fb[(mmY0 + pdy + dy) * SW + mmX0 + pdx + dx] = rgb(255, 60, 60);
    for (int i = 3; i < 12; i++) {
        int lx = pdx + (int)(cos(pA) * i);
        int ly = pdy + (int)(sin(pA) * i);
        if (lx >= 0 && lx < mmS && ly >= 0 && ly < mmS)
            fb[(mmY0 + ly) * SW + mmX0 + lx] = rgb(255, 220, 60);
    }
    for (int i = 0; i < mmS; i++) {
        fb[mmY0 * SW + mmX0 + i] = rgb(90, 72, 45);
        fb[(mmY0 + mmS - 1) * SW + mmX0 + i] = rgb(90, 72, 45);
        fb[(mmY0 + i) * SW + mmX0] = rgb(90, 72, 45);
        fb[(mmY0 + i) * SW + mmX0 + mmS - 1] = rgb(90, 72, 45);
    }
}

// ═══════════════════════════════════════════════════════════════
//  GAME UPDATE
// ═══════════════════════════════════════════════════════════════
static void updateGame(double dt) {
    gTime += dt;
    soundSignal = 0;

    double moveSpd = (sprinting ? 5.5 : 3.8) * dt;
    double rotSpd  = 2.8 * dt;

    pA += iTurn * rotSpd;

    double dx = cos(pA) * iMY * moveSpd + cos(pA + M_PI / 2) * iMX * moveSpd;
    double dy = sin(pA) * iMY * moveSpd + sin(pA + M_PI / 2) * iMX * moveSpd;

    // Sliding collision
    double r = 0.25;
    double nx = pX + dx, ny = pY + dy;
    if (canWalk(nx + r, pY) && canWalk(nx - r, pY) && canWalk(nx, pY + r) && canWalk(nx, pY - r))
        pX = nx;
    if (canWalk(pX + r, ny) && canWalk(pX - r, ny) && canWalk(pX, ny + r) && canWalk(pX, ny - r))
        pY = ny;

    // Head bob
    double speed = sqrt(dx * dx + dy * dy) / dt;
    if (speed > 0.5) bobPhase += dt * speed * 0.6;

    // Footstep
    static double lastStep = 0;
    if (speed > 0.5 && gTime - lastStep > 0.35) {
        soundSignal = 1;
        lastStep = gTime;
    }

    // ── Door animation ──
    // Door is at south wall center
    int hx = MAP / 2 - 5, hy = MAP / 2 - 5;
    double doorCX = hx + 5.0;  // center of 2-wide door
    double doorCY = hy + 10.5;  // south wall
    double distToDoor = sqrt((pX - doorCX) * (pX - doorCX) + (pY - doorCY) * (pY - doorCY));

    if (distToDoor < 4.0) {
        doorTarget = 1.0;
    } else {
        doorTarget = 0.0;
    }

    double prevDoorOpen = doorOpen;
    double doorSpeed = 3.0 * dt;
    if (doorOpen < doorTarget) {
        doorOpen += doorSpeed;
        if (doorOpen > doorTarget) doorOpen = doorTarget;
    } else {
        doorOpen -= doorSpeed;
        if (doorOpen < doorTarget) doorOpen = doorTarget;
    }

    // Door sound: play once when door starts opening or fully closes
    if (prevDoorOpen < 0.1 && doorOpen >= 0.1) soundSignal = 5; // door open
    if (prevDoorOpen > 0.1 && doorOpen <= 0.05) soundSignal = 5; // door close

    // Water check
    uint8_t under = wmap[(int)pX][(int)pY];
    if (under == T_Water && thirst < 100.0) {
        thirst = 100.0;
        score++;
        soundSignal = 2;
    }

    // Thirst
    thirst -= 2.5 * dt;
    if (thirst < 0) thirst = 0;

    // Warning
    if (thirst > 0 && thirst < 20 && fmod(gTime, 1.5) < dt)
        soundSignal = 4;

    // Death
    if (thirst <= 0) {
        soundSignal = 3;
        pX = hx + 5.5; pY = hy + 8.0; pA = -M_PI / 2;
        thirst = 100.0;
    }

    sprinting = (sqrt(iMX * iMX + iMY * iMY) > 0.85) ? 1 : 0;

    // Particles
    float wind = 0.3f + 0.15f * sin((float)gTime * 0.3f);
    for (int i = 0; i < MAXP; i++) {
        Part &p = parts[i];
        p.x += p.vx + wind;
        p.y += p.vy;
        p.life -= 1.0f;
        if (p.life <= 0 || p.x > SW + 5 || p.y < -5 || p.y > SH + 5) {
            p.x = -2.0f;
            p.y = (float)(rand() % SH);
            p.vx = 0.2f + (rand() % 80) / 100.0f;
            p.vy = (rand() % 20 - 10) / 100.0f;
            p.life = 120.0f + (rand() % 250);
            p.col = rgb(200 + rand() % 25, 175 + rand() % 25, 130 + rand() % 20, 100);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  C INTERFACE
// ═══════════════════════════════════════════════════════════════
extern "C" {
    EMSCRIPTEN_KEEPALIVE void init()            { genTextures(); genWorld(); }
    EMSCRIPTEN_KEEPALIVE void update_game(double dt) { updateGame(dt); }
    EMSCRIPTEN_KEEPALIVE void render_game()     { renderFrame(); }
    EMSCRIPTEN_KEEPALIVE uint8_t *get_framebuffer() { return (uint8_t *)fb; }
    EMSCRIPTEN_KEEPALIVE double  get_thirst()   { return thirst; }
    EMSCRIPTEN_KEEPALIVE double  get_x()        { return pX; }
    EMSCRIPTEN_KEEPALIVE double  get_y()        { return pY; }
    EMSCRIPTEN_KEEPALIVE double  get_angle()    { return pA; }
    EMSCRIPTEN_KEEPALIVE int     get_score()    { return score; }
    EMSCRIPTEN_KEEPALIVE double  get_time()     { return gTime; }
    EMSCRIPTEN_KEEPALIVE int     get_sound()    { int s = soundSignal; soundSignal = 0; return s; }
    EMSCRIPTEN_KEEPALIVE int     get_sprint()   { return sprinting; }
    EMSCRIPTEN_KEEPALIVE void    set_input(float mx, float my, float t) { iMX = mx; iMY = my; iTurn = t; }
}

int main() { return 0; }
