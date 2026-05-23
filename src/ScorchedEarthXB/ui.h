#ifndef SCORCHEDXB_UI_H
#define SCORCHEDXB_UI_H

#include <xtl.h>
#include "font.h"

/*---------------------------------------------------------------------------
    ScorchedXB - ui.h
    UI drawing, texture backgrounds, bitmap font.

    Textures loaded at UI_Init from D:\tex\:
        splash.dds -- pre-baked title screen (PRESS START already in image)
        ui.dds     -- chrome background overlay for menus and HUD

    Font sizes (5x7 bitmap Combat Stencil, from font.cpp):
        Large  (48px)  -- titles, headings
        Medium (28px)  -- menus, prompts
        Small  (18px)  -- HUD, debug text

    nScale in UI_DrawText: 1=small 2=medium 3=large
---------------------------------------------------------------------------*/

/* System */
void   UI_Init(void);
void   UI_Shutdown(void);

/* Texture backgrounds */
void   UI_DrawUIBackground(BYTE bAlpha);   /* draws ui.dds fullscreen */

/* Convenience wrappers (nScale: 1=small 2=medium 3=large) */
void   UI_DrawText(const char* pszText, float x, float y,
    int nScale, DWORD color);
float  UI_TextWidth(const char* pszText, int nScale);

/* Direct font access */
void   UI_DrawTextWithFont(Font* pFont, const char* pszText,
    float x, float y, DWORD color);
Font* UI_GetFontLarge(void);
Font* UI_GetFontMedium(void);
Font* UI_GetFontSmall(void);

/* Title screen */
void   UI_TitleDraw(void);
int    UI_TitleUpdate(WORD wPressed);
void   UI_TitleReset(void);

#endif /* SCORCHEDXB_UI_H */