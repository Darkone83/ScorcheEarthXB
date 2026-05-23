#ifndef SCORCHEDXB_RESULTS_H
#define SCORCHEDXB_RESULTS_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - results.h
    Round/game results screen.
---------------------------------------------------------------------------*/

#define RESULTS_ACTION_NONE      0
#define RESULTS_ACTION_CONTINUE  1   /* next round */
#define RESULTS_ACTION_MENU      2   /* game over → menu */

void Results_Init(void);
int  Results_Update(WORD wPressed);
void Results_Draw(void);

#endif