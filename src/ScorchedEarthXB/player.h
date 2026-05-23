#ifndef SCORCHEDXB_PLAYER_H
#define SCORCHEDXB_PLAYER_H

#include <xtl.h>
#include "random.h"

/*---------------------------------------------------------------------------
    ScorchedXB - player.h
    Player state, tank dimensions, and terrain placement.

    Ported from scorch.js Player class and placeTanks().

    Tank sprite dimensions are hardcoded from the 5 tankData entries.
    Player colours match scorch.js PLAYER_COLORS (8 entries, index = id).

    Key constants from scorch.js:
        START_ANGLE  = 30     (left-side players)
        START_ANGLE  = 150    (right-side players)
        START_POWER  = 300
        MAX_POWER    = 1000
        MIN_POWER    = 200
        INITIAL_CASH = 50000
        WEAPONS[0].ammo = 999 (unlimited missile)
---------------------------------------------------------------------------*/

#define PLAYER_MAX       8
#define PLAYER_WEAPONS   19
#define PLAYER_NAME_MAX  24
#define TANK_TYPE_COUNT  5

/* ── Tank type spec (derived from tankData[type][0] and sprite dims) ── */
typedef struct
{
    int nWidth;      /* sprite[0].length                         */
    int nHeight;     /* sprite.length - 1                        */
    int nPivotX;     /* sprite[0][0] -- turret pivot X offset    */
    int nPivotY;     /* sprite[0][1] -- turret pivot Y offset    */
    int nBarrelLen;  /* sprite[0][2]                             */
} TankSpec;

extern const TankSpec k_tankSpecs[TANK_TYPE_COUNT];
extern const DWORD    k_playerColors[8];

/* ── Player ── */
typedef struct
{
    /* Identity */
    int  nID;
    int  nColorIdx;           /* index into k_playerColors (0-7) */
    char szName[PLAYER_NAME_MAX];
    int  nTankType;
    int  bAI;
    int  nAIType;

    /* Sprite dimensions (copied from k_tankSpecs at init) */
    int  nWidth;
    int  nHeight;
    int  nPivotX;
    int  nPivotY;
    int  nBarrelLen;

    /* Position on terrain (top-left corner of sprite) */
    int  nX;
    int  nY;

    /* Aim */
    int  nAngle;       /* 0-179 */
    int  nPower;       /* MIN_POWER - MAX_POWER */
    int  nPowerLimit;

    /* Round state */
    int  bAlive;
    int  bFalling;
    int  nParachutes;

    /* Shield */
    int   bShield;
    float fShieldStrength;
    float fShieldMaxStrength;
    float fShieldDamage;
    int   nShieldThickness;

    /* Inventory */
    int  nWeapons[PLAYER_WEAPONS];
    int  nActiveWeapon;

    /* Round stats */
    int  nKills;
    int  nEarnedCash;
    int  nCash;

    /* Overall stats */
    int  nOverallKills;
    int  nOverallGain;
} Player;

/* ── API ── */

/* Initialise a new player (call once per game, not per round) */
void Player_Init(Player* pP, int nID, const char* pszName,
    int nTankType, int bAI, int nAIType);

/* Reset mutable state for a new round (alive, power, angle, kills etc.) */
void Player_ResetForRound(Player* pP, int nTotalPlayers);

/* Place all living players on the terrain (randomised slot order).
   Calls Terrain_SetPixelBg to flatten the surface under each tank.     */
void Player_PlaceAll(Player* pPlayers, int nCount, Rand* pRand);

/* Turret tip position (q=1 = full barrel, q=0 = pivot) */
int Player_TurretX(const Player* pP, float q);
int Player_TurretY(const Player* pP, float q);

/* Convenience: pixel colour for this player (from k_playerColors) */
DWORD Player_GetColor(const Player* pP);

#endif /* SCORCHEDXB_PLAYER_H */