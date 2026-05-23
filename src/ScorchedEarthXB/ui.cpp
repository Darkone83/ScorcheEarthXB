/*---------------------------------------------------------------------------
    ScorchedXB - ui.cpp
    UI drawing, title screen, and texture-backed backgrounds.

    Textures (from D:\tex\):
        splash.dds -- pre-baked title screen with PRESS START already on it.
                      Drawn fullscreen in UI_TitleDraw; no font needed over it.
        ui.dds     -- UI chrome / background overlay for menus and HUD.
                      Drawn fullscreen before any font overlays.

    Font sizes:
        s_pFontLarge  48px  titles
        s_pFontMedium 28px  menus / prompts
        s_pFontSmall  18px  HUD / debug
---------------------------------------------------------------------------*/

#include "ui.h"
#include "font.h"
#include "tex.h"
#include "render.h"
#include "input.h"

/* -------------------------------------------------------------------------
   Module state
------------------------------------------------------------------------- */

static Font* s_pFontLarge = NULL;
static Font* s_pFontMedium = NULL;
static Font* s_pFontSmall = NULL;

static IDirect3DTexture8* s_pTexSplash = NULL;   /* D:\tex\splash.dds */
static IDirect3DTexture8* s_pTexUI = NULL;   /* D:\tex\ui.dds     */

static DWORD               s_dwBlinkBase = 0;
static BOOL                s_bBlinkOn = TRUE;

/* -------------------------------------------------------------------------
   UI_Init
------------------------------------------------------------------------- */

void UI_Init(void)
{
    Font_SetSD(g_dwDisplayH == 480 && g_dwDisplayW == 640);

    s_pFontLarge = Font_Load(NULL, 48);
    s_pFontMedium = Font_Load(NULL, 28);
    s_pFontSmall = Font_Load(NULL, 18);

    s_pTexSplash = Tex_Load("D:\\tex\\splash.dds");
    s_pTexUI = Tex_Load("D:\\tex\\ui.dds");

    s_dwBlinkBase = GetTickCount();
    s_bBlinkOn = TRUE;
}

/* -------------------------------------------------------------------------
   UI_Shutdown
------------------------------------------------------------------------- */

void UI_Shutdown(void)
{
    Font_Free(s_pFontLarge); s_pFontLarge = NULL;
    Font_Free(s_pFontMedium); s_pFontMedium = NULL;
    Font_Free(s_pFontSmall); s_pFontSmall = NULL;

    Tex_Free(s_pTexSplash); s_pTexSplash = NULL;
    Tex_Free(s_pTexUI); s_pTexUI = NULL;
}

/* -------------------------------------------------------------------------
   Text helpers
------------------------------------------------------------------------- */

void UI_DrawText(const char* pszText, float x, float y,
    int nScale, DWORD color)
{
    Font* pFont = (nScale >= 3) ? s_pFontLarge
        : (nScale >= 2) ? s_pFontMedium
        : s_pFontSmall;
    Font_Draw(pFont, pszText, x, y, color);
}

float UI_TextWidth(const char* pszText, int nScale)
{
    Font* pFont = (nScale >= 3) ? s_pFontLarge
        : (nScale >= 2) ? s_pFontMedium
        : s_pFontSmall;
    return Font_Width(pFont, pszText);
}

void UI_DrawTextWithFont(Font* pFont, const char* pszText,
    float x, float y, DWORD color)
{
    Font_Draw(pFont, pszText, x, y, color);
}

Font* UI_GetFontLarge(void) { return s_pFontLarge; }
Font* UI_GetFontMedium(void) { return s_pFontMedium; }
Font* UI_GetFontSmall(void) { return s_pFontSmall; }

/* -------------------------------------------------------------------------
   UI_DrawUIBackground
   Draws ui.dds fullscreen at the requested alpha.
   Call at the start of any draw function that wants the chrome background.
------------------------------------------------------------------------- */

void UI_DrawUIBackground(BYTE bAlpha)
{
    DWORD dwColor = D3DCOLOR_ARGB(bAlpha, 0xFF, 0xFF, 0xFF);
    Tex_DrawFullscreen(s_pTexUI, dwColor);
}

/* -------------------------------------------------------------------------
   Title screen
   splash.dds already contains the full title art and PRESS START text.
   We draw it fullscreen, then overlay the Team Resurgent / Darkone83
   branding at the bottom so it stays consistent with our font.
------------------------------------------------------------------------- */

void UI_TitleReset(void)
{
    s_dwBlinkBase = GetTickCount();
    s_bBlinkOn = TRUE;
}

int UI_TitleUpdate(WORD wPressed)
{
    DWORD dwDiff = GetTickCount() - s_dwBlinkBase;
    s_bBlinkOn = ((dwDiff / 500) & 1) ? FALSE : TRUE;
    return (wPressed & BTN_START) ? 1 : 0;
}

void UI_TitleDraw(void)
{
    /* splash.dds is fully pre-baked -- title, PRESS START, all art.
       Nothing to overlay.                                           */
    Tex_DrawFullscreen(s_pTexSplash, 0xFFFFFFFF);
}