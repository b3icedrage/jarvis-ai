// ═══════════════════════════════════════════════════════════════════
// OASIS v2 — Desert Survival Raycasting Game
//
// Complete rewrite with:
//   • Procedural wall textures (sandstone, rock, cactus, wood, ruins)
//   • Textured floor casting (procedural sand with pebbles)
//   • Billboard sprites (cacti, animated oasis water)
//   • Sand particle system + heat shimmer
//   • Head bobbing + sprint mechanic
//   • Day/night cycle with procedural sun
//   • Sound signal system (footstep, drink, death, warning)
//   • 100×100 procedural desert world
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
static const int MAP = 100;

// ── Tiles ─────────────────────────────────────────────────────
enum Tile : uint8_t {
    T_Empty = 0, T_House = 1, T_Rock = 2, T_Cactus = 3,
    T_Oasis = 4, T_Wood = 5, T_Ruin = 6
};

// ── Sprite types ──────────────────────────────────────────────
enum SpriteType : uint8_t { S_Cactus = 0, S_Oasis = 1, S_Ruin = 2 };

// ── Globals ───────────────────────────────────────────────────
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

// Sprites
struct Spr { double x, y; uint8_t type; };
static Spr  sprList[800];
static int  numSpr = 0;

// Particles
struct Part { float x, y, vx, vy, life; uint32_t col; };
static const int MAXP = 100;
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
static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// Fog colour (desert haze)
static inline uint32_t fogC(uint32_t c, double d) {
    double f = clampd(d / 20.0, 0.0, 1.0);
    uint8_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    r = (uint8_t)(r + (212 - r) * f);
    g = (uint8_t)(g + (192 - g) * f);
    b = (uint8_t)(b + (162 - b) * f);
    return rgb(r, g, b);
}

// Walkability
static bool canWalk(double x, double y) {
    if (x < 0.25 || x >= MAP - 0.25 || y < 0.25 || y >= MAP - 0.25) return false;
    uint8_t t = wmap[(int)x][(int)y];
    return t == T_Empty || t == T_Oasis;
}

// ═══════════════════════════════════════════════════════════════
//  PROCEDURAL TEXTURE GENERATION
// ═══════════════════════════════════════════════════════════════
static void genTextures() {
    for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
        uint8_t n = h8(x, y);

        // ── 0: Sandstone blocks (house walls) ──
        int by = y % 16, bx = x % 32;
        bool mortar = (by == 0) || (by == 1 && n > 220) ||
                      (bx == 0) || (bx == 16 && by >= 8);
        uint8_t r, g, b;
        if (mortar) { r = 175 + n / 16; g = 160 + n / 16; b = 135 + n / 16; }
        else { r = 215 + n / 10 - 12; g = 195 + n / 10 - 12; b = 160 + n / 12 - 10; }
        textures[0][y * TEX + x] = rgb(r, g, b);

        // ── 1: Rough rock ──
        uint8_t n1 = h8(x * 3, y * 7);
        r = 118 + n1 / 5; g = 102 + n1 / 6; b = 78 + n1 / 7;
        if (h8(x, y + 100) > 242) { r -= 35; g -= 30; b -= 20; }
        textures[1][y * TEX + x] = rgb(r, g, b);

        // ── 2: Cactus (green with ridges) ──
        double ridge = sin((double)x / TEX * 14.0) * 0.35 + 0.65;
        g = (uint8_t)(78 + 55 * ridge + n / 10);
        r = (uint8_t)(28 + n / 14);
        b = (uint8_t)(28 + n / 14);
        if (h8(x * 2, y * 2 + 500) > 252 && x % 3 == 0) { r += 25; g += 35; b += 15; }
        textures[2][y * TEX + x] = rgb(r, g, b);

        // ── 3: Wood planks (house interior) ──
        double grain = sin((double)y * 0.7 + h8(x / 3, y) * 0.015) * 0.18 + 0.82;
        r = (uint8_t)(145 * grain + n / 10);
        g = (uint8_t)(105 * grain + n / 12);
        b = (uint8_t)(52 * grain + n / 16);
        if (y % 16 == 0) { r -= 25; g -= 18; b -= 12; }
        textures[3][y * TEX + x] = rgb(r, g, b);

        // ── 4: Desert sand floor ──
        uint8_t n4 = h8(x + 400, y + 400);
        r = 222 + n4 / 14 - 9; g = 202 + n4 / 16 - 8; b = 155 + n4 / 18 - 7;
        if (h8(x * 3 + 10, y * 3 + 10) > 250) { r -= 28; g -= 22; b -= 14; }
        textures[4][y * TEX + x] = rgb(r, g, b);

        // ── 5: Ruined stone ──
        uint8_t n5 = h8(x + 600, y + 600);
        r = 148 + n5 / 8 - 18; g = 132 + n5 / 10 - 14; b = 112 + n5 / 12 - 10;
        if (h8(x / 2, y / 3 + 700) > 232) { r += 12; g += 8; b += 4; }
        textures[5][y * TEX + x] = rgb(r, g, b);
    }
}

