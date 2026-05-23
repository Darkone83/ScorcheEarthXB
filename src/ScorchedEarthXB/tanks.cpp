/*---------------------------------------------------------------------------
    ScorchedXB - tanks.cpp
    Cel-shaded 3D tank rendering.

    Coordinate system
    -----------------
    Orthographic projection via D3DXMatrixOrthoOffCenterLH:
        (0, 0)           -> top-left screen pixel
        (DisplayW, DisplayH) -> bottom-right screen pixel
    Y axis increases downward (matches screen coords).
    Z is depth; tanks rendered at z=0 with z-test disabled so they always
    appear above the terrain quad.

    Light direction (in Y-down world space): normalize(0.5, -1.0, -0.5)
    Face shading bands:
        N·L > 0.5   -> bright  (player colour × 1.0)
        N·L > 0.05  -> mid     (player colour × 0.55)
        else        -> dark    (player colour × 0.25)

    Face normals and resulting bands:
        Top    (0,-1, 0):  N·L = 0.82 -> bright
        Front  (0, 0,-1):  N·L = 0.41 -> mid
        Right  (1, 0, 0):  N·L = 0.41 -> mid
        Bottom (0, 1, 0):  N·L = -0.82 -> dark
        Back   (0, 0, 1):  N·L = -0.41 -> dark
        Left   (-1, 0, 0): N·L = -0.41 -> dark

    Geometry (local space, origin at sprite center, units = pixels)
    ---------------------------------------------------------------
    Parts scale proportionally to tank nWidth / nHeight per type:
        Tracks : full width × bottom 28% of height × depth 2.5
        Hull   : 85% width × middle 55% of height  × depth 2.0
        Turret : 35% width × top 25% of height     × depth 1.5

    Outline inflate: 1.0 px per side.
    Barrel: rotated quad from pivot to tip, 1.5 px thick.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <d3dx8.h>
#include <math.h>
#include "tanks.h"
#include "player.h"
#include "render.h"

/* =========================================================================
   Types and constants
========================================================================= */

#define TANK_FVF  ( D3DFVF_XYZ | D3DFVF_DIFFUSE )

typedef struct { float x, y, z; DWORD c; } TV;

/* Light direction normalised (Y down) -- upper-left-forward */
#define LX  0.408248f   /*  1 / sqrt(6) */
#define LY -0.816497f   /* -2 / sqrt(6) */
#define LZ -0.408248f   /* -1 / sqrt(6) */

static D3DMATRIX s_matProj;
static D3DMATRIX s_matView;
static int       s_bInit = 0;

/* =========================================================================
   Toon colour helper
========================================================================= */

static float Dot3(float nx, float ny, float nz)
{
    return nx * LX + ny * LY + nz * LZ;
}

static DWORD ToonColor(DWORD base, float nx, float ny, float nz)
{
    float d = Dot3(nx, ny, nz);
    float band = (d > 0.5f) ? 1.0f : (d > 0.05f) ? 0.55f : 0.25f;
    int   r = (int)(((base >> 16) & 0xFF) * band);
    int   g = (int)(((base >> 8) & 0xFF) * band);
    int   b = (int)((base & 0xFF) * band);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
}

/* =========================================================================
   Box emitter
   Appends 36 vertices (12 triangles, 6 faces) to pOut[].
   Returns new vertex count.
========================================================================= */

