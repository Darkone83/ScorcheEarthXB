/*---------------------------------------------------------------------------
    ScorchedXB - terrain.cpp
    Pixel-perfect port of scorch.js Bitmap + terrain generation.

    Generation algorithm (generateTerrain in scorch.js):
        1. Fill bottom 3/8 with sky pixels  (bgMode)
        2. Fill top    5/8 with groundColor
        3. Carve 20 random ellipses          (bgMode -- creates hills)
        4. Add large central mound           (groundColor -- fills valleys)
        5. drop(0, WIDTH)                    (gravity settle)

    After drop() the ground sits at the bottom, sky is above.
    Tanks are placed on the first ground surface found scanning top-down.

    RNG:  LCG matching scorch.js Random class:
              seed = (1664525 * seed + 1013904223) & 0xFFFFFFFF
              int(n) = floor(seed / 0x100000000 * n)

    Colours: 0xFFRRGGBB (matching rgb() which already ORs 0xFF000000).

    gradientStripColor uses INTEGER division (not float) matching scorch.js.

    Rendering: XGSwizzleRect converts linear terrain pixels to Morton-curve
    swizzle order in a power-of-2 D3DFMT_X8R8G8B8 texture sized to cover
    the actual display resolution (480p=1024x512, 720p=2048x1024).
    DrawPrimitiveUP with clamped UVs blits it fullscreen.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <math.h>
#include "terrain.h"
#include "render.h"    /* g_pd3dDevice, g_dwDisplayW, g_dwDisplayH */

/* =========================================================================
   State
========================================================================= */

static DWORD* s_pPixels = NULL;  /* game logic: collision, explosions  */
static short* s_pStars = NULL;  /* star Y positions (BG_STARS)        */
static int* s_pHeights = NULL;  /* terrain surface height per column  */

static int    s_nW = 0;
static int    s_nH = 0;
static DWORD  s_dwGround = 0;
static int    s_nBgKind = TERR_BG_STARS;
static DWORD  s_dwPlain = 0;
static DWORD  s_dwGradA = 0;
static DWORD  s_dwGradB = 0;

static int    s_nDirtyX0 = 0;
static int    s_nDirtyX1 = 0;
static int    s_bDirty = 0;

static DWORD  s_nRandSeed = 0;

/* Ground polygon vertex buffer -- 2 verts per column, max 1280 columns  */
#define GVMAX  ( 1280 * 2 )
typedef struct { float x, y, z, rhw; DWORD c; } GV;
static GV s_gv[GVMAX];

/* =========================================================================
   RNG -- direct port of scorch.js Random class (LCG)
========================================================================= */

static DWORD RandNext(void)
{
    s_nRandSeed = (DWORD)(1664525UL * (DWORD)s_nRandSeed + 1013904223UL);
    return s_nRandSeed;
}

/* Returns integer in [0, n) */
static int RandInt(int n)
{
    DWORD raw;
    if (n <= 1) return 0;
    raw = RandNext();
    /* floor( (raw / 2^32) * n ) -- use 64-bit multiply on x86 */
    return (int)((unsigned __int64)raw * (unsigned __int64)n >> 32);
}

/* =========================================================================
   Colour helpers (matching scorch.js)
========================================================================= */

/* Pack ARGB -- scorch.js rgb() already ORs 0xFF000000 */
static DWORD RGB8(int r, int g, int b)
{
    return 0xFF000000u
        | ((DWORD)(r < 0 ? 0 : r > 255 ? 255 : r) << 16)
        | ((DWORD)(g < 0 ? 0 : g > 255 ? 255 : g) << 8)
        | ((DWORD)(b < 0 ? 0 : b > 255 ? 255 : b));
}

