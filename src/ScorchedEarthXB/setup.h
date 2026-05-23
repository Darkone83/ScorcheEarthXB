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
    /* Player */
    int nTankType;
    int nColorIdx;
    /* AI */
    int nAICount;
    int nAIDiff[SETUP_MAX_AI];
    /* Match options */
    int nRounds;        /* 0=1  1=3  2=5  3=10        */
    int nStartCash;     /* 0=10k 1=25k 2=50k 3=100k   */
    int nWindStrength;  /* 0=None 1=Light 2=Normal 3=Strong */
    int nTerrainType;   /* 0=Random 1=Flat 2=Hills 3=Mountains */
} SetupConfig;

extern SetupConfig g_setup;

#define SETUP_ACTION_NONE  0
#define SETUP_ACTION_DONE  1   /* A pressed -- proceed to store */

void Setup_Init(void);
int  Setup_Update(WORD wPressed);
void Setup_Draw(void);

#endif