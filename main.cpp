// ═══════════════════════════════════════════════════════════════════
// OASIS v7 — Ready Player One Inspired Neon Cyberpunk Raycaster
//
// Deep space sky, neon grid floors, cyberpunk walls with glow edges,
// holographic portals, planets, nebula, lens flares, digital fog.
//
// Compiled to WebAssembly via Emscripten → runs in any browser.
// ═══════════════════════════════════════════════════════════════════

#include <emscripten/emscripten.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

static const int SW = 800, SH = 450;
static const int TEX = 64;
static const int MAP = 80;

enum Tile : uint8_t {
    T_Empty = 0, T_Wall = 1, T_Floor = 2, T_Door = 3, T_Water = 4, T_Furn = 5
};

static uint32_t textures[6][TEX * TEX];
static uint8_t  wmap[MAP][MAP];
static uint32_t fb[SW * SH];
static uint32_t fbPost[SW * SH]; // post-process buffer
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
static double doorOpen = 0.0;
static double doorTarget = 0.0;

// House bounds
static int houseX0, houseY0, houseX1, houseY1;

// Oasis
struct Oasis { double x, y; };
static Oasis oasisList[30];
static int   numOases = 0;

// ── VFX ──
static double shakeAmount = 0.0;   // screen shake intensity (decays)
static double shakeX = 0.0, shakeY = 0.0; // current shake offset
static double heatTime = 0.0;      // heat shimmer phase
static int    wasSprinting = 0;    // for sprint-start shake

// Particles
struct Part {
    float x, y, vx, vy, life, maxLife;
    uint32_t col;
    uint8_t type; // 0=sand, 1=death, 2=splash, 3=trail, 4=dust devil
};
static const int MAXP = 200;
static Part parts[MAXP];

// View mode
static int thirdPerson = 0; // 0=first person, 1=third person
static double camX, camY; // camera position (offset from player in 3rd person)

// Collectibles
struct Item { double x,y; uint8_t type; int active; };
static Item itemList[40]; static int numItems=0;

// Stars
struct Star { float x,y,brightness,twinklePhase; };
static Star starList[120]; static int numStars=0;

// Sandstorm
static double sandstormIntensity=0.0;
static double sandstormTimer=30.0;
static int    sandstormActive=0;

// Minimap fog of war
static uint8_t fogMap[MAP][MAP];

// Score combo
static int comboCount=0;
static double comboTimer=0.0;
static int bestCombo=0;

// Sound
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
static inline uint32_t fogC(uint32_t c, double d) {
    double f = clampd(d / 18.0, 0.0, 1.0); f *= f;
    uint8_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    r = (uint8_t)(r + (15 - r) * f);  // fog: deep space purple-black
    g = (uint8_t)(g + (5 - g) * f);
    b = (uint8_t)(b + (25 - b) * f);
    return rgb(r, g, b);
}
static inline int maxI(int a, int b) { return a > b ? a : b; }
static inline uint32_t blendPixel(uint32_t bg, uint32_t fg) {
    uint8_t a = (fg >> 24);
    if (a == 0) return bg;
    double af = a / 255.0;
    uint8_t br = bg & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = (bg >> 16) & 0xFF;
    uint8_t fr = fg & 0xFF, fg_ = (fg >> 8) & 0xFF, fb_ = (fg >> 16) & 0xFF;
    return rgb(
        (uint8_t)(br + (fr - br) * af),
        (uint8_t)(bg_ + (fg_ - bg_) * af),
        (uint8_t)(bb + (fb_ - bb) * af));
}

static bool canWalk(double x, double y) {
    if (x < 0.25 || x >= MAP - 0.25 || y < 0.25 || y >= MAP - 0.25) return false;
    uint8_t t = wmap[(int)x][(int)y];
    return t == T_Empty || t == T_Door || t == T_Water || t == T_Floor;
}
static bool isInsideHouse(double x, double y) {
    int ix = (int)x, iy = (int)y;
    return ix > houseX0 && ix < houseX1 && iy > houseY0 && iy < houseY1;
}

