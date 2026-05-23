/*---------------------------------------------------------------------------
    ScorchedXB - help.cpp
    4-page help system:
      Page 1 - Controls
      Page 2 - Gameplay
      Page 3 - Weapons  (scrollable)
      Page 4 - Tanks    (with live previews)
    L/R D-pad = flip pages   Up/Down = scroll weapons list   B = back
---------------------------------------------------------------------------*/

#include "help.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "input.h"
#include "tanks.h"
#include "player.h"
#include "weapons.h"

#define PAGE_COUNT  4

static int s_nPage = 0;
static int s_nWepScroll = 0;
#define WEP_VISIBLE  8

/* =========================================================================
   Weapon descriptions
========================================================================= */


/* =========================================================================
   Tank descriptions
========================================================================= */

static const char* k_tankNames[5] = { "Recon", "Medium", "Assault", "Artillery", "Siege" };
static const char* k_tankDesc[5] =
{
    "Light and nimble. Tall narrow turret.",
    "The balanced choice. Good all-rounder.",
    "Wide heavy hull. Built to take hits.",
    "Narrow body, long barrel. Maximum range.",
    "Maximum armour. Wide profile. Hard to kill.",
};

/* =========================================================================
   Helpers
========================================================================= */

static void DrawTitle(Font* pBig, float dw, float dh, const char* title)
{
    float tw = Font_Width(pBig, title);
    Font_Draw(pBig, title, (dw - tw) * 0.5f, dh * 0.05f, 0xFFFFCC00u);
}

static void DrawPageIndicator(Font* pSmall, float dw, float dh)
{
    /* "< 1 / 4 >" */
    static const char* k_pages[PAGE_COUNT] = {
        "Controls", "Gameplay", "Weapons", "Tanks"
    };
    char buf[32];
    int  j = 0;
    const char* name = k_pages[s_nPage];
    float tw;

    buf[j++] = '<'; buf[j++] = ' ';
    buf[j++] = (char)('1' + s_nPage); buf[j++] = '/'; buf[j++] = '4';
    buf[j++] = ' '; buf[j++] = '-'; buf[j++] = ' ';
    while (*name && j < 28) buf[j++] = *name++;
    buf[j++] = ' '; buf[j++] = '>'; buf[j] = '\0';

    tw = Font_Width(pSmall, buf);
    Font_Draw(pSmall, buf, (dw - tw) * 0.5f, dh * 0.91f, 0xFF555555u);

    {
        const char* hint = "L/R change page   B back";
        tw = Font_Width(pSmall, hint);
        Font_Draw(pSmall, hint, (dw - tw) * 0.5f, dh * 0.96f, 0xFF444444u);
    }
}

/* =========================================================================
   Page 1 - Controls
========================================================================= */

static void DrawControls(Font* pBig, Font* pSmall, float dw, float dh)
{
    float cx = dw * 0.5f;
    float col1 = cx - 240.f;
    float col2 = cx + 40.f;
    float y = dh * 0.18f;
    float lineH = 32.f;

    static const char* k_menu[][2] =
    {
        { "D-Pad",   "Navigate"       },
        { "A",       "Select"         },
        { "B",       "Back"           },
    };
    static const char* k_game[][2] =
    {
        { "D-Pad L / R",  "Adjust angle"     },
        { "D-Pad U / D",  "Adjust power"     },
        { "Black",        "Next weapon"      },
        { "White",        "Prev weapon"      },
        { "A",            "Fire"             },
        { "Back",         "Return to menu"   },
    };

    DrawTitle(pBig, dw, dh, "CONTROLS");

    Font_Draw(pSmall, "MENUS", col1, y, 0xFF888888u);
    y += lineH;
    { int i; for (i = 0; i < 3; i++, y += lineH) { Font_Draw(pSmall, k_menu[i][0], col1, y, 0xFFFFFFFFu); Font_Draw(pSmall, k_menu[i][1], col2, y, 0xFFFFCC00u); } }

    y += lineH * 0.5f;
    Font_Draw(pSmall, "IN-GAME", col1, y, 0xFF888888u);
    y += lineH;
    { int i; for (i = 0; i < 6; i++, y += lineH) { Font_Draw(pSmall, k_game[i][0], col1, y, 0xFFFFFFFFu); Font_Draw(pSmall, k_game[i][1], col2, y, 0xFFFFCC00u); } }
}

/* =========================================================================
   Page 2 - Gameplay
========================================================================= */

static void DrawGameplay(Font* pBig, Font* pSmall, float dw, float dh)
{
    float cx = dw * 0.5f;
    float y = dh * 0.17f;
    float lineH = 34.f;
    float tw;

    static const char* k_tips[] =
    {
        "Players take turns firing at each other.",
        "Set your angle and power, then fire.",
        "Wind pushes your shot left or right.",
        "Direct hits deal full damage.",
        "Near misses deal splash damage.",
        "Kill enemies to earn cash.",
        "Spend cash in the weapon store",
        "between rounds to restock.",
        "Shields absorb damage for one round.",
        "Parachutes prevent fall damage.",
        "Last tank standing wins the round.",
    };

    DrawTitle(pBig, dw, dh, "GAMEPLAY");

    {
        int i;
        int count = (int)(sizeof(k_tips) / sizeof(k_tips[0]));
        for (i = 0; i < count; i++, y += lineH)
        {
            DWORD col = (i % 2 == 0) ? 0xFFFFFFFFu : 0xFFCCCCCCu;
            tw = Font_Width(pSmall, k_tips[i]);
            Font_Draw(pSmall, k_tips[i], cx - tw * 0.5f, y, col);
        }
    }
}

/* =========================================================================
   Page 3 - Weapons
========================================================================= */

