#ifndef SCORCHEDXB_JUKEBOX_H
#define SCORCHEDXB_JUKEBOX_H

#include <xtl.h>

void Jukebox_Init(void);
int  Jukebox_Update(WORD wPressed);  /* returns 1 when B pressed */
void Jukebox_Draw(void);

#endif