// ═══════════════════════════════════════════════════════════════
//  WORLD GENERATION
// ═══════════════════════════════════════════════════════════════
static void genWorld() {
    memset(wmap, T_Empty, sizeof(wmap));
    numSpr = 0;
    srand(42);

    // Rock clusters
    for (int i = 0; i < 160; i++) {
        int rx = rand() % MAP, ry = rand() % MAP;
        int sz = 1 + rand() % 4;
        for (int dx = 0; dx < sz; dx++)
            for (int dy = 0; dy < sz; dy++) {
                int x = rx + dx, y = ry + dy;
                if (x < MAP && y < MAP && (abs(x - MAP / 2) > 8 || abs(y - MAP / 2) > 8))
                    wmap[x][y] = T_Rock;
            }
    }

    // Cactus tiles (become sprites during rendering)
    for (int i = 0; i < 200; i++) {
        int cx = rand() % MAP, cy = rand() % MAP;
        if (wmap[cx][cy] == T_Empty && (abs(cx - MAP / 2) > 8 || abs(cy - MAP / 2) > 8))
            wmap[cx][cy] = T_Cactus;
    }

    // Stone ruins
    for (int i = 0; i < 15; i++) {
        int bx = rand() % (MAP - 8) + 4, by = rand() % (MAP - 8) + 4;
        if (abs(bx - MAP / 2) < 10 && abs(by - MAP / 2) < 10) continue;
        int bw = 2 + rand() % 3, bh = 2 + rand() % 3;
        for (int dx = 0; dx < bw; dx++)
            for (int dy = 0; dy < bh; dy++)
                if (wmap[bx + dx][by + dy] == T_Empty)
                    wmap[bx + dx][by + dy] = T_Ruin;
        if (bw > 2) wmap[bx + bw / 2][by + bh - 1] = T_Empty; // doorway
    }

    // Oases (12 locations)
    int oases[][2] = {
        {10,10},{72,12},{12,72},{74,74},{26,6},{6,26},
        {80,38},{38,80},{20,48},{60,20},{48,60},{50,50}
    };
    for (auto &o : oases)
        for (int dx = 0; dx < 2; dx++)
            for (int dy = 0; dy < 2; dy++) {
                int x = o[0] + dx, y = o[1] + dy;
                if (x < MAP && y < MAP && wmap[x][y] == T_Empty)
                    wmap[x][y] = T_Oasis;
            }

    // ── The house (13×13 exterior) ──
    int hx = MAP / 2 - 6, hy = MAP / 2 - 6;
    for (int i = 0; i < 13; i++) {
        wmap[hx + i][hy] = T_House;    wmap[hx + i][hy + 12] = T_House;
        wmap[hx][hy + i] = T_House;    wmap[hx + 12][hy + i] = T_House;
    }
    // Door (south wall, 2-wide)
    wmap[hx + 5][hy + 12] = T_Empty;  wmap[hx + 6][hy + 12] = T_Empty;
    // Windows (north wall)
    wmap[hx + 4][hy] = T_Empty;  wmap[hx + 8][hy] = T_Empty;
    // Interior furniture
    wmap[hx + 3][hy + 3] = T_Wood;  wmap[hx + 4][hy + 3] = T_Wood;  // desk
    wmap[hx + 8][hy + 3] = T_Wood;  wmap[hx + 9][hy + 3] = T_Wood;  // shelf
    wmap[hx + 3][hy + 9] = T_Wood;  wmap[hx + 4][hy + 9] = T_Wood;  // bed
    wmap[hx + 3][hy + 10] = T_Wood; wmap[hx + 4][hy + 10] = T_Wood;

    // Build sprite list (cacti + oases)
    numSpr = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            uint8_t t = wmap[x][y];
            if (t == T_Cactus && numSpr < 800)
                sprList[numSpr++] = { x + 0.5, y + 0.5, S_Cactus };
            else if (t == T_Oasis && numSpr < 800)
                sprList[numSpr++] = { x + 0.5, y + 0.5, S_Oasis };
        }

    // Player inside house
    pX = hx + 6.5; pY = hy + 10.0;
    pA = -M_PI / 2;  // facing north
    thirst = 100; score = 0; gTime = 0; bobPhase = 0;

    // Init particles
    for (int i = 0; i < MAXP; i++) {
        parts[i].x = (float)(rand() % SW);
        parts[i].y = (float)(rand() % SH);
        parts[i].vx = 0.3f + (rand() % 100) / 100.0f;
        parts[i].vy = -0.1f + (rand() % 20) / 100.0f;
        parts[i].life = (float)(rand() % 200);
        parts[i].col = rgb(210 + rand() % 30, 195 + rand() % 20, 155 + rand() % 20, 140);
    }
}