/* gradientStripColor: integer division matching scorch.js exactly */
static DWORD GradientStrip(DWORD a, DWORD b, int strip, int steps)
{
    int ar = (int)((a >> 16) & 0xFF);
    int ag = (int)((a >> 8) & 0xFF);
    int ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF);
    int bg = (int)((b >> 8) & 0xFF);
    int bb = (int)(b & 0xFF);
    return RGB8(ar + ((br - ar) / steps) * strip,
        ag + ((bg - ag) / steps) * strip,
        ab + ((bb - ab) / steps) * strip);
}

/* =========================================================================
   Background computation  (matches Bitmap.backgroundColor)
========================================================================= */

static DWORD BackgroundAt(int x, int y)
{
    int strip, steps;

    switch (s_nBgKind)
    {
    case TERR_BG_STARS:
        if (x >= 0 && x < s_nW && s_pStars[x] == (short)y)
        {
            int i = 255 - (255 * y / s_nH);
            return RGB8(i, i, i);
        }
        return 0xFF000000u;

    case TERR_BG_GRADIENT:
        steps = 127;
        strip = y / (s_nH / steps);
        if (strip < 0) strip = 0;
        if (strip >= steps) strip = steps - 1;
        return GradientStrip(s_dwGradA, s_dwGradB, strip, steps);

    default: /* TERR_BG_PLAIN */
        return s_dwPlain;
    }
}

/* =========================================================================
   Pixel buffer ops
========================================================================= */

static int InBounds(int x, int y)
{
    return x >= 0 && y >= 0 && x < s_nW && y < s_nH;
}

static DWORD GetPixelRaw(int x, int y)
{
    return s_pPixels[y * s_nW + x];
}

static void PutPixel(int x, int y, DWORD c)
{
    if (!InBounds(x, y)) return;
    s_pPixels[y * s_nW + x] = c;
}

/* Write background (sky) colour at (x,y) */
static void PutBg(int x, int y)
{
    if (!InBounds(x, y)) return;
    s_pPixels[y * s_nW + x] = BackgroundAt(x, y);
}

/* Write ground colour at (x,y) */
static void PutGround(int x, int y)
{
    if (!InBounds(x, y)) return;
    s_pPixels[y * s_nW + x] = s_dwGround;
}

/* =========================================================================
   Drawing primitives (matching Bitmap methods in scorch.js)
========================================================================= */

/* fillRect: bBg = 1 → background, 0 → ground */
static void FillRect(int x, int y, int w, int h, int bBg)
{
    int px, py;
    for (py = y; py < y + h; py++)
        for (px = x; px < x + w; px++)
            if (bBg) PutBg(px, py); else PutGround(px, py);
}

