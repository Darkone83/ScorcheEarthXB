/*---------------------------------------------------------------------------
    ScorchedXB - jukebox.cpp
    Jukebox screen.  Plays the full soundtrack with track info + progress.
    Credits nebula+stars background.
    White/Left=Prev  Black/Right=Next  A=Play/Stop  B=Back
---------------------------------------------------------------------------*/

#include "jukebox.h"
#include "audio.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "input.h"

#define TRACK_COUNT  7

static const char* k_trackNames[TRACK_COUNT] =
{
    "Scorched Steel Menu",
    "Scorched Cannon Menu",
    "Steel Orders",
    "Scorched Trajectory",
    "Ashfall Duel",
    "Ashen Command",
    "Ashes After Victory",
};

/* Approximate track durations in seconds (for progress bar) */
static const int k_trackDurSec[TRACK_COUNT] =
{
    180, 180, 180, 180, 180, 180, 180
};

static int   s_nTrack = 0;
static int   s_bPlaying = 0;
static DWORD s_dwStart = 0;

/* =========================================================================
   Background -- nebula gradient + simple starfield
========================================================================= */

#define JB_STAR_COUNT 150
typedef struct { float x, y, z; BYTE brightness; } JBStar;
static JBStar s_stars[JB_STAR_COUNT];
static int    s_starsInit = 0;
static DWORD  s_starSeed = 0xABCD1234u;

static DWORD SRand(void)
{
    s_starSeed = s_starSeed * 1664525u + 1013904223u;
    return s_starSeed;
}

static void InitStars(void)
{
    int i;
    if (s_starsInit) return;
    s_starSeed ^= GetTickCount();
    for (i = 0; i < JB_STAR_COUNT; i++)
    {
        DWORD r = SRand();
        s_stars[i].z = (float)(r & 1023u) * (1.f / 1023.f);
        r = SRand();
        s_stars[i].x = (float)(r % (DWORD)g_dwDisplayW);
        r = SRand();
        s_stars[i].y = (float)(r % (DWORD)g_dwDisplayH);
        s_stars[i].brightness = (BYTE)(80u + (DWORD)(s_stars[i].z * 175.f));
    }
    s_starsInit = 1;
}

static void UpdateStars(void)
{
    int i;
    float dh = (float)g_dwDisplayH;
    float dw = (float)g_dwDisplayW;
    for (i = 0; i < JB_STAR_COUNT; i++)
    {
        s_stars[i].y -= 0.2f + s_stars[i].z * 0.5f;
        if (s_stars[i].y < -4.f)
        {
            DWORD r = SRand();
            s_stars[i].y = dh + 4.f;
            s_stars[i].x = (float)(r % (DWORD)dw);
        }
    }
}

static void DrawBackground(void)
{
    typedef struct { float x, y, z, rhw; DWORD c; }GV;
    float dw = (float)g_dwDisplayW, dh = (float)g_dwDisplayH;
    DWORD cT = D3DCOLOR_XRGB(18, 8, 38), cM = D3DCOLOR_XRGB(5, 5, 22), cB = D3DCOLOR_XRGB(2, 2, 8);
    GV v[6]; int i;

    v[0].x = 0; v[0].y = 0;       v[0].z = 0; v[0].rhw = 1; v[0].c = cT;
    v[1].x = dw; v[1].y = 0;       v[1].z = 0; v[1].rhw = 1; v[1].c = cT;
    v[2].x = 0; v[2].y = dh * 0.4f; v[2].z = 0; v[2].rhw = 1; v[2].c = cM;
    v[3].x = dw; v[3].y = dh * 0.4f; v[3].z = 0; v[3].rhw = 1; v[3].c = cM;
    v[4].x = 0; v[4].y = dh;      v[4].z = 0; v[4].rhw = 1; v[4].c = cB;
    v[5].x = dw; v[5].y = dh;      v[5].z = 0; v[5].rhw = 1; v[5].c = cB;
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 4, v, sizeof(GV));

    /* Stars */
    {
        typedef struct { float x, y, z, rhw; DWORD c; }SV;
        SV q[4]; float sz;
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        for (i = 0; i < JB_STAR_COUNT; i++)
        {
            BYTE b = s_stars[i].brightness;
            DWORD c = D3DCOLOR_ARGB(b, b, b, 255);
            sz = 1.f + s_stars[i].z * 1.5f;
            q[0].x = s_stars[i].x;    q[0].y = s_stars[i].y;    q[0].z = 0; q[0].rhw = 1; q[0].c = c;
            q[1].x = s_stars[i].x + sz; q[1].y = s_stars[i].y;    q[1].z = 0; q[1].rhw = 1; q[1].c = c;
            q[2].x = s_stars[i].x;    q[2].y = s_stars[i].y + sz; q[2].z = 0; q[2].rhw = 1; q[2].c = c;
            q[3].x = s_stars[i].x + sz; q[3].y = s_stars[i].y + sz; q[3].z = 0; q[3].rhw = 1; q[3].c = c;
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(SV));
        }
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   Public API
========================================================================= */

void Jukebox_Init(void)
{
    s_nTrack = 0;
    s_bPlaying = 0;
    InitStars();
}

