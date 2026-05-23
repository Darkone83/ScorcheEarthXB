/*---------------------------------------------------------------------------
    ScorchedXB - credits.cpp
    Scrolling credits with nebula background and starfield.
    Based on RXDK Credits.cpp visual style.
    Track 6 plays throughout.  Any button exits.
---------------------------------------------------------------------------*/

#include "credits.h"
#include "audio.h"
#include "font.h"
#include "ui.h"
#include "render.h"

static int Ftoi(float f)
{
    int i;
    __asm
    {
        fld   f
        fistp i
    }
    return i;
}

/* =========================================================================
   Credit lines
========================================================================= */

typedef enum { LT_BLANK = 0, LT_TITLE, LT_LABEL, LT_NAME } LineType;
typedef struct { const char* text; LineType type; DWORD color; } CreditLine;

static const CreditLine k_lines[] =
{
    { "ScorchedXB",             LT_TITLE, 0xFFFFCC00u },
    { "",                       LT_BLANK, 0           },
    { "Xbox Port of",           LT_LABEL, 0xFFCCBB99u },
    { "Scorched Earth 2000",    LT_NAME,  0xFFFFAA44u },
    { "",                       LT_BLANK, 0           },
    { "",                       LT_BLANK, 0           },
    { "Developed By",           LT_LABEL, 0xFFCCBB99u },
    { "Darkone83",              LT_NAME,  0xFFCC66FFu },
    { "",                       LT_BLANK, 0           },
    { "",                       LT_BLANK, 0           },
    { "Presented By",           LT_LABEL, 0xFFCCBB99u },
    { "Team Resurgent",         LT_NAME,  0xFFFF00CCu },
    { "",                       LT_BLANK, 0           },
    { "",                       LT_BLANK, 0           },
    { "Original Game",          LT_LABEL, 0xFFCCBB99u },
    { "scorch.js v1.2",         LT_NAME,  0xFF88DDFFu },
    { "",                       LT_BLANK, 0           },
    { "",                       LT_BLANK, 0           },
    { "",                       LT_BLANK, 0           },
    { "",                       LT_BLANK, 0           },
};

#define LINE_COUNT  ( (int)(sizeof(k_lines)/sizeof(k_lines[0])) )
#define SCROLL_SPEED  38.0f

/* =========================================================================
   Starfield
========================================================================= */

#define STAR_COUNT 180

typedef struct { float x, y, z; BYTE brightness; BYTE colorType; } Star;
static Star  s_stars[STAR_COUNT];
static int   s_starsInit = 0;
static DWORD s_starSeed = 0x1234ABCD;

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
    for (i = 0; i < STAR_COUNT; i++)
    {
        DWORD r = SRand();
        s_stars[i].z = (float)(r & 1023u) * (1.f / 1023.f);
        r = SRand();
        s_stars[i].x = (float)(r % (DWORD)g_dwDisplayW);
        r = SRand();
        s_stars[i].y = (float)(r % (DWORD)g_dwDisplayH);
        s_stars[i].brightness = (BYTE)(80u + (DWORD)(s_stars[i].z * 175.f));
        r = SRand();
        s_stars[i].colorType = (BYTE)(r & 7u);
    }
    s_starsInit = 1;
}

static void UpdateStars(void)
{
    int i;
    float dw = (float)g_dwDisplayW;
    float dh = (float)g_dwDisplayH;
    for (i = 0; i < STAR_COUNT; i++)
    {
        s_stars[i].y -= 0.15f + s_stars[i].z * 0.35f;
        if (s_stars[i].y < -4.f) s_stars[i].y += dh + 8.f;
        (void)dw;
    }
}