/* drawLine (Bresenham) */
static void DrawLine(int x1, int y1, int x2, int y2, int bBg)
{
    int dx = (x2 > x1 ? x2 - x1 : x1 - x2);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -(y2 > y1 ? y2 - y1 : y1 - y2);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int e2;

    for (;;)
    {
        if (bBg) PutBg(x1, y1); else PutGround(x1, y1);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

/* fillCircle -- matches Bitmap.fillCircle, used for explosions */
static void FillCircle(int cx, int cy, int r, int bBg)
{
    int y, span, x;
    int rr = r * r;
    for (y = -r; y <= r; y++)
    {
        span = (int)sqrt((float)(rr - y * y));
        for (x = -span; x <= span; x++)
        {
            if (bBg) PutBg(cx + x, cy + y);
            else      PutGround(cx + x, cy + y);
        }
    }
}

/* fillEllipse -- matches Bitmap.fillEllipse, used in generation */
static void FillEllipse(int cx, int cy, int a, int b, int bBg)
{
    float fa = (float)a;
    float fb = (float)b;
    float fb2 = fb * fb;
    int   y, span, x;

    for (y = -b; y <= b; y++)
    {
        float t = 1.0f - ((float)(y * y)) / fb2;
        span = (int)(fa * (float)sqrt(t > 0.0f ? t : 0.0f));
        for (x = -span; x <= span; x++)
        {
            if (bBg) PutBg(cx + x, cy + y);
            else      PutGround(cx + x, cy + y);
        }
    }
}

/* =========================================================================
   Gravity  (drop -- matches game.drop in scorch.js)
========================================================================= */

void Terrain_Drop(int nX0, int nX1)
{
    int x, y, upperBound, lowerBound, thickness;

    nX0 = nX0 < 0 ? 0 : nX0;
    nX1 = nX1 >= s_nW ? s_nW - 1 : nX1;

    for (x = nX0; x <= nX1; x++)
    {
        y = 0;
        while (y < s_nH)
        {
            lowerBound = s_nH;
            upperBound = y;
            thickness = 0;

            for (; y < s_nH; y++)
            {
                DWORD px = GetPixelRaw(x, y);
                if (px == s_dwGround)
                {
                    thickness++;
                }
                else if (px != BackgroundAt(x, y))
                {
                    /* Non-background, non-ground obstacle (e.g. tank) */
                    lowerBound = y++;
                    break;
                }
            }

            if (thickness > 0)
            {
                int newTop = lowerBound - thickness;
                int i;

                /* Draw ground at new settled position */
                for (i = newTop; i < lowerBound; i++)
                    PutGround(x, i);

                /* Clear vacated region above */
                for (i = upperBound; i < newTop; i++)
                    PutBg(x, i);
            }
        }
    }
}

/* =========================================================================
   Background / colour randomisation  (randomBackground in scorch.js)
========================================================================= */

/* 8 colour entries matching scorch.js constant array */
static const unsigned char k_cR[8] = { 0,   0, 254,   0, 254,   0, 254,  34 };
static const unsigned char k_cG[8] = { 0, 150,   0,   0, 254, 254, 254,  23 };
static const unsigned char k_cB[8] = { 0,   0,   0, 254, 254, 254,   0,  65 };

/* Sky start excludes white(4), cyan(5), yellow(6) */
static const int k_skyStart[5] = { 0, 1, 2, 3, 7 };

/* Earth tones -- bright enough to read against black star sky or any gradient */
static const unsigned char k_groundR[8] = { 180, 140, 200, 160, 220, 100, 170, 210 };
static const unsigned char k_groundG[8] = { 120,  90, 130, 110, 160, 130, 100, 170 };
static const unsigned char k_groundB[8] = { 60,  50,  60,  50,  80,  60,  50,  90 };

static void ChooseBackground(void)
{
    int   skyIdx, skyKind;
    int   groundIdx;
    int   sr, sg, sb;

    /* Sky start colour */
    skyIdx = k_skyStart[RandInt(5)];
    sr = k_cR[skyIdx];
    sg = k_cG[skyIdx];
    sb = k_cB[skyIdx] + 100;
    if (sb > 255) sb = 255;

    /* Ground colour: fixed earth tone table -- always visible on TV */
    groundIdx = RandInt(8);
    s_dwGround = RGB8(k_groundR[groundIdx],
        k_groundG[groundIdx],
        k_groundB[groundIdx]);

    skyKind = RandInt(3);

    switch (skyKind)
    {
    case 0:  /* stars */
    {
        int x;
        s_nBgKind = TERR_BG_STARS;
        for (x = 0; x < s_nW; x++)
            s_pStars[x] = (x % 3 == 0) ? (short)RandInt(s_nH) : -1;
        break;
    }
    case 1:  /* gradient */
    {
        int rc2 = RandInt(8);
        s_nBgKind = TERR_BG_GRADIENT;
        s_dwGradA = RGB8(sr, sg, sb);
        s_dwGradB = RGB8(k_cR[rc2], k_cG[rc2], k_cB[rc2]);
        break;
    }
    default: /* plain */
    {
        sr -= 50;  if (sr < 20) sr = 20;
        sg -= 50;  if (sg < 20) sg = 20;
        sb += 30;  if (sb > 255) sb = 255;
        s_nBgKind = TERR_BG_PLAIN;
        s_dwPlain = RGB8(sr, sg, sb);
        break;
    }
    }

    s_dwGround = RGB8(k_groundR[groundIdx],
        k_groundG[groundIdx],
        k_groundB[groundIdx]);
}

/* =========================================================================
   Terrain generation  (generateTerrain in scorch.js)
========================================================================= */

/* =========================================================================
   Terrain generation -- height-map approach replacing scorch.js drop().
   Generates a hilly profile guaranteed to show ground on a TV.
   Base height ~55% from top, smoothed random walk with sine variation.
========================================================================= */

static void Generate(int nTerrainType)
{
    int* heights = (int*)malloc((size_t)s_nW * sizeof(int));
    int   x, y;
    int   h;
    float base, amp, stepScale;
    int   smoothPasses;

    if (!heights) return;

    /* Style parameters */
    switch (nTerrainType)
    {
    case 1: /* Flat */
        base = (float)s_nH * 0.65f;
        amp = (float)s_nH * 0.04f;
        stepScale = 0.3f;
        smoothPasses = 6;
        break;
    case 2: /* Hills */
        base = (float)s_nH * 0.55f;
        amp = (float)s_nH * 0.18f;
        stepScale = 0.6f;
        smoothPasses = 4;
        break;
    case 3: /* Mountains */
        base = (float)s_nH * 0.50f;
        amp = (float)s_nH * 0.38f;
        stepScale = 1.4f;
        smoothPasses = 2;
        break;
    default: /* Random */
        base = (float)s_nH * 0.55f;
        amp = (float)s_nH * 0.22f;
        stepScale = 1.0f;
        smoothPasses = 3;
        break;
    }

    /* Pass 1: random walk */
    {
        float walk = 0.0f;
        heights[0] = (int)base;
        for (x = 1; x < s_nW; x++)
        {
            walk += ((float)RandInt(21) - 10.0f) * 0.5f * stepScale;
            if (walk > amp) walk = amp;
            if (walk < -amp) walk = -amp;
            heights[x] = (int)(base + walk);
        }
    }

    /* Pass 2: smooth */
    {
        int   iter, i;
        int* tmp = (int*)malloc((size_t)s_nW * sizeof(int));
        if (tmp)
        {
            for (iter = 0; iter < smoothPasses; iter++)
            {
                for (i = 0; i < s_nW; i++)
                {
                    int l = heights[i > 0 ? i - 1 : 0];
                    int r = heights[i < s_nW - 1 ? i + 1 : s_nW - 1];
                    tmp[i] = (l + heights[i] + r) / 3;
                }
                memcpy(heights, tmp, (size_t)s_nW * sizeof(int));
            }
            free(tmp);
        }
    }

    /* Pass 3: clamp */
    for (x = 0; x < s_nW; x++)
    {
        int minH = s_nH / 8;
        int maxH = s_nH * 9 / 10;
        if (heights[x] < minH) heights[x] = minH;
        if (heights[x] > maxH) heights[x] = maxH;
    }

    /* Pass 4: fill pixels */
    for (x = 0; x < s_nW; x++)
    {
        h = heights[x];
        for (y = 0; y < h; y++) PutBg(x, y);
        for (y = h; y < s_nH; y++) PutGround(x, y);
    }

    free(heights);
}

/* =========================================================================
   Dirty rect
========================================================================= */

void Terrain_MarkDirty(int nX0, int nX1)
{
    if (!s_bDirty)
    {
        s_nDirtyX0 = nX0;
        s_nDirtyX1 = nX1;
        s_bDirty = 1;
    }
    else
    {
        if (nX0 < s_nDirtyX0) s_nDirtyX0 = nX0;
        if (nX1 > s_nDirtyX1) s_nDirtyX1 = nX1;
    }
}

/* =========================================================================
   Public API
========================================================================= */

/* =========================================================================
   Height scan -- find topmost ground pixel per column for rendering
========================================================================= */

static void ScanHeights(int x0, int x1)
{
    int x, y;
    if (!s_pHeights) return;
    for (x = x0; x <= x1 && x < s_nW; x++)
    {
        s_pHeights[x] = s_nH;  /* default: no ground visible */
        for (y = 0; y < s_nH; y++)
        {
            if (GetPixelRaw(x, y) == s_dwGround)
            {
                s_pHeights[x] = y;
                break;
            }
        }
    }
}

/* =========================================================================
   Public API
========================================================================= */

void Terrain_Init(void)
{
    s_pPixels = NULL;
    s_pStars = NULL;
    s_pHeights = NULL;
    s_bDirty = 0;
}

void Terrain_Shutdown(void)
{
    if (s_pPixels) { free(s_pPixels); s_pPixels = NULL; }
    if (s_pStars) { free(s_pStars); s_pStars = NULL; }
    if (s_pHeights) { free(s_pHeights); s_pHeights = NULL; }
    s_nW = 0;
    s_nH = 0;
}

void Terrain_NewRound(int nTerrainType)
{
    DWORD newW = g_dwDisplayW;
    DWORD newH = g_dwDisplayH;

    if ((int)newW != s_nW || (int)newH != s_nH)
    {
        if (s_pPixels) free(s_pPixels);
        if (s_pStars) free(s_pStars);
        if (s_pHeights) free(s_pHeights);
        s_nW = (int)newW;
        s_nH = (int)newH;
        s_pPixels = (DWORD*)malloc((size_t)s_nW * (size_t)s_nH * sizeof(DWORD));
        s_pStars = (short*)malloc((size_t)s_nW * sizeof(short));
        s_pHeights = (int*)malloc((size_t)s_nW * sizeof(int));
    }

    s_nRandSeed = GetTickCount();
    ChooseBackground();
    Generate(nTerrainType);

    /* Build height profile for ground polygon rendering */
    ScanHeights(0, s_nW - 1);

    s_nDirtyX0 = 0;
    s_nDirtyX1 = s_nW - 1;
    s_bDirty = 0;
}

/* Terrain_Upload: rescan dirty columns after explosions/drop             */
void Terrain_Upload(void)
{
    int x0, x1;
    if (!s_bDirty) return;
    x0 = s_nDirtyX0 < 0 ? 0 : s_nDirtyX0;
    x1 = s_nDirtyX1 >= s_nW ? s_nW - 1 : s_nDirtyX1;
    ScanHeights(x0, x1);
    s_bDirty = 0;
}

/* =========================================================================
   Terrain_Draw -- three GPU layers: sky, ground polygon, done.
   No CPU-to-back-buffer writes.  Works at any resolution.
========================================================================= */

static void DrawSky(void)
{
    GV    v[4];
    float w = (float)s_nW;
    float h = (float)s_nH;
    DWORD cTop = 0xFF000000u;
    DWORD cBot = 0xFF000000u;

    switch (s_nBgKind)
    {
    case TERR_BG_GRADIENT:
        cTop = s_dwGradA;
        cBot = s_dwGradB;
        break;
    case TERR_BG_PLAIN:
        cTop = cBot = s_dwPlain;
        break;
    default: /* STARS -- black background */
        cTop = cBot = 0xFF000000u;
        break;
    }

    v[0].x = 0.f; v[0].y = 0.f; v[0].z = 0.f; v[0].rhw = 1.f; v[0].c = cTop;
    v[1].x = w;   v[1].y = 0.f; v[1].z = 0.f; v[1].rhw = 1.f; v[1].c = cTop;
    v[2].x = 0.f; v[2].y = h;   v[2].z = 0.f; v[2].rhw = 1.f; v[2].c = cBot;
    v[3].x = w;   v[3].y = h;   v[3].z = 0.f; v[3].rhw = 1.f; v[3].c = cBot;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(GV));
}

static void DrawStars(void)
{
    static GV sv[512];
    int   x, n = 0;
    float fSz = 1.0f;

    for (x = 0; x < s_nW && n < 512; x++)
    {
        if (s_pStars[x] >= 0)
        {
            float brt = 1.0f - (float)s_pStars[x] / (float)s_nH;
            int   ib = (int)(brt * 255.f);
            sv[n].x = (float)x;
            sv[n].y = (float)s_pStars[x];
            sv[n].z = 0.f;
            sv[n].rhw = 1.f;
            sv[n].c = 0xFF000000u | ((DWORD)ib << 16) | ((DWORD)ib << 8) | (DWORD)ib;
            n++;
        }
    }
    if (n == 0) return;
    g_pd3dDevice->SetRenderState(D3DRS_POINTSIZE, *(DWORD*)&fSz);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_POINTLIST, (UINT)n, sv, sizeof(GV));
}