static void DrawWeapons(Font* pBig, Font* pSmall, float dw, float dh)
{
    float listY = dh * 0.18f;
    float rowH = (dh * 0.66f) / (float)WEP_VISIBLE;
    int   i;

    DrawTitle(pBig, dw, dh, "WEAPONS");

    /* Column headers */
    Font_Draw(pSmall, "WEAPON", dw * 0.04f, dh * 0.13f, 0xFF888888u);
    Font_Draw(pSmall, "DESCRIPTION", dw * 0.36f, dh * 0.13f, 0xFF888888u);

    for (i = 0; i < WEP_VISIBLE; i++)
    {
        int   wid = i + s_nWepScroll;
        float iy = listY + (float)i * rowH;
        DWORD nc, dc;

        if (wid >= WEAPON_COUNT) break;

        nc = (wid < 17) ? 0xFFFFCC00u : 0xFF88FF88u;  /* green for shield/chute */
        dc = 0xFFCCCCCCu;

        Font_Draw(pSmall, k_weapons[wid].pszName, dw * 0.04f, iy, nc);
        Font_Draw(pSmall, k_weaponDesc[wid], dw * 0.36f, iy, dc);
    }

    /* Scroll indicator */
    if (WEAPON_COUNT > WEP_VISIBLE)
    {
        char buf[16];
        int  j = 0, start = s_nWepScroll + 1, end = s_nWepScroll + WEP_VISIBLE, total = WEAPON_COUNT;
        float tw;
        if (end > total) end = total;
        if (start >= 10) buf[j++] = (char)('0' + start / 10); buf[j++] = (char)('0' + start % 10);
        buf[j++] = '-';
        if (end >= 10) buf[j++] = (char)('0' + end / 10); buf[j++] = (char)('0' + end % 10);
        buf[j++] = '/';
        if (total >= 10) buf[j++] = (char)('0' + total / 10); buf[j++] = (char)('0' + total % 10);
        buf[j] = '\0';
        tw = Font_Width(pSmall, buf);
        Font_Draw(pSmall, buf, (dw - tw) * 0.5f, dh * 0.86f, 0xFF555555u);
        Font_Draw(pSmall, "U/D to scroll", dw * 0.04f, dh * 0.86f, 0xFF444444u);
    }
}

/* =========================================================================
   Page 4 - Tanks
========================================================================= */

static void DrawTanks(Font* pBig, Font* pSmall, float dw, float dh)
{
    float rowH = (dh * 0.72f) / 5.f;
    float startY = dh * 0.18f;
    float preX = dw * 0.12f;   /* tank preview centre X */
    float nameX = dw * 0.24f;   /* name + desc start X   */
    int   i;

    DrawTitle(pBig, dw, dh, "TANK TYPES");

    for (i = 0; i < 5; i++)
    {
        float ry = startY + (float)i * rowH;
        float cy = ry + rowH * 0.5f;

        /* Coloured row tint for selected */
        {
            typedef struct { float x, y, z, rhw; DWORD c; }BV;
            BV v[4]; DWORD bg = 0xFF181818u;
            v[0].x = dw * 0.02f; v[0].y = ry;       v[0].z = 0; v[0].rhw = 1; v[0].c = bg;
            v[1].x = dw * 0.98f; v[1].y = ry;       v[1].z = 0; v[1].rhw = 1; v[1].c = bg;
            v[2].x = dw * 0.02f; v[2].y = ry + rowH - 3; v[2].z = 0; v[2].rhw = 1; v[2].c = bg;
            v[3].x = dw * 0.98f; v[3].y = ry + rowH - 3; v[3].z = 0; v[3].rhw = 1; v[3].c = bg;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL);
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        }

        /* Tank preview */
        Tanks_DrawPreview(i, k_playerColors[i], preX, cy, 1.2f);

        /* Name */
        Font_Draw(pBig, k_tankNames[i], nameX, ry + rowH * 0.12f,
            0xFFFFCC00u);

        /* Description */
        Font_Draw(pSmall, k_tankDesc[i], nameX, ry + rowH * 0.55f,
            0xFF999999u);
    }
}

/* =========================================================================
   Public API
========================================================================= */

void Help_Init(void)
{
    s_nPage = 0;
    s_nWepScroll = 0;
    Tanks_Init();
}

int Help_Update(WORD wPressed)
{
    if (wPressed & BTN_DPAD_LEFT)
    {
        s_nPage--;
        if (s_nPage < 0) s_nPage = PAGE_COUNT - 1;
        s_nWepScroll = 0;
    }
    if (wPressed & BTN_DPAD_RIGHT)
    {
        s_nPage++;
        if (s_nPage >= PAGE_COUNT) s_nPage = 0;
        s_nWepScroll = 0;
    }

    /* Weapon scroll */
    if (s_nPage == 2)
    {
        if (wPressed & BTN_DPAD_UP)
        {
            s_nWepScroll--;
            if (s_nWepScroll < 0) s_nWepScroll = 0;
        }
        if (wPressed & BTN_DPAD_DOWN)
        {
            s_nWepScroll++;
            if (s_nWepScroll > WEAPON_COUNT - WEP_VISIBLE)
                s_nWepScroll = WEAPON_COUNT - WEP_VISIBLE;
        }
    }

    if (wPressed & BTN_B) return 1;
    return 0;
}

void Help_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;

    UI_DrawUIBackground(220);

    switch (s_nPage)
    {
    case 0: DrawControls(pBig, pSmall, dw, dh); break;
    case 1: DrawGameplay(pBig, pSmall, dw, dh); break;
    case 2: DrawWeapons(pBig, pSmall, dw, dh); break;
    case 3: DrawTanks(pBig, pSmall, dw, dh); break;
    }

    DrawPageIndicator(pSmall, dw, dh);
}