static int EmitBox(TV* pOut, int n,
    float x0, float y0, float z0,
    float x1, float y1, float z1,
    DWORD baseColor)
{
    /* Per-face colours */
    DWORD cTop = ToonColor(baseColor, 0, -1, 0);
    DWORD cBot = ToonColor(baseColor, 0, 1, 0);
    DWORD cFront = ToonColor(baseColor, 0, 0, -1);
    DWORD cBack = ToonColor(baseColor, 0, 0, 1);
    DWORD cRight = ToonColor(baseColor, 1, 0, 0);
    DWORD cLeft = ToonColor(baseColor, -1, 0, 0);

#define V(px,py,pz,cc) pOut[n].x=(px); pOut[n].y=(py); pOut[n].z=(pz); pOut[n].c=(cc); n++

    /* Top face    (y = y0, normal 0,-1,0) */
    V(x0, y0, z0, cTop); V(x1, y0, z0, cTop); V(x1, y0, z1, cTop);
    V(x0, y0, z0, cTop); V(x1, y0, z1, cTop); V(x0, y0, z1, cTop);

    /* Bottom face (y = y1, normal 0,+1,0) */
    V(x0, y1, z1, cBot); V(x1, y1, z1, cBot); V(x1, y1, z0, cBot);
    V(x0, y1, z1, cBot); V(x1, y1, z0, cBot); V(x0, y1, z0, cBot);

    /* Front face  (z = z0, normal 0,0,-1) */
    V(x0, y1, z0, cFront); V(x1, y1, z0, cFront); V(x1, y0, z0, cFront);
    V(x0, y1, z0, cFront); V(x1, y0, z0, cFront); V(x0, y0, z0, cFront);

    /* Back face   (z = z1, normal 0,0,+1) */
    V(x0, y0, z1, cBack); V(x1, y0, z1, cBack); V(x1, y1, z1, cBack);
    V(x0, y0, z1, cBack); V(x1, y1, z1, cBack); V(x0, y1, z1, cBack);

    /* Right face  (x = x1, normal +1,0,0) */
    V(x1, y0, z0, cRight); V(x1, y0, z1, cRight); V(x1, y1, z1, cRight);
    V(x1, y0, z0, cRight); V(x1, y1, z1, cRight); V(x1, y1, z0, cRight);

    /* Left face   (x = x0, normal -1,0,0) */
    V(x0, y0, z1, cLeft); V(x0, y0, z0, cLeft); V(x0, y1, z0, cLeft);
    V(x0, y0, z1, cLeft); V(x0, y1, z0, cLeft); V(x0, y1, z1, cLeft);

#undef V
    return n;
}

/* Same box inflated outward by t on all sides, in solid black for outline */
static int EmitBoxOutline(TV* pOut, int n,
    float x0, float y0, float z0,
    float x1, float y1, float z1,
    float t)
{
    DWORD black = 0xFF000000u;
    return EmitBox(pOut, n,
        x0 - t, y0 - t, z0 - t,
        x1 + t, y1 + t, z1 + t,
        black);
    /* ToonColor(black, ...) = 0xFF000000 regardless of band since 0*x=0 */
}

/* =========================================================================
   Barrel quad helper
   Draws a rotated rect from pivot to tip, thickness thick, in color c.
========================================================================= */

#define MAX_BARREL_VERTS 6

static int EmitBarrel(TV* pOut, int n,
    float px, float py,
    float tx, float ty,
    float thick, DWORD c)
{
    /* Direction and perpendicular */
    float dx = tx - px, dy = ty - py;
    float len = (float)sqrt(dx * dx + dy * dy);
    float nx, ny;

    if (len < 0.01f) return n;

    nx = (-dy / len) * thick * 0.5f;
    ny = (dx / len) * thick * 0.5f;

    /* 2 triangles forming the barrel rect */
    pOut[n].x = px + nx; pOut[n].y = py + ny; pOut[n].z = 0.f; pOut[n].c = c; n++;
    pOut[n].x = px - nx; pOut[n].y = py - ny; pOut[n].z = 0.f; pOut[n].c = c; n++;
    pOut[n].x = tx - nx; pOut[n].y = ty - ny; pOut[n].z = 0.f; pOut[n].c = c; n++;

    pOut[n].x = px + nx; pOut[n].y = py + ny; pOut[n].z = 0.f; pOut[n].c = c; n++;
    pOut[n].x = tx - nx; pOut[n].y = ty - ny; pOut[n].z = 0.f; pOut[n].c = c; n++;
    pOut[n].x = tx + nx; pOut[n].y = ty + ny; pOut[n].z = 0.f; pOut[n].c = c; n++;

    return n;
}

/* =========================================================================
   Per-tank geometry buffer
   Up to 4 boxes × 36 verts = 144 per pass
========================================================================= */

#define TANK_VERTS_MAX 160

static TV s_fill[TANK_VERTS_MAX];
static TV s_outline[TANK_VERTS_MAX];

/* =========================================================================
   DrawOne  -- builds and submits geometry for a single player
========================================================================= */

