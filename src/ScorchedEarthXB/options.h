#ifndef SCORCHEDXB_OPTIONS_H
#define SCORCHEDXB_OPTIONS_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - options.h
    In-game options screen.  Three settings:
        Music Volume   0-10  (D-pad L/R)
        SFX Volume     0-10  (D-pad L/R)
        Rumble         ON/OFF (D-pad L/R)

    Navigation: D-pad Up/Down.
    Confirm/back: B button saves and returns OPTS_ACTION_BACK.

    Updates g_cfg and calls Config_Apply + Config_Save on exit.
---------------------------------------------------------------------------*/

#define OPTS_ACTION_NONE  0
#define OPTS_ACTION_BACK  1   /* B pressed -- settings saved, return to menu */

void Options_Init(void);
int  Options_Update(WORD wPressed);   /* returns OPTS_ACTION_* */
void Options_Draw(void);

#endif /* SCORCHEDXB_OPTIONS_H */