static DWORD StarColor(BYTE t, BYTE b)
{
    switch (t)
    {
    case 0: return D3DCOLOR_ARGB(b, b, b, 255);
    case 1: return D3DCOLOR_ARGB(b, (BYTE)(b >> 1), b, 255);
    case 2: return D3DCOLOR_ARGB(b, 255, (BYTE)(b >> 1), 255);
    case 3: return D3DCOLOR_ARGB(b, 255, 255, (BYTE)(b >> 1));
    case 4: return D3DCOLOR_ARGB(b, 255, (BYTE)((b >> 1) + 80), (BYTE)(b >> 2));
    case 5: return D3DCOLOR_ARGB(b, 200, 100, 255);
    case 6: return D3DCOLOR_ARGB(b, (BYTE)(b >> 1), 255, (BYTE)(b >> 1));
    default:return D3DCOLOR_ARGB(b, 255, 255, 255);
    }
}

static void DrawStars(void)
{
    typedef struct { float x, y, z, rhw; DWORD c; } SV;
    int i;
    SV  q[4];
    float sz;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    for (i = 0; i < STAR_COUNT; i++)
    {
        DWORD c = StarColor(s_stars[i].colorType, s_stars[i].brightness);
        sz = 1.f + s_stars[i].z * 1.5f;
        q[0].x = s_stars[i].x;    q[0].y = s_stars[i].y;    q[0].z = 0; q[0].rhw = 1; q[0].c = c;
        q[1].x = s_stars[i].x + sz; q[1].y = s_stars[i].y;    q[1].z = 0; q[1].rhw = 1; q[1].c = c;
        q[2].x = s_stars[i].x;    q[2].y = s_stars[i].y + sz; q[2].z = 0; q[2].rhw = 1; q[2].c = c;
        q[3].x = s_stars[i].x + sz; q[3].y = s_stars[i].y + sz; q[3].z = 0; q[3].rhw = 1; q[3].c = c;
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(SV));
    }
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   Nebula background
========================================================================= */

static void DrawNebula(void)
{
    typedef struct { float x, y, z, rhw; DWORD c; } GV;
    float dw = (float)g_dwDisplayW, dh = (float)g_dwDisplayH;
    GV v[6];
    DWORD cT = D3DCOLOR_XRGB(18, 8, 38), cM = D3DCOLOR_XRGB(5, 5, 22), cB = D3DCOLOR_XRGB(2, 2, 8);
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
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 4, v, sizeof(GV));
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   State
========================================================================= */

static float s_fScrollY = 0.f;
static DWORD s_dwLast = 0;

/* =========================================================================
   Public API
========================================================================= */

void Credits_Init(void)
{
    s_fScrollY = 0.f;
    s_dwLast = GetTickCount();
    InitStars();
    Audio_MusicPlayCredits();
}

int Credits_Update(WORD wPressed)
{
    DWORD now = GetTickCount();
    float fDt = (float)(now - s_dwLast) / 1000.f;
    if (fDt > 0.1f) fDt = 0.1f;
    s_dwLast = now;

    UpdateStars();
    s_fScrollY += SCROLL_SPEED * fDt;

    if (wPressed) return CRED_ACTION_DONE;
    return CRED_ACTION_NONE;
}

void Credits_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;
    float  cx = dw * 0.5f;
    float  y = dh - s_fScrollY;
    int    i;

    DrawNebula();
    DrawStars();

    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);

    for (i = 0; i < LINE_COUNT; i++)
    {
        const CreditLine* L = &k_lines[i];
        float lineH, tw;
        Font* pF;

        switch (L->type)
        {
        case LT_TITLE: lineH = 56.f; pF = pBig;   break;
        case LT_LABEL: lineH = 40.f; pF = pSmall; break;
        case LT_NAME:  lineH = 44.f; pF = pBig;   break;
        default:       lineH = 28.f; pF = pSmall; break;
        }

        if (y > -lineH && y < dh && L->text && L->text[0])
        {
            tw = Font_Width(pF, L->text);
            Font_Draw(pF, L->text, cx - tw * 0.5f, y, L->color);
        }

        y += lineH;
    }

    if (s_fScrollY > 80.f)
    {
        const char* hint = "Press any button to exit";
        float tw = Font_Width(pSmall, hint);
        Font_Draw(pSmall, hint, (dw - tw) * 0.5f, dh - 20.f, 0xFF444444u);
    }

    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}