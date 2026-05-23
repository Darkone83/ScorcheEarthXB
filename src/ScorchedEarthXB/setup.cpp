/*---------------------------------------------------------------------------
    ScorchedXB - setup.cpp
    Two-tab pre-game setup screen.
    Tab 0: Tank type, color, AI config
    Tab 1: Match options (rounds, cash, wind, terrain)
---------------------------------------------------------------------------*/

#include "setup.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "input.h"
#include "tanks.h"
#include "player.h"

/* defaults: 3 rounds, $25k start, Normal wind, Random terrain */
SetupConfig g_setup = { 0, 0, 1, { 0, 1, 2 }, 1, 1, 2, 0 };

static int s_nTab = 0;   /* 0=player  1=match */
static int s_nSection = 0;

static const char* k_diffNames[3] = { "Shooter", "Cyborg", "Killer" };
static const DWORD k_diffCol[3] = { 0xFF88FFAAu, 0xFFFFDD44u, 0xFFFF6644u };

static const char* k_roundNames[4] = { "1",   "3",   "5",   "10" };
static const int   k_roundVals[4] = { 1,     3,     5,     10 };
static const char* k_cashNames[4] = { "$10k","$25k","$50k","$100k" };
static const int   k_cashVals[4] = { 10000, 25000, 50000, 100000 };
static const char* k_windNames[4] = { "None","Light","Normal","Strong" };
static const char* k_terrainNames[4] = { "Random","Flat","Hills","Mountains" };

/* =========================================================================
   Helpers
========================================================================= */

static void DrawCentred(Font* pF, const char* s, float y, DWORD c)
{
    float tw = Font_Width(pF, s);
    Font_Draw(pF, s, ((float)g_dwDisplayW - tw) * 0.5f, y, c);
}

