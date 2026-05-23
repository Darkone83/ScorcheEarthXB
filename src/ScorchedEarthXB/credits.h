#ifndef SCORCHEDXB_CREDITS_H
#define SCORCHEDXB_CREDITS_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - credits.h
    Credits sequence.  Plays track 6 (credits music).
    Text scrolls up from bottom.
    Any button pressed → returns CRED_ACTION_DONE.
---------------------------------------------------------------------------*/

#define CRED_ACTION_NONE  0
#define CRED_ACTION_DONE  1

void Credits_Init(void);
int  Credits_Update(WORD wPressed);
void Credits_Draw(void);

#endif /* SCORCHEDXB_CREDITS_H */