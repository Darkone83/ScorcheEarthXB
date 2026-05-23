/*---------------------------------------------------------------------------
    ScorchedXB - options.cpp
    Music Vol / SFX Vol / Rumble / Wall Type.
---------------------------------------------------------------------------*/

#include "options.h"
#include "config.h"
#include "input.h"
#include "ui.h"
#include "render.h"

#define OPT_COUNT   4
#define OPT_MUSIC   0
#define OPT_SFX     1
#define OPT_RUMBLE  2
#define OPT_WALL    3

static int s_nSel = 0;

void Options_Init(void) { s_nSel = 0; }

int Options_Update(WORD wPressed)
{
    if (wPressed & BTN_DPAD_UP) { s_nSel--; if (s_nSel < 0) s_nSel = OPT_COUNT - 1; }
    if (wPressed & BTN_DPAD_DOWN) { s_nSel++; if (s_nSel >= OPT_COUNT) s_nSel = 0; }

    if (wPressed & (BTN_DPAD_LEFT | BTN_DPAD_RIGHT))
    {
        int dir = (wPressed & BTN_DPAD_RIGHT) ? 1 : -1;
        if (s_nSel == OPT_MUSIC) { g_cfg.nMusicVol += dir; if (g_cfg.nMusicVol < 0) g_cfg.nMusicVol = 0; if (g_cfg.nMusicVol > 10) g_cfg.nMusicVol = 10; }
        if (s_nSel == OPT_SFX) { g_cfg.nSFXVol += dir; if (g_cfg.nSFXVol < 0)   g_cfg.nSFXVol = 0;   if (g_cfg.nSFXVol > 10)   g_cfg.nSFXVol = 10; }
        if (s_nSel == OPT_RUMBLE) g_cfg.bRumble = !g_cfg.bRumble;
        if (s_nSel == OPT_WALL) { g_cfg.nWallType += dir; if (g_cfg.nWallType < 0) g_cfg.nWallType = 2; if (g_cfg.nWallType > 2) g_cfg.nWallType = 0; }
        Config_Apply();
    }

    if (wPressed & BTN_B) { Config_Save(); return OPTS_ACTION_BACK; }
    return OPTS_ACTION_NONE;
}

static void DrawBar(float x, float y, int v, int mx, DWORD c)
{
    typedef struct { float x, y, z, rhw; DWORD c; }BV;
    float bw = 200.f, bh = 12.f, fw = bw * (float)v / (float)mx;
    BV vx[4];
    vx[0].z = 0; vx[0].rhw = 1; vx[1].z = 0; vx[1].rhw = 1; vx[2].z = 0; vx[2].rhw = 1; vx[3].z = 0; vx[3].rhw = 1;
    vx[0].c = vx[1].c = vx[2].c = vx[3].c = 0xFF333333u;
    vx[0].x = x; vx[0].y = y; vx[1].x = x + bw; vx[1].y = y; vx[2].x = x; vx[2].y = y + bh; vx[3].x = x + bw; vx[3].y = y + bh;
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vx, sizeof(BV));
    if (fw > 0.f) { vx[0].c = vx[1].c = vx[2].c = vx[3].c = c; vx[1].x = x + fw; vx[3].x = x + fw; g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vx, sizeof(BV)); }
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

void Options_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;
    float  cx = dw * 0.5f;
    float  startY = dh * 0.26f;
    float  rowH = 56.f;
    float  tw;
    int    i;

    static const char* k_labels[OPT_COUNT] = {
        "MUSIC VOLUME", "SFX VOLUME", "RUMBLE", "WALLS"
    };
    static const char* k_wallNames[3] = { "NONE", "WRAP", "BOUNCE" };

    UI_DrawUIBackground(220);

    tw = Font_Width(pBig, "OPTIONS");
    Font_Draw(pBig, "OPTIONS", cx - tw * 0.5f, dh * 0.10f, 0xFFFFCC00u);

    for (i = 0; i < OPT_COUNT; i++)
    {
        float iy = startY + (float)i * rowH;
        DWORD col = (i == s_nSel) ? 0xFFFFFFFF : 0xFF888888u;
        DWORD sel = (i == s_nSel) ? 0xFF00FFFFu : 0xFF005555u;

        if (i == s_nSel) Font_Draw(pSmall, ">", cx - 280.f, iy, 0xFFFFCC00u);
        Font_Draw(pSmall, k_labels[i], cx - 260.f, iy, col);

        if (i == OPT_RUMBLE)
            Font_Draw(pSmall, g_cfg.bRumble ? "ON" : "OFF", cx + 60.f, iy,
                g_cfg.bRumble ? 0xFF00FF88u : 0xFF884444u);
        else if (i == OPT_WALL)
            Font_Draw(pSmall, k_wallNames[g_cfg.nWallType], cx + 60.f, iy,
                (g_cfg.nWallType == 0) ? 0xFF888888u : 0xFF00FF88u);
        else
        {
            int vol = (i == OPT_MUSIC) ? g_cfg.nMusicVol : g_cfg.nSFXVol;
            DrawBar(cx + 50.f, iy + 2.f, vol, 10, sel);
            { char n[4]; n[0] = (char)('0' + vol / 10); n[1] = (char)('0' + vol % 10); n[2] = '\0'; Font_Draw(pSmall, n, cx + 260.f, iy, col); }
        }
    }

    tw = Font_Width(pSmall, "< > change    B back");
    Font_Draw(pSmall, "< > change    B back", cx - tw * 0.5f, dh * 0.87f, 0xFF666666u);
}