static void DrawRowBg(float dw, float y, float h, int sel)
{
    typedef struct { float x, y, z, rhw; DWORD c; }BV;
    BV v[4]; DWORD bg = sel ? 0xFF443300u : 0xFF181818u;
    v[0].x = dw * 0.02f; v[0].y = y;  v[0].z = 0; v[0].rhw = 1; v[0].c = bg;
    v[1].x = dw * 0.98f; v[1].y = y;  v[1].z = 0; v[1].rhw = 1; v[1].c = bg;
    v[2].x = dw * 0.02f; v[2].y = y + h; v[2].z = 0; v[2].rhw = 1; v[2].c = bg;
    v[3].x = dw * 0.98f; v[3].y = y + h; v[3].z = 0; v[3].rhw = 1; v[3].c = bg;
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

static void DrawOptionRow(Font* pSmall, float dw, float y, float h,
    const char* label, const char* value,
    int sel, DWORD valCol)
{
    DrawRowBg(dw, y, h, sel);
    if (sel) Font_Draw(pSmall, ">", dw * 0.03f, y + 4.f, 0xFFFFCC00u);
    Font_Draw(pSmall, label, dw * 0.08f, y + 4.f, sel ? 0xFFFFFFFFu : 0xFF999999u);
    {
        float tw = Font_Width(pSmall, value);
        Font_Draw(pSmall, value, dw * 0.92f - tw, y + 4.f, valCol);
    }
}

/* =========================================================================
   Tab section counts
========================================================================= */

/* Tab 0: 0=tank 1=color 2=aicount 3..5=ai diffs */
#define TAB0_BASE_SECS  3   /* tank, color, aicount */

/* Tab 1: 0=rounds 1=cash 2=wind 3=terrain */
#define TAB1_SECS  4

static int Tab0SectionCount(void) { return TAB0_BASE_SECS + g_setup.nAICount; }
static int Tab1SectionCount(void) { return TAB1_SECS; }
static int CurTabSections(void) { return s_nTab == 0 ? Tab0SectionCount() : Tab1SectionCount(); }

/* =========================================================================
   Setup_Init / Update
========================================================================= */

void Setup_Init(void)
{
    s_nTab = 0;
    s_nSection = 0;
    Tanks_Init();
}

int Setup_Update(WORD wPressed)
{
    int maxSec = CurTabSections() - 1;

    /* Tab switch with Black/White */
    if (wPressed & BTN_BLACK) { s_nTab = 1; s_nSection = 0; }
    if (wPressed & BTN_WHITE) { s_nTab = 0; s_nSection = 0; }

    /* Navigate sections */
    if (wPressed & BTN_DPAD_DOWN) { s_nSection++; if (s_nSection > maxSec) s_nSection = 0; }
    if (wPressed & BTN_DPAD_UP) { s_nSection--; if (s_nSection < 0) s_nSection = maxSec; }

    /* Change value */
    if (wPressed & (BTN_DPAD_LEFT | BTN_DPAD_RIGHT))
    {
        int dir = (wPressed & BTN_DPAD_RIGHT) ? 1 : -1;

        if (s_nTab == 0)
        {
            if (s_nSection == 0) { g_setup.nTankType += dir; if (g_setup.nTankType < 0)g_setup.nTankType = 4; if (g_setup.nTankType > 4)g_setup.nTankType = 0; }
            if (s_nSection == 1) { g_setup.nColorIdx += dir; if (g_setup.nColorIdx < 0)g_setup.nColorIdx = 7; if (g_setup.nColorIdx > 7)g_setup.nColorIdx = 0; }
            if (s_nSection == 2) { g_setup.nAICount += dir; if (g_setup.nAICount < 1) g_setup.nAICount = 3;  if (g_setup.nAICount > 3) g_setup.nAICount = 1; }
            if (s_nSection >= 3)
            {
                int ai = s_nSection - 3;
                g_setup.nAIDiff[ai] += dir;
                if (g_setup.nAIDiff[ai] < 0)g_setup.nAIDiff[ai] = 2;
                if (g_setup.nAIDiff[ai] > 2)g_setup.nAIDiff[ai] = 0;
            }
        }
        else
        {
            if (s_nSection == 0) { g_setup.nRounds += dir; if (g_setup.nRounds < 0)      g_setup.nRounds = 3;       if (g_setup.nRounds > 3)      g_setup.nRounds = 0; }
            if (s_nSection == 1) { g_setup.nStartCash += dir; if (g_setup.nStartCash < 0)   g_setup.nStartCash = 3;    if (g_setup.nStartCash > 3)   g_setup.nStartCash = 0; }
            if (s_nSection == 2) { g_setup.nWindStrength += dir; if (g_setup.nWindStrength < 0) g_setup.nWindStrength = 3; if (g_setup.nWindStrength > 3) g_setup.nWindStrength = 0; }
            if (s_nSection == 3) { g_setup.nTerrainType += dir; if (g_setup.nTerrainType < 0)  g_setup.nTerrainType = 3;  if (g_setup.nTerrainType > 3)  g_setup.nTerrainType = 0; }
        }
    }

    if (wPressed & BTN_A) return SETUP_ACTION_DONE;
    return SETUP_ACTION_NONE;
}

/* =========================================================================
   Setup_Draw -- Tab 0
========================================================================= */

static void DrawTab0(Font* pBig, Font* pSmall, float dw, float dh)
{
    int   i;
    float spacing = dw * 0.14f;
    float previewCX = dw * 0.5f;
    float previewY = dh * 0.22f;
    DWORD selCol = 0xFFFFCC00u;
    DWORD dimCol = 0xFF666666u;

    /* ── Tank Type ── */
    {
        DWORD lc = (s_nSection == 0) ? selCol : dimCol;
        DrawCentred(pSmall, "TANK TYPE   < >", dh * 0.14f, lc);

        for (i = 0; i < 5; i++)
        {
            static const char* k_names[5] = { "RECON","MEDIUM","ASSAULT","ARTY","SIEGE" };
            float tx = previewCX + ((float)(i - 2)) * spacing;
            DWORD col = k_playerColors[g_setup.nColorIdx];
            float fSc = (i == g_setup.nTankType) ? 1.6f : 0.9f;
            DWORD boxCol = (i == g_setup.nTankType) ? 0xFF443300u : 0xFF1A1A1Au;
            float bw = spacing * 0.85f;
            typedef struct { float x, y, z, rhw; DWORD c; }BV; BV v[4];
            v[0].x = tx - bw * 0.5f; v[0].y = previewY - 4; v[0].z = 0; v[0].rhw = 1; v[0].c = boxCol;
            v[1].x = tx + bw * 0.5f; v[1].y = previewY - 4; v[1].z = 0; v[1].rhw = 1; v[1].c = boxCol;
            v[2].x = tx - bw * 0.5f; v[2].y = previewY + 60; v[2].z = 0; v[2].rhw = 1; v[2].c = boxCol;
            v[3].x = tx + bw * 0.5f; v[3].y = previewY + 60; v[3].z = 0; v[3].rhw = 1; v[3].c = boxCol;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL); g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);

            Tanks_DrawPreview(i, col, tx, previewY + 22.f, fSc);
            {
                float tw = Font_Width(pSmall, k_names[i]);
                Font_Draw(pSmall, k_names[i], tx - tw * 0.5f, previewY + 48.f,
                    (i == g_setup.nTankType) ? selCol : 0xFF555555u);
            }
        }
    }

    /* ── Tank Color ── */
    {
        float swatchY = dh * 0.50f;
        float sw = 26.f, sh = 16.f;
        float totalW = 8.f * (sw + 6.f) - 6.f;
        float startX = dw * 0.5f - totalW * 0.5f;
        DWORD lc = (s_nSection == 1) ? selCol : dimCol;
        DrawCentred(pSmall, "TANK COLOR   < >", dh * 0.46f, lc);
        for (i = 0; i < 8; i++)
        {
            float sx = startX + (float)i * (sw + 6.f);
            DWORD col = k_playerColors[i];
            typedef struct { float x, y, z, rhw; DWORD c; }BV; BV v[4];
            v[0].x = sx;   v[0].y = swatchY;   v[0].z = 0; v[0].rhw = 1; v[0].c = col;
            v[1].x = sx + sw; v[1].y = swatchY;   v[1].z = 0; v[1].rhw = 1; v[1].c = col;
            v[2].x = sx;   v[2].y = swatchY + sh; v[2].z = 0; v[2].rhw = 1; v[2].c = col;
            v[3].x = sx + sw; v[3].y = swatchY + sh; v[3].z = 0; v[3].rhw = 1; v[3].c = col;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL); g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
            if (i == g_setup.nColorIdx)
            {
                char sel[2] = { '*','\0' };
                Font_Draw(pSmall, sel, sx + sw * 0.3f, swatchY + sh + 2.f, selCol);
            }
        }
    }

    /* ── AI Opponents ── */
    {
        float baseY = dh * 0.62f;
        float rowH = 22.f;
        char  buf[24];
        DWORD lc = (s_nSection == 2) ? selCol : dimCol;
        buf[0] = 'A'; buf[1] = 'I'; buf[2] = ' '; buf[3] = 'O'; buf[4] = 'P'; buf[5] = 'P';
        buf[6] = 'O'; buf[7] = 'N'; buf[8] = 'E'; buf[9] = 'N'; buf[10] = 'T'; buf[11] = 'S';
        buf[12] = ':'; buf[13] = ' '; buf[14] = (char)('0' + g_setup.nAICount); buf[15] = '\0';
        DrawCentred(pSmall, buf, baseY, lc);
        for (i = 0; i < g_setup.nAICount; i++)
        {
            float iy = baseY + rowH + (float)i * rowH;
            int   d = g_setup.nAIDiff[i];
            DWORD dc = (s_nSection == 3 + i) ? k_diffCol[d] : dimCol;
            char  nb[24]; int j = 0;
            const char* dn = k_diffNames[d];
            nb[j++] = 'A'; nb[j++] = 'I'; nb[j++] = ' '; nb[j++] = (char)('1' + i); nb[j++] = ':'; nb[j++] = ' ';
            while (*dn && j < 22)nb[j++] = *dn++; nb[j] = '\0';
            DrawCentred(pSmall, nb, iy, dc);
        }
    }
}