static void DrawOne(const Player* pP)
{
    float hw = (float)pP->nWidth * 0.5f;
    float hh = (float)pP->nHeight * 0.5f;
    DWORD col = Player_GetColor(pP);
    DWORD dark = ToonColor(col, 0.f, 1.f, 0.f);
    DWORD lite = ToonColor(col, 1.f, 0.f, 0.f);

    float cx = (float)pP->nX + hw;
    float cy = (float)pP->nY + hh;

    float fScale = (float)g_dwDisplayW / 640.f;
    if (fScale < 1.f) fScale = 1.f;

    /* ── Per-type shape parameters ──────────────────────────────────
       hullW  : hull half-width ratio (of hw)
       turrW  : turret half-width ratio (of hw)
       hullTop: hull top Y offset ratio (of hh)  negative = up
       turrH  : turret height ratio (of hh)
       dep    : z-depth (affects outline thickness feel)
    ──────────────────────────────────────────────────────────────── */
    float hullW, turrW, hullTop, turrH, dep;
    int   bExtraBox = 0;   /* type-specific extra detail box */
    float exX0, exY0, exX1, exY1;   /* extra box bounds */

    switch (pP->nTankType)
    {
    default:
    case 0: /* Recon -- small, nimble, tall thin turret */
        hullW = 0.78f;  turrW = 0.22f;
        hullTop = 0.25f;  turrH = 1.05f;  dep = 1.5f;
        break;

    case 1: /* Medium -- balanced, classic shape */
        hullW = 0.85f;  turrW = 0.35f;
        hullTop = 0.30f;  turrH = 0.90f;  dep = 2.0f;
        break;

    case 2: /* Assault -- wide hull, squat heavy turret */
        hullW = 0.95f;  turrW = 0.46f;
        hullTop = 0.18f;  turrH = 0.65f;  dep = 2.5f;
        break;

    case 3: /* Artillery -- narrow body, tall raised gun mount */
        hullW = 0.68f;  turrW = 0.18f;
        hullTop = 0.28f;  turrH = 0.80f;  dep = 1.5f;
        /* gun mount platform: wide base under the turret */
        bExtraBox = 1;
        exX0 = -hw * 0.40f; exY0 = -hh * 0.28f - hh * 0.30f;
        exX1 = hw * 0.40f; exY1 = -hh * 0.28f + 0.5f;
        break;

    case 4: /* Siege -- maximum armour, very wide, squat turret */
        hullW = 1.00f;  turrW = 0.52f;
        hullTop = 0.12f;  turrH = 0.55f;  dep = 3.0f;
        /* sponson armour: small boxes on hull sides */
        bExtraBox = 1;
        exX0 = -hw * 1.00f; exY0 = hh * 0.05f;
        exX1 = hw * 1.00f; exY1 = hh * 0.40f;
        break;
    }

    /* Derived positions */
    float trackY0 = hh - hh * 0.38f;
    float trackY1 = hh;
    float hullY0 = -hh * hullTop;
    float hullY1 = trackY0 + 0.5f;
    float turrY1 = hullY0 + 0.5f;
    float turrY0 = turrY1 - hh * turrH;
    float hullHW = hw * hullW;
    float turrHW = hw * turrW;
    float outline = 1.0f * fScale;

    int nFill = 0, nOut = 0;

    /* -- Extra detail box (drawn first so body renders over it) -- */
    if (bExtraBox)
    {
        nFill = EmitBox(s_fill, nFill,
            exX0, exY0, -dep * 0.6f,
            exX1, exY1, dep * 0.6f, dark);
        nOut = EmitBoxOutline(s_outline, nOut,
            exX0, exY0, -dep * 0.6f,
            exX1, exY1, dep * 0.6f, outline);
    }

    /* -- Hull -- */
    nFill = EmitBox(s_fill, nFill,
        -hullHW, hullY0, -dep,
        hullHW, hullY1, dep, col);
    nOut = EmitBoxOutline(s_outline, nOut,
        -hullHW, hullY0, -dep,
        hullHW, hullY1, dep, outline);

    /* -- Tracks -- */
    nFill = EmitBox(s_fill, nFill,
        -hw, trackY0, -dep * 1.2f,
        hw, trackY1, dep * 1.2f, dark);
    nOut = EmitBoxOutline(s_outline, nOut,
        -hw, trackY0, -dep * 1.2f,
        hw, trackY1, dep * 1.2f, outline);

    /* -- Turret -- */
    nFill = EmitBox(s_fill, nFill,
        -turrHW, turrY0, -dep * 0.75f,
        turrHW, turrY1, dep * 0.75f, lite);
    nOut = EmitBoxOutline(s_outline, nOut,
        -turrHW, turrY0, -dep * 0.75f,
        turrHW, turrY1, dep * 0.75f, outline);

    /* --- World transform: translate to tank center ----------------------- */
    {
        D3DMATRIX world;
        D3DXMatrixTranslation((D3DXMATRIX*)&world, cx, cy, 0.0f);
        g_pd3dDevice->SetTransform(D3DTS_WORLD, &world);
    }

    /* --- Pass 0: barrel -- drawn FIRST so the turret covers its base ---- */
    {
        D3DMATRIX ident;
        float     fScale = (float)g_dwDisplayW / 640.f;
        float     px = (float)(pP->nX + pP->nPivotX);
        float     py = (float)(pP->nY + pP->nPivotY);
        float     tx = (float)Player_TurretX(pP, 1.0f);
        float     ty = (float)Player_TurretY(pP, 1.0f);
        int       nBarrel = 0;
        TV        barrel[12];

        if (fScale < 1.f) fScale = 1.f;

        D3DXMatrixIdentity((D3DXMATRIX*)&ident);
        g_pd3dDevice->SetTransform(D3DTS_WORLD, &ident);
        g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

        nBarrel = EmitBarrel(barrel, 0, px, py, tx, ty, 3.5f * fScale, 0xFF000000u);
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
            nBarrel / 3, barrel, sizeof(TV));
        nBarrel = EmitBarrel(barrel, 0, px, py, tx, ty, 1.5f * fScale, col);
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
            nBarrel / 3, barrel, sizeof(TV));

        /* Restore world transform for tank body */
        D3DXMatrixTranslation((D3DXMATRIX*)&ident, cx, cy, 0.0f);
        g_pd3dDevice->SetTransform(D3DTS_WORLD, &ident);
    }

    /* --- Pass 1: outline (back-face, inflated, black) ------------------- */
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
        nOut / 3,
        s_outline,
        sizeof(TV));

    /* --- Pass 2: fill (front-face, toon colours) ------------------------ */
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
        nFill / 3,
        s_fill,
        sizeof(TV));
}

