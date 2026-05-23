/*---------------------------------------------------------------------------
    ScorchedXB - setup.cpp
    Pre-game setup screen: tank type, color, AI count + difficulty.
    Uses ui.dds background + tank previews from tanks.cpp.
---------------------------------------------------------------------------*/

#include "setup.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "input.h"
#include "tanks.h"
#include "player.h"
SetupConfig g_setup = { 0, 0, 1, { 0, 1, 2 } };

/* =========================================================================
   Navigation
   Sections: 0=TankType  1=TankColor  2=AICount  3=AIDiff[0..2]
========================================================================= */

static int s_nSection = 0;   /* 0=tank  1=color  2=AIcount  3-5=AIdiff */
#define SECTION_COUNT  5       /* tank, color, aicount, ai0diff..ai2diff */

static const char* k_diffNames[3] = { "Shooter", "Cyborg", "Killer" };
static const char* k_diffColors[3] = { 0 };   /* unused, using DWORD below */
static const DWORD k_diffCol[3] = { 0xFF88FFAAu, 0xFFFFDD44u, 0xFFFF6644u };

/* =========================================================================
   Helpers
========================================================================= */

static void DrawLabel(Font* pF, const char* s, float x, float y, DWORD c)
{
    Font_Draw(pF, s, x, y, c);
}

static void DrawCentred(Font* pF, const char* s, float y, DWORD c)
{
    float tw = Font_Width(pF, s);
    Font_Draw(pF, s, ((float)g_dwDisplayW - tw) * 0.5f, y, c);
}

/* =========================================================================
   Setup_Init
========================================================================= */

void Setup_Init(void)
{
    s_nSection = 0;
    Tanks_Init();   /* required for DrawPreview */
}

/* =========================================================================
   Setup_Update
========================================================================= */