/* =========================================================================
   Setup_Draw -- Tab 1 (Match Options)
========================================================================= */

static void DrawTab1(Font* pBig, Font* pSmall, float dw, float dh)
{
    float startY = dh * 0.22f;
    float rowH = dh * 0.13f;

    DrawCentred(pBig, "MATCH OPTIONS", dh * 0.10f, 0xFFFFCC00u);

    DrawOptionRow(pSmall, dw, startY + rowH * 0.f, rowH - 4.f, "ROUNDS",
        k_roundNames[g_setup.nRounds], s_nSection == 0, 0xFFFFFFFFu);
    DrawOptionRow(pSmall, dw, startY + rowH * 1.f, rowH - 4.f, "STARTING CASH",
        k_cashNames[g_setup.nStartCash], s_nSection == 1, 0xFF88FF88u);
    DrawOptionRow(pSmall, dw, startY + rowH * 2.f, rowH - 4.f, "WIND",
        k_windNames[g_setup.nWindStrength], s_nSection == 2, 0xFF88DDFFu);
    DrawOptionRow(pSmall, dw, startY + rowH * 3.f, rowH - 4.f, "TERRAIN",
        k_terrainNames[g_setup.nTerrainType], s_nSection == 3, 0xFFFFAA44u);
}

/* =========================================================================
   Setup_Draw
========================================================================= */

void Setup_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;

    UI_DrawUIBackground(240);

    /* Tab indicator */
    {
        float tw0 = Font_Width(pSmall, "PLAYER SETUP");
        float tw1 = Font_Width(pSmall, "MATCH OPTIONS");
        float gap = dw * 0.04f;
        float totalW = tw0 + gap + tw1;
        float sx = dw * 0.5f - totalW * 0.5f;
        Font_Draw(pSmall, "PLAYER SETUP", sx, dh * 0.04f,
            s_nTab == 0 ? 0xFFFFCC00u : 0xFF444444u);
        Font_Draw(pSmall, "MATCH OPTIONS", sx + tw0 + gap, dh * 0.04f,
            s_nTab == 1 ? 0xFFFFCC00u : 0xFF444444u);
    }

    if (s_nTab == 0) DrawTab0(pBig, pSmall, dw, dh);
    else            DrawTab1(pBig, pSmall, dw, dh);

    /* Hints */
    DrawCentred(pSmall,
        "U/D select   L/R change   Black/White switch tab   A start",
        dh * 0.93f, 0xFF444444u);
}