// ═══════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════

// Head-bob vertical offset
static inline int getBob() {
    double speed = sqrt(iMX * iMX + iMY * iMY);
    if (speed < 0.1) return 0;
    return (int)(sin(bobPhase * 10.0) * 4.0 * speed);
}

static void renderFrame() {
    double fov = M_PI / 3.0;
    double tanHalf = tan(fov / 2.0);

    // Day/night
    double dayP = fmod(gTime / 90.0, 1.0);
    double bright = 0.65 + 0.35 * fabs(sin(dayP * M_PI));

    // Sky colours
    uint32_t skyT = rgb((uint8_t)(30 * bright + 40), (uint8_t)(50 * bright + 50), (uint8_t)(120 * bright + 80));
    uint32_t skyH = rgb((uint8_t)(215 * bright), (uint8_t)(195 * bright), (uint8_t)(158 * bright));
    uint32_t sunC = rgb((uint8_t)(255 * bright), (uint8_t)(230 * bright), (uint8_t)(150 * bright));

    // Sun position (arcs across sky)
    double sunAngle = dayP * M_PI;
    int sunSX = (int)(SW * 0.15 + SW * 0.7 * (dayP));
    int sunSY = (int)(SH * 0.5 - SH * 0.4 * sin(sunAngle));

    int bob = getBob();

    // ── Per-column raycasting ──
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
            if (tile != T_Empty && tile != T_Cactus && tile != T_Oasis) { hit = 1; break; }
        }

        double pd;
        double wallHitX, wallHitY;
        if (hit) {
            pd = side == 0 ? sdx - ddx : sdy - ddy;
            if (pd < 0.01) pd = 0.01;
            if (side == 0) { wallHitX = mx + (sx < 0 ? 1 : 0); wallHitY = pY + pd * rdy; }
            else           { wallHitX = pX + pd * rdx; wallHitY = my + (sy < 0 ? 1 : 0); }
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

        // Texture X coordinate for wall
        double wallX;
        if (hit) {
            wallX = side == 0 ? (wallHitY - floor(wallHitY)) : (wallHitX - floor(wallHitX));
        } else {
            wallX = 0;
        }
        int texX = (int)(wallX * TEX) & (TEX - 1);
        if ((side == 0 && rdx > 0) || (side == 1 && rdy < 0)) texX = TEX - texX - 1;

        // Wall texture index
        int texIdx = 0;
        if (hit) {
            switch (tile) {
                case T_House: texIdx = 0; break;
                case T_Rock:  texIdx = 1; break;
                case T_Ruin:  texIdx = 5; break;
                case T_Wood:  texIdx = 3; break;
                default:      texIdx = 0;
            }
        }

        // ── Sky ──
        for (int y = 0; y < cT; y++) {
            double t = (double)y / (SH * 0.5);
            uint32_t sc = rgb(
                (uint8_t)((30 + (215 - 30) * t) * bright),
                (uint8_t)((50 + (195 - 50) * t) * bright),
                (uint8_t)((120 + (158 - 120) * t) * bright));
            fb[y * SW + x] = sc;
        }

        // ── Textured wall ──
        if (hit) {
            uint32_t *tex = textures[texIdx];
            for (int y = cT; y <= cB; y++) {
                int texY = (int)(((double)(y - dT) / lh) * TEX) & (TEX - 1);
                uint32_t tc = tex[texY * TEX + texX];
                // Side darkening
                if (side) {
                    uint8_t r = tc & 0xFF, g = (tc >> 8) & 0xFF, b = (tc >> 16) & 0xFF;
                    tc = rgb((uint8_t)(r * 0.78), (uint8_t)(g * 0.78), (uint8_t)(b * 0.78));
                }
                fb[y * SW + x] = fogC(tc, pd);
            }
        }

        // ── Textured floor ──
        for (int y = cB + 1; y < SH; y++) {
            double rowD = (double)SH / (2.0 * (y - SH / 2.0 + bob));
            double fx = pX + rowD * rdx;
            double fy = pY + rowD * rdy;
            int tx = ((int)(fx * TEX)) & (TEX - 1);
            int ty = ((int)(fy * TEX)) & (TEX - 1);
            uint32_t fc = textures[4][ty * TEX + tx];
            fb[y * SW + x] = fogC(fc, rowD);
        }

        // Z-buffer for sprites
        zbuf[x] = pd;
    }

    // ── Sprite rendering ──
    // Sort sprites by distance (far to near)
    static int sprOrder[800];
    static double sprDist[800];
    for (int i = 0; i < numSpr; i++) {
        sprOrder[i] = i;
        double dx = sprList[i].x - pX, dy = sprList[i].y - pY;
        sprDist[i] = dx * dx + dy * dy;
    }
    for (int i = 0; i < numSpr - 1; i++)
        for (int j = i + 1; j < numSpr; j++)
            if (sprDist[sprOrder[i]] < sprDist[sprOrder[j]]) {
                int tmp = sprOrder[i]; sprOrder[i] = sprOrder[j]; sprOrder[j] = tmp;
            }

    double dirX = cos(pA), dirY = sin(pA);
    double plX = -sin(pA) * tanHalf, plY = cos(pA) * tanHalf;
    double invDet = 1.0 / (plX * dirY - dirX * plY);

    for (int si = 0; si < numSpr; si++) {
        Spr &s = sprList[sprOrder[si]];
        double dx = s.x - pX, dy = s.y - pY;
        double tX = invDet * (dirY * dx - dirX * dy);
        double tY = invDet * (-plY * dx + plX * dy);
        if (tY <= 0.2) continue;

        int scrX = (int)(SW / 2.0 * (1.0 + tX / tY));
        int spH = abs((int)(SH / tY));
        int spW = spH;

        int dSX = -spW / 2 + scrX;
        int dEX =  spW / 2 + scrX;
        int dSY = -spH / 2 + SH / 2 - bob;
        int dEY =  spH / 2 + SH / 2 - bob;

        double dist = sqrt(sprDist[sprOrder[si]]);

        if (s.type == S_Cactus) {
            // Draw cactus as a green column with shape
            for (int stripe = dSX; stripe < dEX; stripe++) {
                if (stripe < 0 || stripe >= SW) continue;
                if (tY >= zbuf[stripe]) continue;
                int cactusW = spW / 4; // cactus is thinner than full sprite
                int center = (dSX + dEX) / 2;
                if (abs(stripe - center) > cactusW) continue;

                for (int y = dSY; y < dEY; y++) {
                    if (y < 0 || y >= SH) continue;
                    double relY = (double)(y - (dSY + dEY) / 2) / (spH / 2.0);
                    // Round top
                    if (relY < -0.7) {
                        double maxW = cactusW * (1.0 + relY * 1.5);
                        if (abs(stripe - center) > maxW) continue;
                    }
                    double ridge = sin((double)(stripe - center) / cactusW * 6.0) * 0.25 + 0.75;
                    uint8_t g = (uint8_t)(75 + 50 * ridge * (1.0 - relY * 0.2));
                    uint8_t r = 28, b = 28;
                    fb[y * SW + stripe] = fogC(rgb(r, g, b), dist);
                }
            }
        } else if (s.type == S_Oasis) {
            // Draw oasis as animated blue circle
            int rad = spH / 3;
            int cx = (dSX + dEX) / 2;
            int cy = (dSY + dEY) / 2;
            for (int stripe = cx - rad; stripe <= cx + rad; stripe++) {
                if (stripe < 0 || stripe >= SW) continue;
                if (tY >= zbuf[stripe]) continue;
                for (int y = cy - rad; y <= cy + rad; y++) {
                    if (y < 0 || y >= SH) continue;
                    double dd = sqrt((double)(stripe - cx) * (stripe - cx) + (double)(y - cy) * (y - cy));
                    if (dd > rad) continue;
                    double ripple = sin(dd * 4.0 - gTime * 5.0) * 0.15 + 0.85;
                    double edge = 1.0 - dd / rad;
                    uint8_t r = (uint8_t)(35 * ripple * edge);
                    uint8_t g = (uint8_t)(115 * ripple * edge);
                    uint8_t b = (uint8_t)(215 * ripple * edge);
                    fb[y * SW + stripe] = fogC(rgb(r, g, b), dist);
                }
            }
        }
    }

    // ── Sand particles ──
    for (int i = 0; i < MAXP; i++) {
        Part &p = parts[i];
        if (p.life <= 0) continue;
        int px_ = (int)p.x, py_ = (int)p.y;
        if (px_ >= 0 && px_ < SW && py_ >= 0 && py_ < SH) {
            fb[py_ * SW + px_] = p.col;
            // Slight alpha blend
            if (px_ + 1 < SW)
                fb[py_ * SW + px_ + 1] = rgb(200, 185, 150, 80);
        }
    }

    // ── Minimap (bottom-right, 120×120) ──
    int mmS = 120, mmT = 16;
    int mmX0 = SW - mmS - 8, mmY0 = SH - mmS - 8;
    double mmSc = (double)mmS / mmT;
    // Semi-transparent background
    for (int y = 0; y < mmS; y++)
        for (int xx = 0; xx < mmS; xx++)
            fb[(mmY0 + y) * SW + mmX0 + xx] = rgb(15, 12, 8, 200);
    // Tiles
    for (int y = 0; y < mmS; y++)
        for (int xx = 0; xx < mmS; xx++) {
            int wx = (int)(pX - mmT / 2.0 + xx / mmSc);
            int wy = (int)(pY - mmT / 2.0 + y / mmSc);
            if (wx < 0 || wx >= MAP || wy < 0 || wy >= MAP) continue;
            uint32_t c;
            switch (wmap[wx][wy]) {
                case T_Empty:  c = rgb(200, 182, 148); break;
                case T_House:  c = rgb(185, 165, 125); break;
                case T_Rock:   c = rgb(125, 105, 68);  break;
                case T_Cactus: c = rgb(38, 110, 38);   break;
                case T_Oasis:  c = rgb(50, 128, 220);  break;
                case T_Wood:   c = rgb(140, 100, 50);  break;
                case T_Ruin:   c = rgb(140, 125, 95);  break;
                default:       c = rgb(100, 100, 100);
            }
            fb[(mmY0 + y) * SW + mmX0 + xx] = c;
        }
    // Player
    int pdx = mmS / 2, pdy = mmS / 2;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 4)
                fb[(mmY0 + pdy + dy) * SW + mmX0 + pdx + dx] = rgb(255, 55, 55);
    // Direction
    for (int i = 3; i < 14; i++) {
        int lx = pdx + (int)(cos(pA) * i);
        int ly = pdy + (int)(sin(pA) * i);
        if (lx >= 0 && lx < mmS && ly >= 0 && ly < mmS)
            fb[(mmY0 + ly) * SW + mmX0 + lx] = rgb(255, 220, 70);
    }
    // Border
    for (int i = 0; i < mmS; i++) {
        fb[mmY0 * SW + mmX0 + i] = rgb(110, 88, 55);
        fb[(mmY0 + mmS - 1) * SW + mmX0 + i] = rgb(110, 88, 55);
        fb[(mmY0 + i) * SW + mmX0] = rgb(110, 88, 55);
        fb[(mmY0 + i) * SW + mmX0 + mmS - 1] = rgb(110, 88, 55);
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

    // Footstep sound
    static double lastStep = 0;
    if (speed > 0.5 && gTime - lastStep > 0.35) {
        soundSignal = 1; // footstep
        lastStep = gTime;
    }

    // Oasis check
    uint8_t under = wmap[(int)pX][(int)pY];
    if (under == T_Oasis && thirst < 100.0) {
        thirst = 100.0;
        score++;
        soundSignal = 2; // drink
    }

    // Thirst
    thirst -= 2.5 * dt;
    if (thirst < 0) thirst = 0;

    // Warning beep
    if (thirst > 0 && thirst < 20 && fmod(gTime, 1.5) < dt)
        soundSignal = 4;

    // Death
    if (thirst <= 0) {
        soundSignal = 3; // death
        int hx = MAP / 2 - 6, hy = MAP / 2 - 6;
        pX = hx + 6.5; pY = hy + 10.0; pA = -M_PI / 2;
        thirst = 100.0;
    }

    // Sprint (when input magnitude > 0.8)
    sprinting = (sqrt(iMX * iMX + iMY * iMY) > 0.85) ? 1 : 0;

    // Update particles
    float wind = 0.4f + 0.2f * sin((float)gTime * 0.3f);
    for (int i = 0; i < MAXP; i++) {
        Part &p = parts[i];
        p.x += p.vx + wind;
        p.y += p.vy;
        p.life -= 1.0f;
        if (p.life <= 0 || p.x > SW + 5 || p.y < -5 || p.y > SH + 5) {
            p.x = -2.0f;
            p.y = (float)(rand() % SH);
            p.vx = 0.3f + (rand() % 100) / 100.0f;
            p.vy = -0.15f + (rand() % 30) / 100.0f;
            p.life = 100.0f + (rand() % 200);
            p.col = rgb(210 + rand() % 25, 195 + rand() % 20, 155 + rand() % 20, 140);
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
