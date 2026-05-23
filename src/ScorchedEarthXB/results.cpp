/*---------------------------------------------------------------------------
    ScorchedXB - results.cpp
    Round / game-over results screen.
---------------------------------------------------------------------------*/

#include "results.h"
#include "game.h"
#include "player.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "input.h"

static DWORD s_dwEnter = 0;

/* =========================================================================
   Helpers
========================================================================= */

static void IStr(int n, char* buf, int cap)
{
    char tmp[12];
    int  i = 0, j = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (n < 0) { if (j < cap - 1) buf[j++] = '-'; n = -n; }
    while (n > 0 && i < 12) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0 && j < cap - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void DrawBar(float x, float y, float w, float h, float ratio, DWORD col)
{
    typedef struct { float x, y, z, rhw; DWORD c; }BV;
    BV v[4];
    DWORD bg = 0xFF222222u;
    v[0].x = x;   v[0].y = y;   v[0].z = 0; v[0].rhw = 1; v[0].c = bg;
    v[1].x = x + w; v[1].y = y;   v[1].z = 0; v[1].rhw = 1; v[1].c = bg;
    v[2].x = x;   v[2].y = y + h; v[2].z = 0; v[2].rhw = 1; v[2].c = bg;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 0; v[3].rhw = 1; v[3].c = bg;
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
    if (ratio > 0.f)
    {
        v[0].c = v[1].c = v[2].c = v[3].c = col;
        v[1].x = v[3].x = x + w * ratio;
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
    }
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   Public API
========================================================================= */

void Results_Init(void)
{
    Game_StopRumble();
    s_dwEnter = GetTickCount();
}

int Results_Update(WORD wPressed)
{
    if (GetTickCount() - s_dwEnter < 1200u) return RESULTS_ACTION_NONE;
    if (wPressed & (BTN_A | BTN_START | BTN_B))
        return Game_IsGameOver() ? RESULTS_ACTION_MENU : RESULTS_ACTION_CONTINUE;
    return RESULTS_ACTION_NONE;
}

void Results_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;
    int    nCount = Game_GetPlayerCount();
    int    i, winner = -1;
    float  rowH = (dh * 0.52f) / (float)(nCount > 0 ? nCount : 1);
    char   buf[20];

    UI_DrawUIBackground(220);

    /* ── Title ───────────────────────────────────────────────────── */
    {
        const char* t = Game_IsGameOver() ? "GAME OVER" : "ROUND COMPLETE";
        float tw = Font_Width(pBig, t);
        Font_Draw(pBig, t, (dw - tw) * 0.5f, dh * 0.03f, 0xFFFFCC00u);
    }

    /* Find winner (sole survivor) */
    {
        int alive = 0;
        for (i = 0; i < nCount; i++)
        {
            const Player* p = Game_GetPlayer(i);
            if (p && p->bAlive) { winner = i; alive++; }
        }
        if (alive != 1) winner = -1;
    }

    /* ── Winner banner ───────────────────────────────────────────── */
    if (winner >= 0)
    {
        const Player* pW = Game_GetPlayer(winner);
        if (pW)
        {
            char wb[32]; int j = 0;
            const char* pre = "WINNER: ";
            while (*pre) wb[j++] = *pre++;
            { const char* n = pW->szName; while (*n && j < 30) wb[j++] = *n++; }
            wb[j] = '\0';
            {
                float tw = Font_Width(pSmall, wb);
                Font_Draw(pSmall, wb, (dw - tw) * 0.5f, dh * 0.13f, Player_GetColor(pW));
            }
        }
    }

    /* ── Column headers ───────────────────────────────────────────── */
    {
        float hy = dh * 0.22f;
        Font_Draw(pSmall, "PLAYER", dw * 0.03f, hy, 0xFFAA9977u);
        Font_Draw(pSmall, "HEALTH", dw * 0.28f, hy, 0xFFAA9977u);
        Font_Draw(pSmall, "KILLS", dw * 0.60f, hy, 0xFFAA9977u);
        Font_Draw(pSmall, "EARNED", dw * 0.72f, hy, 0xFFAA9977u);
        Font_Draw(pSmall, "TOTAL", dw * 0.86f, hy, 0xFFAA9977u);
    }

    /* ── Player rows ─────────────────────────────────────────────── */
    for (i = 0; i < nCount; i++)
    {
        const Player* pP = Game_GetPlayer(i);
        float ry = dh * 0.28f + (float)i * rowH;
        DWORD col; float ratio;

        if (!pP) continue;
        col = pP->bAlive ? Player_GetColor(pP) : 0xFF444444u;
        ratio = pP->bAlive ? (float)pP->nPowerLimit / 1000.f : 0.f;
        if (ratio < 0.f) ratio = 0.f;
        if (ratio > 1.f) ratio = 1.f;

        /* Winner highlight row */
        if (i == winner)
        {
            typedef struct { float x, y, z, rhw; DWORD c; }BV;
            BV v[4]; DWORD bg = 0xFF3A2800u;
            v[0].x = dw * 0.01f; v[0].y = ry - 2;      v[0].z = 0; v[0].rhw = 1; v[0].c = bg;
            v[1].x = dw * 0.99f; v[1].y = ry - 2;      v[1].z = 0; v[1].rhw = 1; v[1].c = bg;
            v[2].x = dw * 0.01f; v[2].y = ry + rowH - 2; v[2].z = 0; v[2].rhw = 1; v[2].c = bg;
            v[3].x = dw * 0.99f; v[3].y = ry + rowH - 2; v[3].z = 0; v[3].rhw = 1; v[3].c = bg;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL);
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        }

        /* Name */
        Font_Draw(pSmall, pP->szName, dw * 0.03f, ry, col);

        /* Health bar */
        DrawBar(dw * 0.28f, ry + 2.f, dw * 0.28f, 13.f, ratio, col);

        /* Kills */
        IStr(pP->nKills, buf, 8);
        Font_Draw(pSmall, buf, dw * 0.60f, ry, col);

        /* Earned cash this round */
        buf[0] = '$'; IStr(pP->nEarnedCash, buf + 1, 10);
        Font_Draw(pSmall, buf, dw * 0.72f, ry, 0xFF88FF88u);

        /* Total cash */
        buf[0] = '$'; IStr(pP->nCash, buf + 1, 10);
        Font_Draw(pSmall, buf, dw * 0.86f, ry,
            pP->bAlive ? 0xFFFFCC00u : 0xFF666666u);
    }

    /* ── Hint ─────────────────────────────────────────────────────── */
    {
        const char* h = Game_IsGameOver()
            ? "A - Return to menu"
            : "A - Continue to store";
        float tw = Font_Width(pSmall, h);
        Font_Draw(pSmall, h, (dw - tw) * 0.5f, dh * 0.93f, 0xFF555555u);
    }
}