int Setup_Update(WORD wPressed)
{
    /* Navigate sections */
    if (wPressed & BTN_DPAD_DOWN)
    {
        s_nSection++;
        /* Skip AI diff sections beyond nAICount */
        if (s_nSection == 3 && g_setup.nAICount < 1) s_nSection++;
        if (s_nSection == 4 && g_setup.nAICount < 2) s_nSection++;
        if (s_nSection == 5 && g_setup.nAICount < 3) s_nSection++;
        if (s_nSection > 2 + g_setup.nAICount) s_nSection = 0;
    }
    if (wPressed & BTN_DPAD_UP)
    {
        s_nSection--;
        if (s_nSection < 0) s_nSection = 2 + g_setup.nAICount;
    }

    /* Change value */
    if (wPressed & BTN_DPAD_LEFT)
    {
        if (s_nSection == 0) { g_setup.nTankType--;  if (g_setup.nTankType < 0)  g_setup.nTankType = 4; }
        if (s_nSection == 1) { g_setup.nColorIdx--;  if (g_setup.nColorIdx < 0)  g_setup.nColorIdx = 7; }
        if (s_nSection == 2) { g_setup.nAICount--;   if (g_setup.nAICount < 1)   g_setup.nAICount = 3; }
        if (s_nSection >= 3)
        {
            int ai = s_nSection - 3;
            g_setup.nAIDiff[ai]--;
            if (g_setup.nAIDiff[ai] < 0) g_setup.nAIDiff[ai] = 2;
        }
    }
    if (wPressed & BTN_DPAD_RIGHT)
    {
        if (s_nSection == 0) { g_setup.nTankType++;  if (g_setup.nTankType > 4)  g_setup.nTankType = 0; }
        if (s_nSection == 1) { g_setup.nColorIdx++;  if (g_setup.nColorIdx > 7)  g_setup.nColorIdx = 0; }
        if (s_nSection == 2) { g_setup.nAICount++;   if (g_setup.nAICount > 3)   g_setup.nAICount = 1; }
        if (s_nSection >= 3)
        {
            int ai = s_nSection - 3;
            g_setup.nAIDiff[ai]++;
            if (g_setup.nAIDiff[ai] > 2) g_setup.nAIDiff[ai] = 0;
        }
    }

    if (wPressed & BTN_A) return SETUP_ACTION_DONE;
    if (wPressed & BTN_B) return SETUP_ACTION_DONE;

    return SETUP_ACTION_NONE;
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
    float  cx = dw * 0.5f;
    int    i;
    DWORD  selCol = 0xFFFFCC00u;
    DWORD  dimCol = 0xFF888888u;

    UI_DrawUIBackground(240);

    /* Title */
    DrawCentred(pBig, "GAME SETUP", dh * 0.07f, 0xFFFFCC00u);

    /* ── Tank Type ──────────────────────────────────────────────── */
    {
        float labelY = dh * 0.20f;
        float previewY = dh * 0.26f;
        float previewCX = cx;
        float spacing = dw * 0.14f;
        DWORD lc = (s_nSection == 0) ? selCol : dimCol;

        DrawCentred(pSmall, "TANK TYPE", labelY, lc);
        if (s_nSection == 0)
            DrawCentred(pSmall, "< >", labelY, 0xFFFFFFFFu);

        /* Draw 5 tank previews in a row */
        for (i = 0; i < 5; i++)
        {
            static const char* k_names[5] = { "RECON", "MEDIUM", "ASSAULT", "ARTY", "SIEGE" };
            float tx = previewCX + ((float)(i - 2)) * spacing;
            DWORD col = k_playerColors[g_setup.nColorIdx];
            float fSc = (i == g_setup.nTankType) ? 1.8f : 1.0f;
            DWORD boxCol = (i == g_setup.nTankType) ? 0xFF443300u : 0xFF1A1A1Au;
            float nameY = previewY + 38.f;

            /* Highlight box */
            {
                typedef struct { float x, y, z, rhw; DWORD c; } BV;
                BV v[4];
                float bw = spacing * 0.85f, bh = 48.f;
                v[0].x = tx - bw * 0.5f; v[0].y = previewY - 6;    v[0].z = 0; v[0].rhw = 1; v[0].c = boxCol;
                v[1].x = tx + bw * 0.5f; v[1].y = previewY - 6;    v[1].z = 0; v[1].rhw = 1; v[1].c = boxCol;
                v[2].x = tx - bw * 0.5f; v[2].y = previewY + bh;   v[2].z = 0; v[2].rhw = 1; v[2].c = boxCol;
                v[3].x = tx + bw * 0.5f; v[3].y = previewY + bh;   v[3].z = 0; v[3].rhw = 1; v[3].c = boxCol;
                g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
                g_pd3dDevice->SetTexture(0, NULL);
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
                g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
            }

            Tanks_DrawPreview(i, col, tx, previewY + 18.f, fSc);

            /* Tank type name */
            {
                float tw = Font_Width(pSmall, k_names[i]);
                DWORD nc = (i == g_setup.nTankType) ? 0xFFFFCC00u : 0xFF666666u;
                Font_Draw(pSmall, k_names[i], tx - tw * 0.5f, nameY, nc);
            }
        }
    }

    /* ── Tank Color ──────────────────────────────────────────────── */
    {
        float labelY = dh * 0.46f;
        float swatchY = dh * 0.54f;
        float sw = 28.f, sh = 18.f;
        float totalW = 8.f * (sw + 6.f) - 6.f;
        float startX = cx - totalW * 0.5f;
        DWORD lc = (s_nSection == 1) ? selCol : dimCol;

        DrawCentred(pSmall, "TANK COLOR", labelY, lc);

        for (i = 0; i < 8; i++)
        {
            float sx = startX + (float)i * (sw + 6.f);
            DWORD col = k_playerColors[i];
            typedef struct { float x, y, z, rhw; DWORD c; } BV;
            BV v[4];

            v[0].x = sx;    v[0].y = swatchY;    v[0].z = 0; v[0].rhw = 1; v[0].c = col;
            v[1].x = sx + sw; v[1].y = swatchY;    v[1].z = 0; v[1].rhw = 1; v[1].c = col;
            v[2].x = sx;    v[2].y = swatchY + sh; v[2].z = 0; v[2].rhw = 1; v[2].c = col;
            v[3].x = sx + sw; v[3].y = swatchY + sh; v[3].z = 0; v[3].rhw = 1; v[3].c = col;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL);
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);

            if (i == g_setup.nColorIdx)
            {
                /* Selection border */
                char sel[2] = { '*', '\0' };
                Font_Draw(pSmall, sel, sx + sw * 0.3f, swatchY + sh + 2.f, 0xFFFFCC00u);
            }
        }
    }

    /* ── AI Opponents ────────────────────────────────────────────── */
    {
        float baseY = dh * 0.64f;
        char buf[32];
        DWORD lc = (s_nSection == 2) ? selCol : dimCol;

        buf[0] = 'A'; buf[1] = 'I'; buf[2] = ' '; buf[3] = 'O'; buf[4] = 'P'; buf[5] = 'P'; buf[6] = 'O';
        buf[7] = 'N'; buf[8] = 'E'; buf[9] = 'N'; buf[10] = 'T'; buf[11] = 'S'; buf[12] = ':'; buf[13] = ' ';
        buf[14] = (char)('0' + g_setup.nAICount); buf[15] = '\0';
        DrawCentred(pSmall, buf, baseY, lc);

        for (i = 0; i < g_setup.nAICount; i++)
        {
            float iy = baseY + 24.f + (float)i * 22.f;
            int   d = g_setup.nAIDiff[i];
            DWORD dc = (s_nSection == 3 + i) ? selCol : dimCol;
            char  namebuf[24];
            int   j;
            const char* dn = k_diffNames[d];

            namebuf[0] = 'A'; namebuf[1] = 'I'; namebuf[2] = ' ';
            namebuf[3] = (char)('1' + i); namebuf[4] = ':'; namebuf[5] = ' ';
            for (j = 0; dn[j] && j < 16; j++) namebuf[6 + j] = dn[j];
            namebuf[6 + j] = '\0';

            DrawCentred(pSmall, namebuf, iy, (s_nSection == 3 + i) ? k_diffCol[d] : dc);
        }
    }

    /* ── Hints ───────────────────────────────────────────────────── */
    DrawCentred(pSmall, "UP/DOWN select   LEFT/RIGHT change   A start", dh * 0.92f, 0xFF555555u);
}