static void DrawGround(void)
{
    int x, n = 0;
    if (!s_pHeights) return;

    for (x = 0; x < s_nW && n < GVMAX; x++)
    {
        s_gv[n].x = (float)x; s_gv[n].y = (float)s_pHeights[x]; s_gv[n].z = 0.f; s_gv[n].rhw = 1.f; s_gv[n].c = s_dwGround; n++;
        s_gv[n].x = (float)x; s_gv[n].y = (float)s_nH;          s_gv[n].z = 0.f; s_gv[n].rhw = 1.f; s_gv[n].c = s_dwGround; n++;
    }
    if (n < 4) return;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, (UINT)(n - 2), s_gv, sizeof(GV));
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

void Terrain_Draw(void)
{
    DrawSky();
    if (s_nBgKind == TERR_BG_STARS && s_pStars) DrawStars();
    DrawGround();
}

/* =========================================================================
   Query / write API
========================================================================= */

int Terrain_IsBackground(int x, int y)
{
    if (!InBounds(x, y)) return 0;
    return GetPixelRaw(x, y) == BackgroundAt(x, y);
}

int Terrain_IsGround(int x, int y)
{
    if (!InBounds(x, y)) return 0;
    return GetPixelRaw(x, y) == s_dwGround;
}

DWORD Terrain_GetGroundColor(void) { return s_dwGround; }
int   Terrain_GetBgKind(void) { return s_nBgKind; }

