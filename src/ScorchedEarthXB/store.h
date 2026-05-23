#ifndef SCORCHEDXB_STORE_H
#define SCORCHEDXB_STORE_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - store.h
    Weapon store -- pre-game loadout and between-round restock.
    Operates directly on Game_GetPlayer(0)'s inventory and cash.
---------------------------------------------------------------------------*/

#define STORE_ACTION_NONE  0
#define STORE_ACTION_DONE  1

void Store_Init(void);          /* call each time store is entered */
int  Store_Update(WORD wPressed);
void Store_Draw(void);

#endif