int Jukebox_Update(WORD wPressed)
{
    UpdateStars();

    if (wPressed & BTN_B) { Audio_MusicStop(); s_bPlaying = 0; return 1; }

    /* Next track */
    if ((wPressed & BTN_BLACK) || (wPressed & BTN_DPAD_RIGHT))
    {
        s_nTrack++;
        if (s_nTrack >= TRACK_COUNT) s_nTrack = 0;
        if (s_bPlaying) { Audio_MusicPlayTrack(s_nTrack); s_dwStart = GetTickCount(); }
    }

    /* Prev track */
    if ((wPressed & BTN_WHITE) || (wPressed & BTN_DPAD_LEFT))
    {
        s_nTrack--;
        if (s_nTrack < 0) s_nTrack = TRACK_COUNT - 1;
        if (s_bPlaying) { Audio_MusicPlayTrack(s_nTrack); s_dwStart = GetTickCount(); }
    }

    /* Play / Stop toggle */
    if (wPressed & BTN_A)
    {
        if (s_bPlaying)
        {
            Audio_MusicStop();
            s_bPlaying = 0;
        }
        else
        {
            Audio_MusicPlayTrack(s_nTrack);
            s_dwStart = GetTickCount();
            s_bPlaying = 1;
        }
    }

    return 0;
}

void Jukebox_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;
    float  cx = dw * 0.5f;
    float  tw;

    DrawBackground();

    /* Title */
    {
        const char* t = "JUKEBOX";
        tw = Font_Width(pBig, t);
        Font_Draw(pBig, t, cx - tw * 0.5f, dh * 0.08f, 0xFFFFCC00u);
    }

    /* Track number */
    {
        char buf[8];
        buf[0] = (char)('0' + (s_nTrack + 1) / 10);
        buf[1] = (char)('0' + (s_nTrack + 1) % 10);
        buf[2] = '/';
        buf[3] = '0' + TRACK_COUNT / 10;
        buf[4] = '0' + TRACK_COUNT % 10;
        buf[5] = '\0';
        tw = Font_Width(pSmall, buf);
        Font_Draw(pSmall, buf, cx - tw * 0.5f, dh * 0.36f, 0xFF888888u);
    }

    /* Track name */
    {
        tw = Font_Width(pBig, k_trackNames[s_nTrack]);
        Font_Draw(pBig, k_trackNames[s_nTrack], cx - tw * 0.5f, dh * 0.43f,
            s_bPlaying ? 0xFFFFCC00u : 0xFF888888u);
    }

    /* Status */
    {
        const char* status = s_bPlaying ? "NOW PLAYING" : "STOPPED";
        tw = Font_Width(pSmall, status);
        Font_Draw(pSmall, status, cx - tw * 0.5f, dh * 0.55f,
            s_bPlaying ? 0xFF88FF88u : 0xFF555555u);
    }

    /* Progress bar */
    if (s_bPlaying)
    {
        DWORD elapsed = (GetTickCount() - s_dwStart) / 1000u;
        int   dur = k_trackDurSec[s_nTrack];
        float ratio = dur > 0 ? (float)elapsed / (float)dur : 0.f;
        float bx = cx - dw * 0.30f;
        float bw = dw * 0.60f;
        float by = dh * 0.63f;
        float bh = 8.f;
        char  tbuf[12];
        int   em = (int)(elapsed / 60), es = (int)(elapsed % 60);

        if (ratio > 1.f) ratio = 1.f;

        /* Bar background */
        {
            typedef struct { float x, y, z, rhw; DWORD c; }BV;
            BV v[4]; DWORD bg = 0xFF222222u, fg = 0xFF66CCFFu;
            v[0].x = bx;    v[0].y = by;    v[0].z = 0; v[0].rhw = 1; v[0].c = bg;
            v[1].x = bx + bw; v[1].y = by;    v[1].z = 0; v[1].rhw = 1; v[1].c = bg;
            v[2].x = bx;    v[2].y = by + bh; v[2].z = 0; v[2].rhw = 1; v[2].c = bg;
            v[3].x = bx + bw; v[3].y = by + bh; v[3].z = 0; v[3].rhw = 1; v[3].c = bg;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL);
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            if (ratio > 0.f)
            {
                v[0].c = v[1].c = v[2].c = v[3].c = fg;
                v[1].x = v[3].x = bx + bw * ratio;
                g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            }
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        }

        /* Elapsed time */
        tbuf[0] = (char)('0' + em / 10); tbuf[1] = (char)('0' + em % 10);
        tbuf[2] = ':';
        tbuf[3] = (char)('0' + es / 10); tbuf[4] = (char)('0' + es % 10);
        tbuf[5] = '\0';
        tw = Font_Width(pSmall, tbuf);
        Font_Draw(pSmall, tbuf, cx - tw * 0.5f, by + 12.f, 0xFF888888u);
    }

    /* Controls hint */
    Font_Draw(pSmall, "White/Black = Prev/Next     A = Play/Stop     B = Back",
        dw * 0.04f, dh * 0.90f, 0xFF444444u);
}