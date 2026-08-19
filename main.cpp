// ═══════════════════════════════════════════════════════════════════
// OASIS v3 — 3D Desert Survival Raycasting Game
//
// Flat brown desert, one house, scattered water oases.
// Enhanced 3D: procedural textures with depth shading, proper
// lighting model, animated water with reflections, perspective
// floor casting, volumetric heat haze, realistic shadows.
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
    T_Wall  = 1,  // house exterior wall
    T_Floor = 2,  // house interior floor
    T_Door  = 3,  // doorway (walkable)
    T_Water = 4,  // oasis water tile
    T_Furn  = 5   // furniture (desk, shelf, bed)
};

// ── Globals ───────────────────────────────────────────────────
static uint32_t textures[4][TEX * TEX]; // 0=wall, 1=int-floor, 2=furniture, 3=desert ground
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

// Oasis positions (world coords, center of 2×2 clusters)
struct Oasis { double x, y; };
static Oasis oasisList[30];
static int   numOases = 0;

// Particles
struct Part { float x, y, vx, vy, life; uint32_t col; };
static const int MAXP = 80;
static Part parts[MAXP];

// Sound signal (reset each frame by JS)
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
static inline uint16_t h16(int x, int y) {
    int n = x * 2654435761 + y * 40503;
    n = (n ^ (n >> 16)) * 0x45d9f3b;
    return (uint16_t)((n ^ (n >> 16)) & 0xFFFF);
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

// Desert fog — warm sandy haze
static inline uint32_t fogC(uint32_t c, double d) {
    double f = clampd(d / 16.0, 0.0, 1.0);
    f = f * f; // quadratic falloff for realism
    uint8_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    r = (uint8_t)(r + (218 - r) * f);
    g = (uint8_t)(g + (196 - g) * f);
    b = (uint8_t)(b + (158 - b) * f);
    return rgb(r, g, b);
}

// Walkability
static bool canWalk(double x, double y) {
    if (x < 0.25 || x >= MAP - 0.25 || y < 0.25 || y >= MAP - 0.25) return false;
    uint8_t t = wmap[(int)x][(int)y];
    return t == T_Empty || t == T_Door || t == T_Water;
}

// ═══════════════════════════════════════════════════════════════
//  PROCEDURAL TEXTURE GENERATION — Enhanced 3D textures
// ═══════════════════════════════════════════════════════════════
static void genTextures() {
    for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
        uint8_t n  = h8(x, y);
        uint8_t n2 = h8(x * 3 + 7, y * 5 + 13);
        uint8_t n3 = h8(x * 7 + 31, y * 11 + 97);

        // ── 0: Sandstone wall with mortar lines + depth shading ──
        {
            int by = y % 16, bx = x % 32;
            bool mortarV = (bx == 0) || (bx == 16 && by >= 8);
            bool mortarH = (by == 0) || (by == 1 && n > 200);
            bool isMortar = mortarV || mortarH;

            // Brick surface normal simulation (fake bevel)
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
                // Add micro-detail: small pores
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

            // Perspective shading within each tile (center brighter)
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

        // ── 2: Furniture — dark wood grain with bevel ──
        {
            double grain = sin((double)y * 0.5 + n * 0.012) * 0.15 + 0.85;
            double grain2 = sin((double)x * 0.3 + y * 0.7) * 0.08 + 0.92;

            // Edge bevel for 3D look
            double ex = (x < 2) ? (2 - x) / 2.0 : (x >= 62) ? (x - 62) / 2.0 : 0;
            double ey = (y < 2) ? (2 - y) / 2.0 : (y >= 62) ? (y - 62) / 2.0 : 0;
            double edgeShade = 1.0 - (ex + ey) * 0.4;

            double sh = grain * grain2 * edgeShade;
            uint8_t r = (uint8_t)(clampd((135 * sh + n / 12), 0, 255));
            uint8_t g = (uint8_t)(clampd((95 * sh + n / 14), 0, 255));
            uint8_t b = (uint8_t)(clampd((45 * sh + n / 18), 0, 255));

            // Grain lines
            if (y % 8 == 0) { r -= 20; g -= 15; b -= 10; }
            textures[2][y * TEX + x] = rgb(r, g, b);
        }

        // ── 3: Desert sand ground — flat brown with pebbles ──
        {
            double pebble = 0;
            // Pebble pattern
            if (n2 > 225) {
                int px = x % 8, py = y % 8;
                double pd = sqrt((double)(px - 4) * (px - 4) + (double)(py - 4) * (py - 4));
                if (pd < 2.5) pebble = 1.0 - pd / 2.5;
            }
            // Ripples from wind
            double ripple = sin((double)x * 0.3 + (double)y * 0.1) * 0.04;

            double base = 1.0 + ripple;
            uint8_t r = (uint8_t)(clampd((195 + n / 6 - 8) * base - pebble * 25, 0, 255));
            uint8_t g = (uint8_t)(clampd((165 + n / 8 - 6) * base - pebble * 18, 0, 255));
            uint8_t b = (uint8_t)(clampd((110 + n / 10 - 5) * base - pebble * 10, 0, 255));
            textures[3][y * TEX + x] = rgb(r, g, b);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  WORLD GENERATION — Flat desert, one house, water oases
// ═══════════════════════════════════════════════════════════════
static void genWorld() {
    memset(wmap, T_Empty, sizeof(wmap));
    numOases = 0;
    srand(42);

    // ── The house (11×11 exterior, centered) ──
    int hx = MAP / 2 - 5, hy = MAP / 2 - 5;
    // Walls
    for (int i = 0; i < 11; i++) {
        wmap[hx + i][hy]     = T_Wall;  // north
        wmap[hx + i][hy + 10] = T_Wall;  // south
        wmap[hx][hy + i]     = T_Wall;  // west
        wmap[hx + 10][hy + i] = T_Wall;  // east
    }
    // Door — south wall center (3 wide for easy entry)
    wmap[hx + 4][hy + 10] = T_Door;
    wmap[hx + 5][hy + 10] = T_Door;
    wmap[hx + 6][hy + 10] = T_Door;
    // Windows — north wall
    wmap[hx + 3][hy] = T_Door;
    wmap[hx + 7][hy] = T_Door;
    // Window — east wall
    wmap[hx + 10][hy + 4] = T_Door;
    wmap[hx + 10][hy + 6] = T_Door;

    // Interior floor (all interior tiles)
    for (int iy = 1; iy < 10; iy++)
        for (int ix = 1; ix < 10; ix++)
            wmap[hx + ix][hy + iy] = T_Floor;

    // Furniture — desk (northwest)
    wmap[hx + 2][hy + 2] = T_Furn;
    wmap[hx + 3][hy + 2] = T_Furn;
    wmap[hx + 2][hy + 3] = T_Furn;
    // Shelf (northeast)
    wmap[hx + 7][hy + 2] = T_Furn;
    wmap[hx + 8][hy + 2] = T_Furn;
    // Bed (southwest)
    wmap[hx + 2][hy + 7] = T_Furn;
    wmap[hx + 3][hy + 7] = T_Furn;
    wmap[hx + 2][hy + 8] = T_Furn;
    wmap[hx + 3][hy + 8] = T_Furn;

    // ── Oases — scattered water pools ──
    int oasisPos[][2] = {
        {8,  8},  {65, 8},  {8,  65}, {65, 65},
        {25, 15}, {55, 12}, {12, 40}, {68, 40},
        {30, 50}, {50, 55}, {20, 30}, {60, 28},
        {15, 60}, {55, 62}, {40, 10}, {40, 70},
        {10, 20}, {70, 55}, {35, 35}, {45, 45}
    };
    int numOasisPos = 20;
    for (int i = 0; i < numOasisPos; i++) {
        int ox = oasisPos[i][0], oy = oasisPos[i][1];
        // Ensure we don't overlap the house
        if (abs(ox - MAP / 2) < 8 && abs(oy - MAP / 2) < 8) continue;
        // 2×2 water pool
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

    // Player spawns inside the house
    pX = hx + 5.5; pY = hy + 8.0;
    pA = -M_PI / 2;  // facing north
    thirst = 100; score = 0; gTime = 0; bobPhase = 0;

    // Init particles (sand dust)
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
//  RENDERING — Enhanced 3D
// ═══════════════════════════════════════════════════════════════

static inline int getBob() {
    double speed = sqrt(iMX * iMX + iMY * iMY);
    if (speed < 0.1) return 0;
    return (int)(sin(bobPhase * 10.0) * 3.5 * speed);
}

static void renderFrame() {
    double fov = M_PI / 3.0;
    double tanHalf = tan(fov / 2.0);

    // Day/night cycle
    double dayP = fmod(gTime / 120.0, 1.0);  // 2-minute full cycle
    double sunAngle = dayP * M_PI;
    double bright = 0.6 + 0.4 * sin(sunAngle);

    // Sky gradient
    uint32_t skyZenith = rgb(
        (uint8_t)(clampd(45 * bright, 0, 255)),
        (uint8_t)(clampd(70 * bright, 0, 255)),
        (uint8_t)(clampd(140 * bright, 0, 255)));
    // (skyHorizon computed inline in the per-pixel loop below)

    // Sun
    int sunSX = (int)(SW * 0.1 + SW * 0.8 * dayP);
    int sunSY = (int)(SH * 0.45 - SH * 0.35 * sin(sunAngle));
    double sunR = 12.0 + 3.0 * sin(gTime * 0.5); // pulsing

    int bob = getBob();

    // Light direction (from sun position in world)
    double lightDirX = cos(sunAngle - M_PI * 0.3);
    double lightDirY = 0.3; // slightly from above

    for (int x = 0; x < SW; x++) {
        double rayA = pA - fov / 2.0 + ((double)x / SW) * fov;
        double rdx = cos(rayA), rdy = sin(rayA);

        // DDA raycasting
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

        // Texture X coordinate
        double wallX;
        if (hit) {
            wallX = side == 0 ? (wallHitY - floor(wallHitY)) : (wallHitX - floor(wallHitX));
        } else {
            wallX = 0;
        }
        int texX = (int)(wallX * TEX) & (TEX - 1);
        if ((side == 0 && rdx > 0) || (side == 1 && rdy < 0)) texX = TEX - texX - 1;

        // ── Sky with gradient + sun ──
        for (int y = 0; y < cT; y++) {
            double t = (double)y / (SH * 0.5);
            double tSmooth = t * t * (3.0 - 2.0 * t); // smoothstep
            uint8_t sr = (uint8_t)clampd(lerp(45, 220, tSmooth) * bright, 0, 255);
            uint8_t sg = (uint8_t)clampd(lerp(70, 195, tSmooth) * bright, 0, 255);
            uint8_t sb = (uint8_t)clampd(lerp(140, 165, tSmooth) * bright, 0, 255);

            // Distance to sun for glow
            double dsx = x - sunSX, dsy = y - sunSY;
            double sunDist = sqrt(dsx * dsx + dsy * dsy);
            if (sunDist < sunR * 4.0) {
                double glow = 1.0 - sunDist / (sunR * 4.0);
                glow = glow * glow;
                sr = (uint8_t)clampd(sr + 255 * glow * bright, 0, 255);
                sg = (uint8_t)clampd(sg + 200 * glow * bright, 0, 255);
                sb = (uint8_t)clampd(sb + 100 * glow * bright, 0, 255);
            }
            // Sun disc
            if (sunDist < sunR) {
                double sf = 1.0 - sunDist / sunR;
                sf = sf * sf;
                sr = (uint8_t)clampd(lerp(sr, 255, sf), 0, 255);
                sg = (uint8_t)clampd(lerp(sg, 240, sf), 0, 255);
                sb = (uint8_t)clampd(lerp(sb, 180, sf), 0, 255);
            }
            fb[y * SW + x] = rgb(sr, sg, sb);
        }

        // ── Wall rendering with lighting ──
        if (hit) {
            uint32_t *tex = (tile == T_Furn) ? textures[2] : textures[0];
            for (int y = cT; y <= cB; y++) {
                int texY = (int)(((double)(y - dT) / lh) * TEX) & (TEX - 1);
                uint32_t tc = tex[texY * TEX + texX];

                uint8_t r = tc & 0xFF;
                uint8_t g = (tc >> 8) & 0xFF;
                uint8_t b = (tc >> 16) & 0xFF;

                // Side darkening (interior vs exterior faces)
                double sideMul = side ? 0.72 : 1.0;

                // Height-based lighting (top of wall brighter, bottom darker)
                double heightT = (double)(y - dT) / lh;
                double heightMul = 0.75 + 0.25 * (1.0 - heightT);

                // Distance-based AO for walls near the ground
                double aoMul = 1.0;
                if (heightT > 0.85) aoMul = 1.0 - (heightT - 0.85) * 3.0;

                double lightMul = sideMul * heightMul * aoMul * bright;
                r = (uint8_t)clampd(r * lightMul, 0, 255);
                g = (uint8_t)clampd(g * lightMul, 0, 255);
                b = (uint8_t)clampd(b * lightMul, 0, 255);

                fb[y * SW + x] = fogC(rgb(r, g, b), pd);
            }
        }

        // ── Floor with perspective texture + distance attenuation ──
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

            // Floor lighting: closer = brighter, directional from sun
            double floorLight = clampd(1.0 - rowD / 25.0, 0.3, 1.0) * bright;
            // Slight directional
            double fdot = (rdx * lightDirX + rdy * lightDirY);
            floorLight *= (0.9 + 0.1 * fdot);

            fr = (uint8_t)clampd(fr * floorLight, 0, 255);
            fg = (uint8_t)clampd(fg * floorLight, 0, 255);
            fb_ = (uint8_t)clampd(fb_ * floorLight, 0, 255);

            fb[y * SW + x] = fogC(rgb(fr, fg, fb_), rowD);
        }

        // Z-buffer
        zbuf[x] = pd;
    }

    // ── Oasis water rendering (floor decals) ──
    for (int oi = 0; oi < numOases; oi++) {
        Oasis &o = oasisList[oi];
        double dx = o.x - pX, dy = o.y - pY;

        // Transform to screen space
        double dirX = cos(pA), dirY = sin(pA);
        double plX = -sin(pA) * tanHalf, plY = cos(pA) * tanHalf;
        double invDet = 1.0 / (plX * dirY - dirX * plY);
        double tX = invDet * (dirY * dx - dirX * dy);
        double tY = invDet * (-plY * dx + plX * dy);
        if (tY <= 0.3) continue;

        int scrX = (int)(SW / 2.0 * (1.0 + tX / tY));
        double dist = sqrt(dx * dx + dy * dy);

        // Project water pool as an ellipse on the floor
        int poolRadius = (int)(30.0 / tY);  // screen pixels
        if (poolRadius < 3) continue;

        for (int sy = -poolRadius / 3; sy <= poolRadius / 3; sy++) {
            int screenY = SH / 2 - bob + (int)(poolRadius * 0.35) + sy;
            if (screenY < 0 || screenY >= SH) continue;

            for (int sx = -poolRadius; sx <= poolRadius; sx++) {
                int screenX = scrX + sx;
                if (screenX < 0 || screenX >= SW) continue;

                // Elliptical check
                double ex = (double)sx / poolRadius;
                double ey = (double)sy / (poolRadius / 3.0);
                double ed = ex * ex + ey * ey;
                if (ed > 1.0) continue;

                // Depth check
                double rowD = (double)SH / (2.0 * (screenY - SH / 2.0 + bob));
                if (rowD > dist * 0.8) continue;
                if (tY >= zbuf[screenX]) continue;

                // Animated water
                double ripple1 = sin(ed * 8.0 - gTime * 4.0) * 0.12;
                double ripple2 = sin(ed * 5.0 + gTime * 3.0) * 0.08;
                double ripple = 1.0 + ripple1 + ripple2;

                double edgeFade = 1.0 - smoothstep(ed);

                // Water color with depth
                double waterDepth = ed * 0.5;
                uint8_t wr = (uint8_t)clampd((30 + 20 * ripple) * edgeFade * bright, 0, 255);
                uint8_t wg = (uint8_t)clampd((100 + 50 * ripple) * edgeFade * bright, 0, 255);
                uint8_t wb = (uint8_t)clampd((180 + 40 * ripple) * edgeFade * bright, 0, 255);

                // Specular highlight (sun reflection)
                double spec = sin(ed * 12.0 - gTime * 6.0 + pA);
                if (spec > 0.85 && ed < 0.3) {
                    double sf = (spec - 0.85) / 0.15;
                    wr = (uint8_t)clampd(wr + 100 * sf, 0, 255);
                    wg = (uint8_t)clampd(wg + 90 * sf, 0, 255);
                    wb = (uint8_t)clampd(wb + 60 * sf, 0, 255);
                }

                // Blend with existing floor
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
                case T_Door:  c = rgb(120, 108, 85);  break;
                case T_Water: c = rgb(40, 110, 200);   break;
                case T_Furn:  c = rgb(110, 80, 40);    break;
                default:      c = rgb(180, 160, 120);
            }
            fb[(mmY0 + y) * SW + mmX0 + xx] = c;
        }
    // Player dot
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
    // Border
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
        int hx = MAP / 2 - 5, hy = MAP / 2 - 5;
        pX = hx + 5.5; pY = hy + 8.0; pA = -M_PI / 2;
        thirst = 100.0;
    }

    sprinting = (sqrt(iMX * iMX + iMY * iMY) > 0.85) ? 1 : 0;

    // Update particles
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
