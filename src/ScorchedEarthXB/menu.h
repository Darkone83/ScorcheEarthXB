#ifndef SCORCHEDXB_MENU_H
#define SCORCHEDXB_MENU_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - menu.h
    Main menu: NEW GAME / OPTIONS / EXIT

    Menu_Init    -- call on STATE_MENU enter
    Menu_Update  -- call each frame, returns MENU_ACTION_* on selection
    Menu_Draw    -- call inside BeginFrame/EndFrame

    ui.dds is drawn stretched fullscreen as the background.
---------------------------------------------------------------------------*/

#define MENU_ACTION_NONE     0
#define MENU_ACTION_RESUME   1
#define MENU_ACTION_NEWGAME  2
#define MENU_ACTION_OPTIONS  3
#define MENU_ACTION_HELP     4
#define MENU_ACTION_JUKEBOX  5
#define MENU_ACTION_EXIT     6
#define MENU_ACTION_BACK     7

void Menu_Init(void);
int  Menu_Update(WORD wPressed);   /* returns MENU_ACTION_* */
void Menu_Draw(void);

#endif /* SCORCHEDXB_MENU_H */