// ═══════════════════════════════════════════════════════════════
//  PROCEDURAL TEXTURES
// ═══════════════════════════════════════════════════════════════
static void genTextures() {
    for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
        uint8_t n = h8(x, y), n2 = h8(x*3+7, y*5+13), n3 = h8(x*7+31, y*11+97);
        // 0: Cyberpunk wall — dark panel with neon cyan edge glow
        {
          bool edge=(x<2)||(x>=62)||(y<2)||(y>=62);
          bool gridH=(y%16==0), gridV=(x%16==0);
          bool isEdge=edge||gridH||gridV;
          double glow=0;
          if(isEdge){double ex_=x<2?(2.0-x)/2.0:(x>=62?(x-62.0)/2.0:0);
                     double ey_=y<2?(2.0-y)/2.0:(y>=62?(y-62.0)/2.0:0);
                     glow=1.0-std::min(ex_,ey_)*0.5; glow*=glow;}
          uint8_t baseR=18+n/8, baseG=12+n/10, baseB=28+n/6;
          uint8_t nr=(uint8_t)clampd(baseR+glow*120,0,255);
          uint8_t ng=(uint8_t)clampd(baseG+glow*220,0,255);
          uint8_t nb=(uint8_t)clampd(baseB+glow*255,0,255);
          // Circuit trace pattern
          if(!isEdge&&n2>200&&n3>180){nr=(uint8_t)clampd(nr+15,0,255);ng=(uint8_t)clampd(ng+30,0,255);nb=(uint8_t)clampd(nb+40,0,255);}
          textures[0][y*TEX+x]=rgb(nr,ng,nb);
        }
        // 1: Interior floor — dark with cyan grid lines
        {
          bool gx=(x%8==0), gy=(y%8==0);
          double dx_=(x-32.0)/32.0, dy_=(y-32.0)/32.0;
          double dist=sqrt(dx_*dx_+dy_*dy_);
          uint8_t r=15+n/12, g=10+n/14, b=22+n/8;
          if(gx||gy){r=20;g=60+(uint8_t)(sin((double)x*0.5)*20);b=120+(uint8_t)(sin((double)y*0.3)*30);}
          // Center glow
          double cg=1.0-dist*1.5; if(cg>0){cg*=cg;r=(uint8_t)clampd(r+cg*20,0,255);g=(uint8_t)clampd(g+cg*40,0,255);b=(uint8_t)clampd(b+cg*60,0,255);}
          textures[1][y*TEX+x]=rgb(r,g,b);
        }
        // 2: Furniture → server rack / tech block
        {
          bool slot=(y%8>=1&&y%8<=3);
          bool led=(x%16==8&&y%16==4);
          uint8_t r=12+n/10, g=10+n/12, b=18+n/8;
          if(slot){r=25;g=22;b=35;}
          if(led){r=40;g=200;b=100;} // green LED indicator
          textures[2][y*TEX+x]=rgb(r,g,b);
        }
        // 3: Floor grid — dark with neon purple/cyan grid lines
        {
          bool gx=(x%16==0), gy=(y%16==0);
          double dx_=(double)x/TEX, dy_=(double)y/TEX;
          uint8_t r=8+n/14, g=5+n/16, b=15+n/10;
          if(gx){uint8_t gv=(uint8_t)(sin(dy_*6.28)*20+40);r=10;g=gv;b=(uint8_t)(120+sin(dy_*3.14)*40);}
          if(gy){uint8_t gv=(uint8_t)(sin(dx_*6.28)*20+40);r=10;g=gv;b=(uint8_t)(120+sin(dx_*3.14)*40);}
          textures[3][y*TEX+x]=rgb(r,g,b);
        }
        // 4: Ceiling — dark with glowing panels
        {
          bool panel=(x%32>=1&&x%32<=30&&y%32>=1&&y%32<=30);
          bool edge_=(x%32==0||x%32==31||y%32==0||y%32==31);
          uint8_t r=10+n/12, g=8+n/14, b=20+n/8;
          if(edge_){r=15;g=40;b=100;}
          if(panel){double px_=(x%32-16.0)/16.0,py_=(y%32-16.0)/16.0;
                    double pg=1.0-(px_*px_+py_*py_)*0.5; if(pg<0)pg=0;
                    r=(uint8_t)clampd(r+pg*25,0,255);g=(uint8_t)clampd(g+pg*50,0,255);b=(uint8_t)clampd(b+pg*80,0,255);}
          textures[4][y*TEX+x]=rgb(r,g,b);
        }
        // 5: Door — holographic energy gate
        {
          double dx_=(x-32.0)/32.0, dy_=(y-32.0)/32.0;
          double ring=sqrt(dx_*dx_+dy_*dy_);
          double pulse=sin((double)y*0.2)*0.3+0.7;
          uint8_t r=10, g=(uint8_t)(30+ring*80), b=(uint8_t)(80+ring*120);
          if(ring>0.85&&ring<1.0){double glow_=1.0-fabs(ring-0.925)/0.075;
            r=(uint8_t)clampd(r+glow_*100,0,255);g=(uint8_t)clampd(g+glow_*200,0,255);b=(uint8_t)clampd(b+glow_*255,0,255);}
          if(ring<0.85){double inner=sin(ring*20-gTime*4)*0.15+0.85;
            r=(uint8_t)(r*inner*pulse);g=(uint8_t)(g*inner*pulse);b=(uint8_t)(b*inner*pulse);}
          textures[5][y*TEX+x]=rgb(r,g,b);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  WORLD GENERATION
// ═══════════════════════════════════════════════════════════════
static void genWorld() {
    memset(wmap, T_Empty, sizeof(wmap));
    numOases = 0; doorOpen = 0; doorTarget = 0;
    shakeAmount = 0; wasSprinting = 0;
    srand(42);

    int hx = MAP/2-5, hy = MAP/2-5;
    houseX0=hx; houseY0=hy; houseX1=hx+10; houseY1=hy+10;

    for (int i=0;i<11;i++){wmap[hx+i][hy]=T_Wall;wmap[hx+i][hy+10]=T_Wall;wmap[hx][hy+i]=T_Wall;wmap[hx+10][hy+i]=T_Wall;}
    wmap[hx+4][hy+10]=T_Door; wmap[hx+5][hy+10]=T_Door;
    wmap[hx+3][hy]=T_Door; wmap[hx+7][hy]=T_Door; wmap[hx+10][hy+4]=T_Door;

    for(int iy=1;iy<10;iy++)for(int ix=1;ix<10;ix++)wmap[hx+ix][hy+iy]=T_Floor;
    wmap[hx+2][hy+2]=T_Furn;wmap[hx+3][hy+2]=T_Furn;wmap[hx+2][hy+3]=T_Furn;
    wmap[hx+7][hy+2]=T_Furn;wmap[hx+8][hy+2]=T_Furn;
    wmap[hx+2][hy+7]=T_Furn;wmap[hx+3][hy+7]=T_Furn;wmap[hx+2][hy+8]=T_Furn;wmap[hx+3][hy+8]=T_Furn;

    int op[][2]={{8,8},{65,8},{8,65},{65,65},{25,15},{55,12},{12,40},{68,40},{30,50},{50,55},{20,30},{60,28},{15,60},{55,62},{40,10},{40,70},{10,20},{70,55},{35,35},{45,45}};
    for(int i=0;i<20;i++){int ox=op[i][0],oy=op[i][1];if(abs(ox-MAP/2)<8&&abs(oy-MAP/2)<8)continue;
        for(int dx=0;dx<2;dx++)for(int dy=0;dy<2;dy++){int wx=ox+dx,wy=oy+dy;if(wx>=0&&wx<MAP&&wy>=0&&wy<MAP)wmap[wx][wy]=T_Water;}
        if(numOases<30){oasisList[numOases].x=ox+1.0;oasisList[numOases].y=oy+1.0;numOases++;}}

    pX=hx+5.5;pY=hy+8.0;pA=-M_PI/2;thirst=100;score=0;gTime=0;bobPhase=0;
    comboCount=0;comboTimer=0;bestCombo=0;sandstormTimer=25.0;sandstormActive=0;sandstormIntensity=0;

    // Generate stars
    numStars=0;
    for(int i=0;i<120&&numStars<120;i++){
        starList[numStars].x=(float)((rand()%1000)/1000.0*SW);
        starList[numStars].y=(float)((rand()%200)/200.0*(SH*0.4));
        starList[numStars].brightness=0.3f+(float)(rand()%700)/1000.0f;
        starList[numStars].twinklePhase=(float)(rand()%628)/100.0f;
        numStars++;
    }

    // Scatter collectible items
    numItems=0;
    // type: 0=water_bottle, 1=compass, 2=flower, 3=gold
    for(int i=0;i<30&&numItems<40;i++){
        double ix=10.0+(rand()%(MAP-20));
        double iy=10.0+(rand()%(MAP-20));
        if(abs((int)ix-MAP/2)<8&&abs((int)iy-MAP/2)<8)continue;
        if(wmap[(int)ix][(int)iy]!=T_Empty)continue;
        uint8_t itype=(uint8_t)(rand()%4);
        itemList[numItems].x=ix;itemList[numItems].y=iy;
        itemList[numItems].type=itype;itemList[numItems].active=1;
        numItems++;
    }

    // Init fog of war (all unexplored except house)
    memset(fogMap,0,sizeof(fogMap));
    for(int fy=hy-1;fy<=hy+11;fy++)for(int fx=hx-1;fx<=hx+11;fx++)
        if(fx>=0&&fx<MAP&&fy>=0&&fy<MAP)fogMap[fx][fy]=1;

    for(int i=0;i<MAXP;i++){parts[i]={0};parts[i].life=0;}
}

// ═══════════════════════════════════════════════════════════════
//  PARTICLE SPAWNERS
// ═══════════════════════════════════════════════════════════════
static int nextParticle() {
    for(int i=0;i<MAXP;i++) if(parts[i].life<=0) return i;
    return -1;
}
static void spawnDeathBurst(double sx, double sy) {
    for(int i=0;i<40;i++){
        int idx=nextParticle(); if(idx<0)break;
        double a=(double)(rand()%628)/100.0;
        double spd=1.0+(double)(rand()%300)/100.0;
        parts[idx]={0};
        parts[idx].x=(float)sx; parts[idx].y=(float)sy;
        parts[idx].vx=(float)(cos(a)*spd); parts[idx].vy=(float)(sin(a)*spd);
        parts[idx].life=60.0f+(float)(rand()%40); parts[idx].maxLife=parts[idx].life;
        parts[idx].col=rgb(255,(uint8_t)(100+rand()%100),(uint8_t)(30+rand()%30));
        parts[idx].type=1;
    }
}
static void spawnSplash(double sx, double sy) {
    for(int i=0;i<15;i++){
        int idx=nextParticle(); if(idx<0)break;
        double a=(double)(rand()%628)/100.0;
        double spd=0.5+(double)(rand()%200)/100.0;
        parts[idx]={0};
        parts[idx].x=(float)sx; parts[idx].y=(float)sy;
        parts[idx].vx=(float)(cos(a)*spd); parts[idx].vy=(float)(-fabs(sin(a))*spd*1.5);
        parts[idx].life=30.0f+(float)(rand()%20); parts[idx].maxLife=parts[idx].life;
        parts[idx].col=rgb(80,160,240);
        parts[idx].type=2;
    }
}
static void spawnTrail(double sx, double sy, double dx, double dy) {
    int idx=nextParticle(); if(idx<0)return;
    parts[idx]={0};
    parts[idx].x=(float)(sx-dx*0.3); parts[idx].y=(float)(sy-dy*0.3);
    parts[idx].vx=(float)((rand()%40-20)/100.0); parts[idx].vy=(float)((rand()%40-20)/100.0);
    parts[idx].life=25.0f+(float)(rand()%15); parts[idx].maxLife=parts[idx].life;
    parts[idx].col=rgb(200,180,140);
    parts[idx].type=3;
}

// ═══════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════
static inline int getBob() {
    double speed = sqrt(iMX*iMX+iMY*iMY);
    if(speed<0.1)return 0;
    return (int)(sin(bobPhase*10.0)*3.5*speed);
}

static void renderFrame() {
    double fov=M_PI/3.0, tanHalf=tan(fov/2.0);
    double dayP=fmod(gTime/120.0,1.0), sunAngle=dayP*M_PI;
    double bright=0.6+0.4*sin(sunAngle);
    int sunSX=(int)(SW*0.1+SW*0.8*dayP), sunSY=(int)(SH*0.45-SH*0.35*sin(sunAngle));
    double sunR=12.0+3.0*sin(gTime*0.5);
    int bob=getBob();
    double lightDirX=cos(sunAngle-M_PI*0.3), lightDirY=0.3;
    bool playerIndoors=isInsideHouse(pX,pY);

    // Screen shake offset
    int shkX=(int)shakeX, shkY=(int)shakeY;

    // Camera position: in third-person, offset behind and above the player
    if(thirdPerson){
        double backDist=4.0, upDist=1.5;
        camX=pX-cos(pA)*backDist;
        camY=pY-sin(pA)*backDist;
        // Clamp camera to walkable area
        if(!canWalk(camX,camY)){camX=pX;camY=pY;}
    }else{
        camX=pX; camY=pY;
    }

    for(int x=0;x<SW;x++){
        double rayA=pA-fov/2.0+((double)x/SW)*fov;
        double rdx=cos(rayA),rdy=sin(rayA);
        int mx=(int)camX,my=(int)camY;
        double ddx=rdx==0?1e30:fabs(1.0/rdx), ddy=rdy==0?1e30:fabs(1.0/rdy);
        double sdx,sdy; int sx,sy;
        if(rdx<0){sx=-1;sdx=(camX-mx)*ddx;}else{sx=1;sdx=(mx+1.0-camX)*ddx;}
        if(rdy<0){sy=-1;sdy=(camY-my)*ddy;}else{sy=1;sdy=(my+1.0-camY)*ddy;}
        int side=0,hit=0; uint8_t tile=0;
        for(int step=0;step<128;step++){
            if(sdx<sdy){sdx+=ddx;mx+=sx;side=0;}else{sdy+=ddy;my+=sy;side=1;}
            if(mx<0||mx>=MAP||my<0||my>=MAP)break;
            tile=wmap[mx][my];
            if(tile==T_Wall||tile==T_Furn){hit=1;break;}
            if(tile==T_Door&&doorOpen<0.85){hit=1;break;}
        }
        double pd; double wallHitX,wallHitY;
        if(hit){pd=side==0?sdx-ddx:sdy-ddy;if(pd<0.01)pd=0.01;
            if(side==0){wallHitX=mx+(sx<0?1.0:0.0);wallHitY=camY+pd*rdy;}else{wallHitX=camX+pd*rdx;wallHitY=my+(sy<0?1.0:0.0);}
        }else{pd=50.0;wallHitX=camX+pd*rdx;wallHitY=camY+pd*rdy;}
        int lh=(int)(SH/pd), dT=-lh/2+SH/2-bob, dB=lh/2+SH/2-bob;
        int cT=dT<0?0:dT, cB=dB>=SH?SH-1:dB;
        double wallX=hit?(side==0?(wallHitY-floor(wallHitY)):(wallHitX-floor(wallHitX))):0;
        int texX=(int)(wallX*TEX)&(TEX-1);
        if((side==0&&rdx>0)||(side==1&&rdy<0))texX=TEX-texX-1;

        // Sky or ceiling
        if(playerIndoors){
            for(int y=0;y<cT;y++){
                double rowD=(double)SH/(2.0*(SH/2.0-y+bob));if(rowD<0.1)rowD=0.1;
                double cx_=camX+rowD*rdx,cy_=camY+rowD*rdy;
                int tx=((int)(cx_*TEX))&(TEX-1),ty=((int)(cy_*TEX))&(TEX-1);
                uint32_t cc=textures[4][ty*TEX+tx];uint8_t cr=cc&0xFF,cg=(cc>>8)&0xFF,cb=(cc>>16)&0xFF;
                double cl=clampd(0.55-rowD/30.0,0.2,0.55)*bright;
                cr=(uint8_t)clampd(cr*cl,0,255);cg=(uint8_t)clampd(cg*cl,0,255);cb=(uint8_t)clampd(cb*cl,0,255);
                fb[y*SW+x]=fogC(rgb(cr,cg,cb),rowD);
            }
        }else{
            for(int y=0;y<cT;y++){
                double t=(double)y/(SH*0.5);
                // Deep space gradient: black → dark purple → faint blue at horizon
                uint8_t sr=(uint8_t)clampd(lerp(3,12,t)*bright,0,255);
                uint8_t sg=(uint8_t)clampd(lerp(1,5,t)*bright,0,255);
                uint8_t sb=(uint8_t)clampd(lerp(8,35,t)*bright,0,255);
                // Nebula clouds (purple/cyan haze)
                {
                    double nX=(double)x*0.004+gTime*0.003;
                    double nY=(double)y*0.006;
                    double nn=sin(nX*2.1+nY*1.3)*0.5+sin(nX*4.7-nY*2.8+2.1)*0.25+sin(nX*7.3+nY*5.1+4.5)*0.125;
                    double nebulaMask=clampd((nn+0.3)*1.5,0,1); nebulaMask*=nebulaMask;
                    sr=(uint8_t)clampd(sr+nebulaMask*60*bright,0,255);
                    sg=(uint8_t)clampd(sg+nebulaMask*15*bright,0,255);
                    sb=(uint8_t)clampd(sb+nebulaMask*80*bright,0,255);
                }
                // Stars (always visible, brighter at night)
                {
                    double sx_=(double)x*0.1, sy_=(double)y*0.1;
                    double sn=h8((int)(sx_)&255,(int)(sy_)&255)/255.0;
                    if(sn>0.97){double starBright=(sn-0.97)*33.0;
                        double twinkle=sin(gTime*2.0+sx_*0.7+sy_*0.3)*0.3+0.7;
                        starBright*=twinkle*clampd(1.0-t*0.5,0.3,1.0);
                        sr=(uint8_t)clampd(sr+starBright*255,0,255);
                        sg=(uint8_t)clampd(sg+starBright*240,0,255);
                        sb=(uint8_t)clampd(sb+starBright*200,0,255);
                    }
                }
                // Planet 1 (large, left side)
                {
                    double plX_=SW*0.2, plY_=cT*0.3, plR=35.0;
                    double pdx_=x-plX_, pdy_=y-plY_;
                    double pd_=sqrt(pdx_*pdx_+pdy_*pdy_);
                    if(pd_<plR){double n_=pd_/plR;
                        double lit=1.0-n_*n_*0.6; double rim=(pd_>plR*0.85)?(1.0-(pd_-plR*0.85)/(plR*0.15)):0;
                        uint8_t pr=(uint8_t)clampd(30*lit+rim*80,0,255);
                        uint8_t pg=(uint8_t)clampd(20*lit+rim*40,0,255);
                        uint8_t pb=(uint8_t)clampd(60*lit+rim*100,0,255);
                        sr=pr;sg=pg;sb=pb;
                    }
                }
                // Planet 2 (small, right side, cyan)
                {
                    double plX2=SW*0.82, plY2=cT*0.25, plR2=20.0;
                    double pdx2=x-plX2, pdy2=y-plY2;
                    double pd2=sqrt(pdx2*pdx2+pdy2*pdy2);
                    if(pd2<plR2){double n2_=pd2/plR2;
                        double lit2=1.0-n2_*n2_*0.5; double rim2=(pd2>plR2*0.8)?(1.0-(pd2-plR2*0.8)/(plR2*0.2)):0;
                        sr=(uint8_t)clampd(sr+lit2*20+rim2*60,0,255);
                        sg=(uint8_t)clampd(sg+lit2*50+rim2*120,0,255);
                        sb=(uint8_t)clampd(sb+lit2*70+rim2*160,0,255);
                    }
                }
                // Horizon glow — neon purple line where sky meets ground
                {
                    double hzDist=fabs(t-0.85);
                    if(hzDist<0.12){double hz=1.0-hzDist/0.12;hz*=hz*hz;
                        sr=(uint8_t)clampd(sr+hz*80,0,255);
                        sg=(uint8_t)clampd(sg+hz*10,0,255);
                        sb=(uint8_t)clampd(sb+hz*120,0,255);
                    }
                }
                // Cyan sun / digital star
                {
                    double sunX=SW*0.5+sin(gTime*0.01)*SW*0.3;
                    double sunY=cT*0.4;
                    double sdx_=x-sunX, sdy_=y-sunY;
                    double sd_=sqrt(sdx_*sdx_+sdy_*sdy_);
                    // Wide glow
                    if(sd_<40){double g_=1-sd_/40;g_*=g_;sr=(uint8_t)clampd(sr+g_*30,0,255);sg=(uint8_t)clampd(sg+g_*80,0,255);sb=(uint8_t)clampd(sb+g_*120,0,255);}
                    // Core
                    if(sd_<8){double sf=1-sd_/8;sf*=sf;
                        sr=(uint8_t)clampd(lerp(sr,200,sf),0,255);
                        sg=(uint8_t)clampd(lerp(sg,230,sf),0,255);
                        sb=(uint8_t)clampd(lerp(sb,255,sf),0,255);}
                    // Lens flare
                    if(sd_>8&&sd_<50&&sd_<30){
                        double angle_=atan2(sdy_,sdx_);
                        double streak=fabs(cos(angle_))*0.4+fabs(sin(angle_))*0.3;
                        double fb_=(1-sd_/30)*streak*0.2;
                        sg=(uint8_t)clampd(sg+fb_*200,0,255);sb=(uint8_t)clampd(sb+fb_*255,0,255);
                    }
                }
                fb[y*SW+x]=rgb(sr,sg,sb);
            }
            if(bright<0.7){
                float nightness=(float)(1.0-bright/0.7);
                for(int si=0;si<numStars;si++){
                    int stx=(int)starList[si].x,sty=(int)(starList[si].y*SH/180);
                    if(sty<0||sty>=cT)continue;
                    float tw=sin(starList[si].twinklePhase+gTime*1.5)*0.3f+0.7f;
                    float alpha=starList[si].brightness*tw*nightness;
                    if(stx==x){
                        uint8_t sa=(uint8_t)(alpha*255);
                        fb[sty*SW+x]=blendPixel(fb[sty*SW+x],rgb(255,255,220,sa));
                    }
                }
            }
        }

        // Walls
        if(hit){
            uint32_t*tex=tile==T_Furn?textures[2]:tile==T_Door?textures[5]:textures[0];
            for(int y=cT;y<=cB;y++){
                int texY=(int)(((double)(y-dT)/lh)*TEX)&(TEX-1);
                uint32_t tc=tex[texY*TEX+texX];
                uint8_t r=tc&0xFF,g=(tc>>8)&0xFF,b=(tc>>16)&0xFF;
                double sm=side?0.72:1.0, ht=(double)(y-dT)/lh, hm=0.75+0.25*(1-ht), ao=1;
                if(ht>0.85)ao=1-(ht-0.85)*3;
                // Specular highlight (view-dependent shine)
                double specAngle=fabs(sin(rayA-pA*2.0+wallX*0.5));
                double spec=pow(specAngle,16.0)*0.25*bright;
                if(tile==T_Door&&doorOpen>0.05){double a=1-doorOpen;if(a<0.05)a=0.05;r=(uint8_t)clampd((r*sm*hm*ao*bright*a)+spec*180,0,255);g=(uint8_t)clampd((g*sm*hm*ao*bright*a)+spec*160,0,255);b=(uint8_t)clampd((b*sm*hm*ao*bright*a)+spec*120,0,255);}
                else{double lm=sm*hm*ao*bright;r=(uint8_t)clampd(r*lm+spec*180,0,255);g=(uint8_t)clampd(g*lm+spec*160,0,255);b=(uint8_t)clampd(b*lm+spec*120,0,255);}
                fb[y*SW+x]=fogC(rgb(r,g,b),pd);
            }
        }

        // Floor
        for(int y=cB+1;y<SH;y++){
            double rowD=(double)SH/(2.0*(y-SH/2.0+bob));
            double fx_=camX+rowD*rdx,fy_=camY+rowD*rdy;
            int tx=((int)(fx_*TEX))&(TEX-1),ty=((int)(fy_*TEX))&(TEX-1);
            uint32_t fc; uint8_t fr,fg,fb_;
            if(isInsideHouse(fx_,fy_)){fc=textures[1][ty*TEX+tx];fr=fc&0xFF;fg=(fc>>8)&0xFF;fb_=(fc>>16)&0xFF;double fl=clampd(0.65-rowD/30.0,0.25,0.65)*bright;fr=(uint8_t)clampd(fr*fl,0,255);fg=(uint8_t)clampd(fg*fl,0,255);fb_=(uint8_t)clampd(fb_*fl,0,255);}
            else{fc=textures[3][ty*TEX+tx];fr=fc&0xFF;fg=(fc>>8)&0xFF;fb_=(fc>>16)&0xFF;double fl=clampd(1-rowD/25.0,0.2,1)*bright;fr=(uint8_t)clampd(fr*fl,0,255);fg=(uint8_t)clampd(fg*fl,0,255);fb_=(uint8_t)clampd(fb_*fl,0,255);}
            fb[y*SW+x]=fogC(rgb(fr,fg,fb_),rowD);
        }
        zbuf[x]=pd;
    }

    // ── Oasis water + glow ──
    for(int oi=0;oi<numOases;oi++){
        Oasis&o=oasisList[oi]; double odx=o.x-camX,ody=o.y-camY;
        double dirX=cos(pA),dirY=sin(pA),plX=-sin(pA)*tanHalf,plY=cos(pA)*tanHalf;
        double invDet=1/(plX*dirY-dirX*plY);
        double tX_=invDet*(dirY*odx-dirX*ody),tY_=invDet*(-plY*odx+plX*ody);
        if(tY_<=0.3)continue;
        int scrX=(int)(SW/2.0*(1+tX_/tY_)); double dist=sqrt(odx*odx+ody*ody);
        int poolR=(int)(30.0/tY_); if(poolR<3)continue;
        for(int sy=-poolR/3;sy<=poolR/3;sy++){
            int screenY=SH/2-bob+(int)(poolR*0.35)+sy; if(screenY<0||screenY>=SH)continue;
            for(int sx=-poolR;sx<=poolR;sx++){
                int screenX=scrX+sx; if(screenX<0||screenX>=SW)continue;
                double ex=(double)sx/poolR,ey=(double)sy/(poolR/3.0),ed=ex*ex+ey*ey;
                if(ed>1)continue;
                double rowD=(double)SH/(2.0*(screenY-SH/2.0+bob));
                if(rowD>dist*0.8)continue; if(tY_>=zbuf[screenX])continue;
                double r1=sin(ed*8-gTime*4)*0.12,r2=sin(ed*5+gTime*3)*0.08,rp=1+r1+r2;
                double ef=1-smoothstep(ed);
                uint8_t wr=(uint8_t)clampd((30+20*rp)*ef*bright,0,255);
                uint8_t wg=(uint8_t)clampd((100+50*rp)*ef*bright,0,255);
                uint8_t wb=(uint8_t)clampd((180+40*rp)*ef*bright,0,255);
                double sp=sin(ed*12-gTime*6+pA);
                if(sp>0.85&&ed<0.3){double sf=(sp-0.85)/0.15;wr=(uint8_t)clampd(wr+100*sf,0,255);wg=(uint8_t)clampd(wg+90*sf,0,255);wb=(uint8_t)clampd(wb+60*sf,0,255);}
                // Glow effect (bright rim)
                if(ed>0.6&&ed<0.95){double glow=(ed-0.6)/0.35;glow*=glow;wr=(uint8_t)clampd(wr+40*glow*bright,0,255);wg=(uint8_t)clampd(wg+60*glow*bright,0,255);wb=(uint8_t)clampd(wb+30*glow*bright,0,255);}
                uint32_t ex_=fb[screenY*SW+screenX];uint8_t er=ex_&0xFF,eg=(ex_>>8)&0xFF,eb=(ex_>>16)&0xFF;
                double bl=ef*0.85;
                fb[screenY*SW+screenX]=fogC(rgb((uint8_t)(er+(wr-er)*bl),(uint8_t)(eg+(wg-eg)*bl),(uint8_t)(eb+(wb-eb)*bl)),dist);
            }
        }
    }

    // ── Collectible items (floor decals) ──
    for(int ii=0;ii<numItems;ii++){
        if(!itemList[ii].active)continue;
        double ix=itemList[ii].x-camX, iy=itemList[ii].y-camY;
        double dirX=cos(pA),dirY=sin(pA),plX=-sin(pA)*tanHalf,plY=cos(pA)*tanHalf;
        double invDet=1/(plX*dirY-dirX*plY);
        double tX_=invDet*(dirY*ix-dirX*iy),tY_=invDet*(-plY*ix+plX*iy);
        if(tY_<=0.3)continue;
        int scrX=(int)(SW/2.0*(1+tX_/tY_));
        double dist=sqrt(ix*ix+iy*iy);
        int sz=maxI(1,(int)(12.0/tY_));
        // Item colors by type: 0=water(blue), 1=compass(yellow), 2=flower(pink), 3=gold
        uint32_t icols[]={rgb(60,140,220),rgb(220,200,40),rgb(220,80,160),rgb(255,200,40)};
        uint32_t iglow[]={rgb(30,80,180),rgb(180,160,20),rgb(180,40,120),rgb(200,160,20)};
        uint32_t col=icols[itemList[ii].type];
        uint32_t glow=iglow[itemList[ii].type];
        // Pulsing glow
        double pulse=sin(gTime*3.0+ii*1.5)*0.3+0.7;
        for(int dy_=-sz;dy_<=sz;dy_++){
            int screenY=SH/2-bob+(int)(sz*0.3)+dy_;
            if(screenY<0||screenY>=SH)continue;
            for(int dx_=-sz;dx_<=sz;dx_++){
                int screenX=scrX+dx_;
                if(screenX<0||screenX>=SW)continue;
                double d=sqrt((double)(dx_*dx_+dy_*dy_));
                if(d>sz)continue;
                if(tY_>=zbuf[screenX])continue;
                float alpha=(float)(pulse*(1.0-d/sz));
                uint8_t a=(uint8_t)(alpha*200);
                uint32_t c=d<sz*0.5?col:glow;
                fb[screenY*SW+screenX]=blendPixel(fb[screenY*SW+screenX],
                    rgb((uint8_t)((c&0xFF)*pulse),((c>>8)&0xFF)*pulse,((c>>16)&0xFF)*pulse,a));
            }
        }
    }

    // ── Player sprite (third person only, with walk animation) ──
    if(thirdPerson){
        double pdx_=pX-camX, pdy_=pY-camY;
        double dirX=cos(pA),dirY=sin(pA),plX=-sin(pA)*tanHalf,plY=cos(pA)*tanHalf;
        double invDet=1/(plX*dirY-dirX*plY);
        double tX_=invDet*(dirY*pdx_-dirX*pdy_),tY_=invDet*(-plY*pdx_+plX*pdy_);
        if(tY_>0.5){
            int scrX_=(int)(SW/2.0*(1+tX_/tY_));
            int spH_=maxI(1,(int)(SH/tY_*0.7));
            int spW_=spH_/3;
            double dist3d=sqrt(pdx_*pdx_+pdy_*pdy_);
            // Walk animation
            double speed=sqrt(iMX*iMX+iMY*iMY);
            double walkAmt=speed>0.1 ? sin(bobPhase*10.0)*1.0 : 0.0;
            double legSwing=walkAmt*spH_*0.08;
            double armSwing=walkAmt*spH_*0.06;
            double torsoBob=speed>0.1 ? fabs(sin(bobPhase*10.0))*spH_*0.012 : 0.0;
            uint32_t headCol=rgb(175,145,110);
            uint32_t bodyCol=rgb(195,170,130);
            uint32_t shirtCol=rgb(170,140,100);
            uint32_t legCol=rgb(160,140,100);
            uint32_t bootCol=rgb(120,95,60);
            uint32_t armCol=rgb(185,158,118);
            uint32_t hatCol=rgb(150,120,80);
            for(int dy_=-spH_/2;dy_<=spH_/2;dy_++){
                int screenY_=SH/2-bob+dy_+(int)torsoBob;
                if(screenY_<0||screenY_>=SH)continue;
                double normY=(double)dy_/(spH_/2.0);
                // Left leg
                if(normY>0.15 && normY<0.95) {
                    int legW=spW_/5, legCX=scrX_ - spW_/5 + (int)legSwing;
                    for(int dx_=-legW;dx_<=legW;dx_++) {
                        int sx_=legCX+dx_+shkX;
                        if(sx_<0||sx_>=SW||tY_>=zbuf[sx_])continue;
                        uint32_t col=normY>0.8?bootCol:legCol;
                        if(dx_<0) col=fogC(rgb((uint8_t)((col&0xFF)*0.82),((col>>8)&0xFF)*0.82,((col>>16)&0xFF)*0.82),dist3d);
                        else col=fogC(col,dist3d);
                        fb[screenY_*SW+sx_]=col;
                    }
                }
                // Right leg
                if(normY>0.15 && normY<0.95) {
                    int legW=spW_/5, legCX=scrX_ + spW_/5 - (int)legSwing;
                    for(int dx_=-legW;dx_<=legW;dx_++) {
                        int sx_=legCX+dx_+shkX;
                        if(sx_<0||sx_>=SW||tY_>=zbuf[sx_])continue;
                        uint32_t col=normY>0.8?bootCol:legCol;
                        if(dx_<0) col=fogC(rgb((uint8_t)((col&0xFF)*0.82),((col>>8)&0xFF)*0.82,((col>>16)&0xFF)*0.82),dist3d);
                        else col=fogC(col,dist3d);
                        fb[screenY_*SW+sx_]=col;
                    }
                }
                // Torso
                if(normY>-0.35 && normY<0.25) {
                    int torsoW=spW_/3;
                    for(int dx_=-torsoW;dx_<=torsoW;dx_++) {
                        int sx_=scrX_+dx_+shkX;
                        if(sx_<0||sx_>=SW||tY_>=zbuf[sx_])continue;
                        uint32_t col=normY>0.18?rgb(140,100,50):shirtCol;
                        if(dx_<0) col=fogC(rgb((uint8_t)((col&0xFF)*0.85),((col>>8)&0xFF)*0.85,((col>>16)&0xFF)*0.85),dist3d);
                        else col=fogC(col,dist3d);
                        fb[screenY_*SW+sx_]=col;
                    }
                }
                // Left arm
                if(normY>-0.3 && normY<0.2) {
                    int armW=spW_/6, armCX=scrX_ - spW_/3 - armW - (int)armSwing;
                    int armDY=(int)(fabs(armSwing)*0.3);
                    for(int dx_=-armW;dx_<=armW;dx_++) {
                        int sx_=armCX+dx_+shkX, sy_=screenY_+armDY;
                        if(sx_<0||sx_>=SW||sy_<0||sy_>=SH||tY_>=zbuf[sx_])continue;
                        uint32_t col=normY<-0.2?armCol:shirtCol;
                        fb[sy_*SW+sx_]=fogC(col,dist3d);
                    }
                }
                // Right arm
                if(normY>-0.3 && normY<0.2) {
                    int armW=spW_/6, armCX=scrX_ + spW_/3 + armW + (int)armSwing;
                    int armDY=(int)(fabs(armSwing)*0.3);
                    for(int dx_=-armW;dx_<=armW;dx_++) {
                        int sx_=armCX+dx_+shkX, sy_=screenY_+armDY;
                        if(sx_<0||sx_>=SW||sy_<0||sy_>=SH||tY_>=zbuf[sx_])continue;
                        uint32_t col=normY<-0.2?armCol:shirtCol;
                        fb[sy_*SW+sx_]=fogC(col,dist3d);
                    }
                }
                // Head
                if(normY<-0.45) {
                    int headW=spW_/3;
                    for(int dx_=-headW;dx_<=headW;dx_++) {
                        int sx_=scrX_+dx_+shkX;
                        if(sx_<0||sx_>=SW||tY_>=zbuf[sx_])continue;
                        uint32_t col=headCol;
                        if(normY>-0.65&&normY<-0.55&&abs(dx_)<spW_/10)col=rgb(40,30,20);
                        if(dx_<0) col=fogC(rgb((uint8_t)((col&0xFF)*0.88),((col>>8)&0xFF)*0.88,((col>>16)&0xFF)*0.88),dist3d);
                        else col=fogC(col,dist3d);
                        fb[screenY_*SW+sx_]=col;
                    }
                }
                // Hat brim
                if(normY>-0.78 && normY<-0.68) {
                    int hatW=spW_/2+1;
                    for(int dx_=-hatW;dx_<=hatW;dx_++) {
                        int sx_=scrX_+dx_+shkX;
                        if(sx_<0||sx_>=SW||tY_>=zbuf[sx_])continue;
                        uint32_t col=hatCol;
                        if(abs(dx_)==hatW)col=fogC(rgb((uint8_t)((col&0xFF)*0.7),((col>>8)&0xFF)*0.7,((col>>16)&0xFF)*0.7),dist3d);
                        else col=fogC(col,dist3d);
                        fb[screenY_*SW+sx_]=col;
                    }
                }
                // Hat crown
                if(normY>-0.95 && normY<-0.78) {
                    int crownW=spW_/4;
                    for(int dx_=-crownW;dx_<=crownW;dx_++) {
                        int sx_=scrX_+dx_+shkX;
                        if(sx_<0||sx_>=SW||tY_>=zbuf[sx_])continue;
                        fb[screenY_*SW+sx_]=fogC(hatCol,dist3d);
                    }
                }
            }
            // Shadow on ground
            { int shadowW=spW_/2, shadowY=SH/2-bob+spH_/2+2;
            if(shadowY>=0&&shadowY<SH) for(int dx_=-shadowW;dx_<=shadowW;dx_++){
                int sx_=scrX_+dx_+shkX;
                if(sx_<0||sx_>=SW)continue;
                double edge=1.0-fabs((double)dx_/shadowW);
                fb[shadowY*SW+sx_]=blendPixel(fb[shadowY*SW+sx_],rgb(20,15,10,(uint8_t)(edge*60)));
            } }
        }
    }

    // ── Particles (screen-space) ──
    for(int i=0;i<MAXP;i++){
        Part&p=parts[i]; if(p.life<=0)continue;
        // Project world position to screen (for type 1,2,4 particles)
        double pdx=p.x-camX, pdy=p.y-camY;
        double dirX=cos(pA),dirY=sin(pA),plX=-sin(pA)*tanHalf,plY=cos(pA)*tanHalf;
        double invDet=1/(plX*dirY-dirX*plY);
        double tX_=invDet*(dirY*pdx-dirX*pdy),tY_=invDet*(-plY*pdx+plX*pdy);
        if(p.type==0||p.type==3){
            // Screen-space particles (sand, trail)
            int px_=(int)p.x+shkX, py_=(int)p.y+shkY;
            if(px_>=0&&px_<SW&&py_>=0&&py_<SH){
                float alpha=p.life/p.maxLife;
                uint8_t a=(uint8_t)(alpha*180);
                uint8_t r=(p.col)&0xFF, g=(p.col>>8)&0xFF, b=(p.col>>16)&0xFF;
                fb[py_*SW+px_]=blendPixel(fb[py_*SW+px_],rgb(r,g,b,a));
            }
        }else{
            // World-space particles (death, splash)
            if(tY_<=0.2)continue;
            int scrX=(int)(SW/2.0*(1+tX_/tY_));
            double d=sqrt(pdx*pdx+pdy*pdy);
            int sz=maxI(1,(int)(8.0/tY_));
            float alpha=p.life/p.maxLife;
            uint8_t a=(uint8_t)(alpha*220);
            uint8_t r=(p.col)&0xFF, g=(p.col>>8)&0xFF, b=(p.col>>16)&0xFF;
            for(int dy_=-sz;dy_<=sz;dy_++)for(int dx_=-sz;dx_<=sz;dx_++){
                if(dx_*dx_+dy_*dy_>sz*sz)continue;
                int sx_=scrX+dx_+shkX, sy_=SH/2-bob+dy_+shkY;
                if(sx_>=0&&sx_<SW&&sy_>=0&&sy_<SH)
                    fb[sy_*SW+sx_]=blendPixel(fb[sy_*SW+sx_],rgb(r,g,b,a));
            }
        }
    }

    // ── Heat shimmer post-process ──
    heatTime+=0.03;
    for(int y=SH/2+20;y<SH;y++){
        double dist=(double)(y-SH/2)/(SH/2.0);
        double intensity=dist*dist*3.0;
        if(intensity<0.3)continue;
        for(int x=0;x<SW;x++){
            double shimmer=sin(y*0.08+heatTime*2.0+x*0.02)*intensity;
            int sx_=(int)(x+shimmer);
            if(sx_>=0&&sx_<SW&&sx_!=x){
                uint32_t c=fb[y*SW+sx_];
                fb[y*SW+x]=c;
            }
        }
    }

    // ── Sandstorm overlay ──
    if(sandstormActive&&sandstormIntensity>0.05){
        for(int y=0;y<SH;y+=2){
            for(int x=0;x<SW;x+=2){
                double noise=(double)(h8(x+(int)(gTime*50),y+(int)(gTime*30)))/255.0;
                double edgeFade=1.0-(double)y/SH*0.3;
                double a=noise*sandstormIntensity*0.4*edgeFade;
                uint8_t alpha=(uint8_t)(a*255);
                if(alpha>5){
                    uint32_t bg=fb[y*SW+x];
                    fb[y*SW+x]=blendPixel(bg,rgb(210,180,130,alpha));
                    fb[y*SW+x+1]=blendPixel(fb[y*SW+x+1],rgb(210,180,130,alpha));
                    fb[(y+1)*SW+x]=blendPixel(fb[(y+1)*SW+x],rgb(210,180,130,alpha));
                    fb[(y+1)*SW+x+1]=blendPixel(fb[(y+1)*SW+x+1],rgb(210,180,130,alpha));
                }
            }
        }
        // Horizontal wind streaks
        for(int i=0;i<(int)(sandstormIntensity*20);i++){
            int sy=(int)(gTime*80+i*37)%SH;
            int sx=(int)(gTime*200+i*73)%SW;
            int len=10+(int)(sandstormIntensity*30);
            for(int lx=0;lx<len&&sx+lx<SW;lx++){
                double fade=1.0-(double)lx/len;
                uint8_t a=(uint8_t)(fade*sandstormIntensity*120);
                fb[sy*SW+sx+lx]=blendPixel(fb[sy*SW+sx+lx],rgb(200,175,125,a));
            }
        }
    }

    // ── Minimap with fog of war ──
    int mmS=130,mmT=16,mmX0=SW-mmS-8,mmY0=8;
    double mmSc=(double)mmS/mmT;
    for(int y=0;y<mmS;y++)for(int xx=0;xx<mmS;xx++)fb[(mmY0+y)*SW+mmX0+xx]=rgb(20,16,10,180);
    for(int y=0;y<mmS;y++)for(int xx=0;xx<mmS;xx++){
        int wx=(int)(pX-mmT/2.0+xx/mmSc),wy=(int)(pY-mmT/2.0+y/mmSc);
        if(wx<0||wx>=MAP||wy<0||wy>=MAP)continue;
        // Fog of war: only show explored tiles
        if(!fogMap[wx][wy]){
            // Dim unexplored area
            double edgeDist=fmax(abs(wx-(int)pX),abs(wy-(int)pY));
            if(edgeDist>4)continue; // fully hidden
        }
        uint32_t c;
        switch(wmap[wx][wy]){case T_Empty:c=rgb(190,165,125);break;case T_Wall:c=rgb(185,165,130);break;case T_Floor:c=rgb(150,138,118);break;case T_Door:c=doorOpen>0.5?rgb(160,130,80):rgb(100,72,35);break;case T_Water:c=rgb(40,110,200);break;case T_Furn:c=rgb(110,80,40);break;default:c=rgb(180,160,120);}
        // Fog dimming for recently revealed
        if(fogMap[wx][wy]&&fmax(abs(wx-(int)pX),abs(wy-(int)pY))>4){
            uint8_t r=c&0xFF,g=(c>>8)&0xFF,b=(c>>16)&0xFF;
            c=rgb((uint8_t)(r*0.4),(uint8_t)(g*0.4),(uint8_t)(b*0.4));
        }
        fb[(mmY0+y)*SW+mmX0+xx]=c;
    }
    int pdx=mmS/2,pdy=mmS/2;
    for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)if(dx*dx+dy*dy<=4)fb[(mmY0+pdy+dy)*SW+mmX0+pdx+dx]=rgb(255,60,60);
    for(int i=3;i<12;i++){int lx=pdx+(int)(cos(pA)*i),ly=pdy+(int)(sin(pA)*i);if(lx>=0&&lx<mmS&&ly>=0&&ly<mmS)fb[(mmY0+ly)*SW+mmX0+lx]=rgb(255,220,60);}
    for(int i=0;i<mmS;i++){fb[mmY0*SW+mmX0+i]=rgb(90,72,45);fb[(mmY0+mmS-1)*SW+mmX0+i]=rgb(90,72,45);fb[(mmY0+i)*SW+mmX0]=rgb(90,72,45);fb[(mmY0+i)*SW+mmX0+mmS-1]=rgb(90,72,45);}

    // ── Post-process: vignette + color grade ──
    {
        double cx=SW/2.0, cy=SH/2.0;
        double maxDist=sqrt(cx*cx+cy*cy);
        // Desert color grade: warm shift
        double warmR=1.08, warmG=1.02, warmB=0.92;
        for(int py=0;py<SH;py+=2){
            for(int px=0;px<SW;px+=2){
                int idx=py*SW+px;
                uint32_t c=fb[idx];
                uint8_t r=c&0xFF, g=(c>>8)&0xFF, b=(c>>16)&0xFF;
                // Vignette: darken edges
                double dx=(px-cx)/cx, dy=(py-cy)/cy;
                double vignette=1.0-(dx*dx+dy*dy)*0.15;
                vignette=clampd(vignette,0.55,1.0);
                // Color grade: warm desert tone
                r=(uint8_t)clampd(r*warmR*vignette,0,255);
                g=(uint8_t)clampd(g*warmG*vignette,0,255);
                b=(uint8_t)clampd(b*warmB*vignette,0,255);
                fb[idx]=rgb(r,g,b);
                // Fill 2x2 block for performance
                if(px+1<SW) fb[idx+1]=rgb(r,g,b);
                if(py+1<SH) fb[(py+1)*SW+px]=rgb(r,g,b);
                if(px+1<SW&&py+1<SH) fb[(py+1)*SW+px+1]=rgb(r,g,b);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  GAME UPDATE
// ═══════════════════════════════════════════════════════════════
static void updateGame(double dt) {
    gTime+=dt; soundSignal=0;
    double moveSpd=(sprinting?5.5:3.8)*dt, rotSpd=2.8*dt;
    pA+=iTurn*rotSpd;
    double dx=cos(pA)*iMY*moveSpd+cos(pA+M_PI/2)*iMX*moveSpd;
    double dy=sin(pA)*iMY*moveSpd+sin(pA+M_PI/2)*iMX*moveSpd;

    double r=0.25,nx=pX+dx,ny=pY+dy;
    if(canWalk(nx+r,pY)&&canWalk(nx-r,pY)&&canWalk(nx,pY+r)&&canWalk(nx,pY-r))pX=nx;
    if(canWalk(pX+r,ny)&&canWalk(pX-r,ny)&&canWalk(pX,ny+r)&&canWalk(pX,ny-r))pY=ny;

    double speed=sqrt(dx*dx+dy*dy)/dt;
    if(speed>0.5)bobPhase+=dt*speed*0.6;

    static double lastStep=0;
    if(speed>0.5&&gTime-lastStep>0.35){soundSignal|=1;lastStep=gTime;}

    // Sand trail particles
    if(speed>1.0&&rand()%3==0) spawnTrail(pX,pY,dx/speed,dy/speed);

    // Sprint start shake
    int nowSprinting=(sqrt(iMX*iMX+iMY*iMY)>0.85)?1:0;
    if(nowSprinting&&!wasSprinting) shakeAmount=3.0;
    wasSprinting=nowSprinting;
    sprinting=nowSprinting;

    // Door
    int hx=MAP/2-5,hy=MAP/2-5;
    double doorCX=hx+5.0,doorCY=hy+10.5;
    double dtd=sqrt((pX-doorCX)*(pX-doorCX)+(pY-doorCY)*(pY-doorCY));
    doorTarget=dtd<4.0?1.0:0.0;
    double prevDoor=doorOpen;
    double doorSpd=3.0*dt;
    if(doorOpen<doorTarget){doorOpen+=doorSpd;if(doorOpen>doorTarget)doorOpen=doorTarget;}
    else{doorOpen-=doorSpd;if(doorOpen<doorTarget)doorOpen=doorTarget;}
    if(prevDoor<0.1&&doorOpen>=0.1)soundSignal|=5;
    if(prevDoor>0.1&&doorOpen<=0.05)soundSignal|=5;

    // Water
    uint8_t under=wmap[(int)pX][(int)pY];
    if(under==T_Water&&thirst<100.0){
        thirst=100;score++;soundSignal|=2;
        spawnSplash(pX,pY); // splash VFX!
    }

    // Thirst
    thirst-=2.5*dt; if(thirst<0)thirst=0;
    if(thirst>0&&thirst<20&&fmod(gTime,1.5)<dt)soundSignal|=4; // warning
    if(thirst>0&&thirst<20&&fmod(gTime,0.8)<dt)soundSignal|=8; // heartbeat

    // Collectible items
    for(int ii=0;ii<numItems;ii++){
        if(!itemList[ii].active)continue;
        double dx_=pX-itemList[ii].x, dy_=pY-itemList[ii].y;
        if(dx_*dx_+dy_*dy_<1.5){
            itemList[ii].active=0;
            score+=5+itemList[ii].type*2; // gold=11, flower=9, compass=7, water=5
            comboCount++;
            comboTimer=3.0;
            if(comboCount>bestCombo)bestCombo=comboCount;
            // Type-specific effects
            switch(itemList[ii].type){
                case 0: thirst=clampd(thirst+25,0,100); soundSignal|=2; break; // water bottle
                case 3: shakeAmount=2.0; soundSignal|=6; break; // gold sparkle
                default: soundSignal|=6; break;
            }
            // Burst particles
            for(int k=0;k<8;k++){
                int idx=nextParticle(); if(idx<0)break;
                double a_=(double)(rand()%628)/100.0;
                parts[idx]={0};
                parts[idx].x=(float)itemList[ii].x;parts[idx].y=(float)itemList[ii].y;
                parts[idx].vx=(float)(cos(a_)*1.5);parts[idx].vy=(float)(sin(a_)*1.5);
                parts[idx].life=20+(float)(rand()%15);parts[idx].maxLife=parts[idx].life;
                parts[idx].col=rgb(255,220,60);parts[idx].type=1;
            }
        }
    }
    // Combo decay
    if(comboTimer>0){comboTimer-=dt;if(comboTimer<=0)comboCount=0;}

    // Sandstorm
    sandstormTimer-=dt;
    if(sandstormTimer<=0&&!sandstormActive){sandstormActive=1;sandstormIntensity=0;soundSignal|=7;}
    if(sandstormActive){
        sandstormIntensity+=dt*0.15; // ramp up
        if(sandstormIntensity>1.0)sandstormIntensity=1.0;
        thirst-=1.5*dt*sandstormIntensity; // drain extra water
        if(sandstormTimer<-20.0){sandstormActive=0;sandstormIntensity=0;sandstormTimer=30.0+(double)(rand()%20);}
    }
    if(!sandstormActive&&sandstormTimer<-20.0)sandstormTimer=30.0+(double)(rand()%20);

    // Fog of war — reveal tiles around player
    int revealR=6;
    for(int fy=(int)pY-revealR;fy<=(int)pY+revealR;fy++)
        for(int fx=(int)pX-revealR;fx<=(int)pX+revealR;fx++)
            if(fx>=0&&fx<MAP&&fy>=0&&fy<MAP)fogMap[fx][fy]=1;

    // Death
    if(thirst<=0){
        soundSignal|=3;
        spawnDeathBurst(pX,pY); // death burst VFX!
        shakeAmount=8.0; // big shake!
        pX=hx+5.5;pY=hy+8.0;pA=-M_PI/2;thirst=100;
    }

    // Screen shake decay
    if(shakeAmount>0.01){
        shakeX=(double)((rand()%100-50)/10.0)*shakeAmount/8.0;
        shakeY=(double)((rand()%100-50)/10.0)*shakeAmount/8.0;
        shakeAmount*=0.88;
    }else{shakeX=0;shakeY=0;shakeAmount=0;}

    // Particle update
    for(int i=0;i<MAXP;i++){
        Part&p=parts[i]; if(p.life<=0)continue;
        p.x+=p.vx; p.y+=p.vy; p.life-=1.0f;
        if(p.type==1){p.vy+=0.03f;} // gravity on death particles
        if(p.type==2){p.vy+=0.05f;} // gravity on splash
    }

    // Ambient sand particles (screen-space)
    static double lastAmbient=0;
    if(gTime-lastAmbient>0.08){
        lastAmbient=gTime;
        int idx=nextParticle();
        if(idx>=0){
            parts[idx]={0};
            parts[idx].x=-5; parts[idx].y=(float)(rand()%SH);
            parts[idx].vx=0.3f+(rand()%30)/100.0f;
            parts[idx].vy=(float)((rand()%20-10)/100.0);
            parts[idx].life=200+(float)(rand()%200);
            parts[idx].maxLife=parts[idx].life;
            parts[idx].col=rgb(210+rand()%25,195+rand()%20,155+rand()%20);
            parts[idx].type=0;
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
    EMSCRIPTEN_KEEPALIVE int     get_sound()    { int s=soundSignal; soundSignal=0; return s; }
    EMSCRIPTEN_KEEPALIVE int     get_sprint()   { return sprinting; }
    EMSCRIPTEN_KEEPALIVE int     get_third_person() { return thirdPerson; }
    EMSCRIPTEN_KEEPALIVE void    toggle_third_person() { thirdPerson = 1 - thirdPerson; }
    EMSCRIPTEN_KEEPALIVE int     get_combo()    { return comboCount; }
    EMSCRIPTEN_KEEPALIVE int     get_sandstorm(){ return sandstormActive; }
    EMSCRIPTEN_KEEPALIVE void    set_input(float mx, float my, float t) { iMX=mx; iMY=my; iTurn=t; }
}
int main(){return 0;}