/* =========================================================================
   Tanks_Init / Tanks_Shutdown
========================================================================= */

void Tanks_Init(void)
{
    D3DXMatrixOrthoOffCenterLH((D3DXMATRIX*)&s_matProj,
        0.f, (float)g_dwDisplayW,
        (float)g_dwDisplayH, 0.f,
        -50.f, 50.f);
    D3DXMatrixIdentity((D3DXMATRIX*)&s_matView);
    s_bInit = 1;
}

void Tanks_Shutdown(void)
{
    s_bInit = 0;
}

/* =========================================================================
   Tanks_DrawAll
========================================================================= */

void Tanks_DrawAll(const Player* pPlayers, int nCount)
{
    D3DMATRIX ident;
    int       i;

    if (!s_bInit) return;

    /* --- Set up 3D pipeline for tank rendering -------------------------- */
    D3DXMatrixIdentity((D3DXMATRIX*)&ident);

    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &s_matProj);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &s_matView);
    g_pd3dDevice->SetTransform(D3DTS_WORLD, &ident);

    g_pd3dDevice->SetVertexShader(TANK_FVF);
    g_pd3dDevice->SetTexture(0, NULL);

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    /* --- Draw each living player ---------------------------------------- */
    for (i = 0; i < nCount; i++)
    {
        if (pPlayers[i].bAlive)
            DrawOne(&pPlayers[i]);
    }

    /* --- Restore states ------------------------------------------------- */
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   Tanks_DrawPreview -- draw a single tank centered at (cx,cy) for setup UI.
   fScale multiplies the base spec dimensions for display size.
========================================================================= */

void Tanks_DrawPreview(int nTankType, DWORD dwColor, float cx, float cy, float fScale)
{
    Player  fake;
    D3DMATRIX ident;

    if (!s_bInit) return;
    if (nTankType < 0 || nTankType >= TANK_TYPE_COUNT) nTankType = 0;

    memset(&fake, 0, sizeof(fake));
    fake.bAlive = 1;
    fake.nTankType = nTankType;
    fake.nWidth = (int)(k_tankSpecs[nTankType].nWidth * fScale);
    fake.nHeight = (int)(k_tankSpecs[nTankType].nHeight * fScale);
    fake.nPivotX = (int)(k_tankSpecs[nTankType].nPivotX * fScale);
    fake.nPivotY = (int)(k_tankSpecs[nTankType].nPivotY * fScale);
    fake.nBarrelLen = (int)(k_tankSpecs[nTankType].nBarrelLen * fScale);
    fake.nAngle = 45;   /* fixed preview angle */
    fake.nX = (int)(cx - (float)fake.nWidth * 0.5f);
    fake.nY = (int)(cy - (float)fake.nHeight * 0.5f);
    fake.nID = 0;    /* use dwColor directly below */

    /* Override color: temporarily remap via k_playerColors index */
    /* Find which index matches dwColor, or use 0 */
    {
        int j;
        for (j = 0; j < 8; j++)
            if (k_playerColors[j] == dwColor) { fake.nID = j; break; }
    }

    D3DXMatrixIdentity((D3DXMATRIX*)&ident);
    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &s_matProj);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &s_matView);
    g_pd3dDevice->SetTransform(D3DTS_WORLD, &ident);

    g_pd3dDevice->SetVertexShader(TANK_FVF);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    DrawOne(&fake);

    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}