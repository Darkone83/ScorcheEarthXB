#ifndef SCORCHEDXB_SETUP_H
#define SCORCHEDXB_SETUP_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - setup.h
    Pre-game setup: tank type, tank color, AI count + difficulty.
    Results stored in g_setup, read by Game_Init().
---------------------------------------------------------------------------*/

#define SETUP_MAX_AI  3

typedef struct
{
    int nTankType;          /* 0-4                          */
    int nColorIdx;          /* 0-7 index into k_playerColors */
    int nAICount;           /* 1-3                          */
    int nAIDiff[SETUP_MAX_AI];  /* 0=Shooter 1=Cyborg 2=Killer */
} SetupConfig;

extern SetupConfig g_setup;

#define SETUP_ACTION_NONE  0
#define SETUP_ACTION_DONE  1   /* A pressed -- proceed to store */

void Setup_Init(void);
int  Setup_Update(WORD wPressed);
void Setup_Draw(void);

#endif