DWORD Terrain_GetPixel(int x, int y)
{
    if (!InBounds(x, y)) return 0;
    return GetPixelRaw(x, y);
}

void Terrain_SetPixelGround(int x, int y)
{
    PutGround(x, y);
    Terrain_MarkDirty(x, x);
}

void Terrain_SetPixelBg(int x, int y)
{
    PutBg(x, y);
    Terrain_MarkDirty(x, x);
}

void Terrain_Explode(int x, int y, int nRadius)
{
    int x0 = x - nRadius - 1;
    int x1 = x + nRadius + 1;

    /* Carve background circle (applyWeaponToBitmap + bitmap.setColor(null)) */
    FillCircle(x, y, nRadius, 1 /* bg */);

    /* Gravity over affected range (+ 1 col margin each side) */
    Terrain_Drop(x0, x1);

    /* Mark dirty */
    Terrain_MarkDirty(x0, x1);
}
/* =========================================================================
   Batch primitives (no drop, no dirty -- caller handles those)
========================================================================= */

void Terrain_CarveCircle(int cx, int cy, int r)
{
    FillCircle(cx, cy, r, 1 /* bg */);
}

void Terrain_CarveLine(int x1, int y1, int x2, int y2)
{
    DrawLine(x1, y1, x2, y2, 1 /* bg */);
}

void Terrain_DrawLineGround(int x1, int y1, int x2, int y2)
{
    DrawLine(x1, y1, x2, y2, 0 /* ground */);
}