/*---------------------------------------------------------------------------
    ScorchedXB - help.cpp
    Static controls reference screen.  B button returns to menu.
---------------------------------------------------------------------------*/

#include "help.h"
#include "ui.h"
#include "render.h"

void Help_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float  dw = (float)g_dwDisplayW;
    float  dh = (float)g_dwDisplayH;
    float  cx = dw * 0.5f;
    float  col1 = cx - 240.f;   /* button labels  */
    float  col2 = cx + 40.f;    /* descriptions   */
    float  y = dh * 0.18f;
    float  lineH = 32.f;
    float  tw;

    /* Shortened labels so they never clip into col2 */
    static const char* k_rows[][2] =
    {
        { "D-Pad L / R",   "Adjust angle"      },
        { "D-Pad U / D",   "Adjust power"      },
        { "Black",         "Next weapon"        },
        { "White",         "Prev weapon"        },
        { "A",             "Fire"               },
        { "Back",          "Return to menu"     },
        { "",              ""                   },
        { "Left Stick",    "Aim (alternate)"    },
        { "Right Stick",   "Power (alternate)"  },
    };

#define ROW_COUNT  9

    UI_DrawUIBackground(220);

    /* Title */
    tw = Font_Width(pBig, "CONTROLS");
    Font_Draw(pBig, "CONTROLS", cx - tw * 0.5f, dh * 0.05f, 0xFFFFCC00);

    /* Column headers */
    Font_Draw(pSmall, "BUTTON", col1, y, 0xFF888888);
    Font_Draw(pSmall, "ACTION", col2, y, 0xFF888888);
    y += lineH * 1.5f;

    /* Rows */
    {
        int i;
        for (i = 0; i < ROW_COUNT; i++)
        {
            if (k_rows[i][0][0])
            {
                Font_Draw(pSmall, k_rows[i][0], col1, y, 0xFFFFFFFF);
                Font_Draw(pSmall, k_rows[i][1], col2, y, 0xFFFFCC00);
            }
            y += lineH;
        }
    }

    /* Back hint */
    {
        const char* hint = "B  Return to menu";
        tw = Font_Width(pSmall, hint);
        Font_Draw(pSmall, hint, cx - tw * 0.5f, dh * 0.90f, 0xFF666666);
    }
}