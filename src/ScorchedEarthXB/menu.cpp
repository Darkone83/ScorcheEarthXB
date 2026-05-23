/*---------------------------------------------------------------------------
    ScorchedXB - menu.cpp
    Main menu. When a game session is active, RESUME appears as first item.
    Without active session: NEW GAME / OPTIONS / EXIT
    With active session:    RESUME / NEW GAME / OPTIONS / EXIT
---------------------------------------------------------------------------*/

#include "menu.h"
#include "game.h"
#include "ui.h"
#include "render.h"
#include "input.h"

typedef struct { const char* pszLabel; int nAction; } MenuItem;

static const MenuItem k_itemsBase[] =
{
    { "NEW GAME", MENU_ACTION_NEWGAME },
    { "OPTIONS",  MENU_ACTION_OPTIONS },
    { "HELP",     MENU_ACTION_HELP    },
    { "EXIT",     MENU_ACTION_EXIT    },
};

static const MenuItem k_resume = { "RESUME", MENU_ACTION_RESUME };

#define BASE_COUNT  4

static int s_nSel = 0;
static int s_nCount = 0;
static int s_bHasResume = 0;

void Menu_Init(void)
{
    s_bHasResume = Game_IsActive();
    s_nCount = BASE_COUNT + (s_bHasResume ? 1 : 0);
    s_nSel = 0;
}

static int ItemAction(int i)
{
    if (s_bHasResume)
    {
        if (i == 0) return k_resume.nAction;
        return k_itemsBase[i - 1].nAction;
    }
    return k_itemsBase[i].nAction;
}

static const char* ItemLabel(int i)
{
    if (s_bHasResume)
    {
        if (i == 0) return k_resume.pszLabel;
        return k_itemsBase[i - 1].pszLabel;
    }
    return k_itemsBase[i].pszLabel;
}

int Menu_Update(WORD wPressed)
{
    if (wPressed & BTN_DPAD_UP)
        s_nSel = (s_nSel - 1 + s_nCount) % s_nCount;

    if (wPressed & BTN_DPAD_DOWN)
        s_nSel = (s_nSel + 1) % s_nCount;

    if (wPressed & BTN_A)
        return ItemAction(s_nSel);

    if (wPressed & BTN_BACK)
        return MENU_ACTION_BACK;

    return MENU_ACTION_NONE;
}

void Menu_Draw(void)
{
    float dw = (float)g_dwDisplayW;
    float dh = (float)g_dwDisplayH;
    Font* pFont = UI_GetFontMedium();
    float fItemH = Font_Height(pFont);
    float fSpacing = fItemH * 2.0f;
    float fBlockH = (float)(s_nCount - 1) * fSpacing + fItemH;
    float fStartY = (dh - fBlockH) * 0.55f;
    float fCursorW = Font_Width(pFont, "> ");
    int   i;

    UI_DrawUIBackground(0xFF);

    for (i = 0; i < s_nCount; i++)
    {
        const char* pszLabel = ItemLabel(i);
        float fY = fStartY + (float)i * fSpacing;
        float fLabelW = Font_Width(pFont, pszLabel);
        float fTotalW = fCursorW + fLabelW;
        float fX = (dw - fTotalW) * 0.5f;
        DWORD dwCol;

        /* RESUME item gets a distinct green tint to stand out */
        if (s_bHasResume && i == 0)
            dwCol = (i == s_nSel) ? 0xFF00FF88u : 0xFF007744u;
        else
            dwCol = (i == s_nSel) ? 0xFFFF8C00u : 0xFF888888u;

        if (i == s_nSel)
            Font_Draw(pFont, ">", fX, fY, 0xFFFFFFFF);

        Font_Draw(pFont, pszLabel, fX + fCursorW, fY, dwCol);
    }
}