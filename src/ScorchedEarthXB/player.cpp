/*---------------------------------------------------------------------------
    ScorchedXB - player.cpp
    Player state and terrain placement.

    Tank spec table derived from tankData in scorch.js (5 tank types).
    Player colours match scorch.js PLAYER_COLORS.
    PlaceAll ports scorch.js placeTanks() faithfully:
        - Randomised slot assignment so players don't always get the same X
        - Finds first Y where all tank-width columns are ground
        - Clears one pixel above each ground column to flatten the pocket
        - Places tank at (x, y - height) so sprite bottom sits on surface
---------------------------------------------------------------------------*/

#include <math.h>
#include <string.h>
#include "player.h"
#include "terrain.h"
#include "render.h"
#include "weapons.h"    /* g_dwDisplayW, g_dwDisplayH */

/* =========================================================================
   Tables
========================================================================= */

/* Matches tankData[0..4][0] = [pivotX, pivotY, barrelLen, ...] */
/* Types: 0=Recon  1=Medium  2=Assault  3=Artillery  4=Siege    */
const TankSpec k_tankSpecs[TANK_TYPE_COUNT] =
{
    { 11,  7,  8, 3, 8 },   /* 0 Recon     -- small, fast, med barrel  */
    { 15, 10,  7, 5, 9 },   /* 1 Medium    -- balanced standard         */
    { 17, 10,  6, 4, 7 },   /* 2 Assault   -- wide, short barrel        */
    { 12, 11, 13, 4, 7 },   /* 3 Artillery -- narrow, very long barrel  */
    { 20, 10,  4, 3, 9 },   /* 4 Siege     -- widest, stubby barrel     */
};

/* Matches scorch.js PLAYER_COLORS (rgb already includes 0xFF alpha) */
const DWORD k_playerColors[8] =
{
    0xFFFF0000,   /* red       */
    0xFFFFFF00,   /* yellow    */
    0xFFFFFFFF,   /* white     */
    0xFF2B704F,   /* dark green (43, 112, 79) */
    0xFF00FFFF,   /* cyan      */
    0xFF000096,   /* dark blue (0, 0, 150) */
    0xFF009600,   /* green (0, 150, 0) */
    0xFFFF00FF,   /* magenta   */
};

/* =========================================================================
   Player_Init
   One-time setup; does not touch per-round fields.
========================================================================= */

void Player_Init(Player* pP, int nID, const char* pszName,
    int nTankType, int bAI, int nAIType)
{
    const TankSpec* pSpec;
    const char* src;
    char* dst;
    int             i;

    memset(pP, 0, sizeof(Player));

    pP->nID = nID;
    pP->nColorIdx = nID;   /* default: color matches slot, overridden for human */
    pP->nTankType = nTankType < 0 ? 0 : nTankType >= TANK_TYPE_COUNT ? TANK_TYPE_COUNT - 1 : nTankType;
    pP->bAI = bAI;
    pP->nAIType = nAIType;

    /* Copy name */
    src = pszName ? pszName : "Player";
    dst = pP->szName;
    for (i = 0; i < PLAYER_NAME_MAX - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';

    /* Sprite dimensions from spec table, scaled to display resolution.
       Base resolution is 640x480; at 720p (1280x720) tanks are 2x.     */
    {
        float fScale = (float)g_dwDisplayW / 640.f;
        if (fScale < 1.f) fScale = 1.f;
        pSpec = &k_tankSpecs[pP->nTankType];
        pP->nWidth = (int)(pSpec->nWidth * fScale);
        pP->nHeight = (int)(pSpec->nHeight * fScale);
        pP->nPivotX = (int)(pSpec->nPivotX * fScale);
        pP->nPivotY = (int)(pSpec->nPivotY * fScale);
        pP->nBarrelLen = (int)(pSpec->nBarrelLen * fScale);
    }

    /* Starting inventory: weapon 0 (Missile) = 999, rest = 0 */
    pP->nWeapons[0] = 999;
    for (i = 1; i < PLAYER_WEAPONS; i++)
        pP->nWeapons[i] = 0;

    pP->nCash = 50000;   /* INITIAL_CASH from scorch.js */
}

/* =========================================================================
   Player_ResetForRound
   Matches scorch.js newRound() player reset block.
   Angle: left-half players aim right (30), right-half aim left (150).
========================================================================= */

void Player_ResetForRound(Player* pP, int nTotalPlayers)
{
    pP->bAlive = 1;
    pP->nPowerLimit = 1000;   /* MAX_POWER */
    pP->nPower = 300;    /* START_POWER */
    pP->nAngle = pP->nID < nTotalPlayers / 2 ? 30 : 150;
    pP->nKills = 0;
    pP->nEarnedCash = 0;
    pP->bFalling = 0;
    pP->nActiveWeapon = 0;

    /* Activate shield from inventory (consumes one) */
    if (pP->nWeapons[WID_SHIELD] > 0)
    {
        pP->nWeapons[WID_SHIELD]--;
        pP->bShield = 1;
        pP->fShieldStrength = 1.0f;
        pP->fShieldMaxStrength = 1.0f;
        pP->fShieldDamage = 0.75f;
        pP->nShieldThickness = 4;
    }
    else
    {
        pP->bShield = 0;
        pP->fShieldStrength = 0.f;
    }

    /* Copy parachute count from inventory (consumed one at a time on use) */
    pP->nParachutes = pP->nWeapons[WID_PARACHUTE];
}

/* =========================================================================
   Player_PlaceAll
   Ports scorch.js placeTanks() exactly.
   Randomised slot assignment ensures different players land at different Xs.
========================================================================= */

void Player_PlaceAll(Player* pPlayers, int nCount, Rand* pRand)
{
    int  slots[PLAYER_MAX];
    int  nSlots;
    int  i, si, pIdx, x, y, k;
    int  W = (int)g_dwDisplayW;
    int  H = (int)g_dwDisplayH;

    /* Build slot index list: [0, 1, ..., nCount-1] */
    nSlots = nCount < PLAYER_MAX ? nCount : PLAYER_MAX;
    for (i = 0; i < nSlots; i++) slots[i] = i;

    for (i = 0; i < nSlots; i++)
    {
        Player* pP;
        int     count;

        /* Pick random remaining slot */
        si = Rand_Int(pRand, nSlots - i);
        pIdx = slots[si];

        /* Compact: move last entry into vacated slot */
        slots[si] = slots[nSlots - i - 1];

        pP = &pPlayers[pIdx];
        if (!pP->bAlive) continue;

        /* Evenly-spaced X position for slot i (1-indexed fraction) */
        x = W / (nCount + 1) * (i + 1);

        /* Scan top-down for a surface where all tank-width columns are ground */
        for (y = 1; y < H; y++)
        {
            count = 0;
            for (k = 0; k < pP->nWidth; k++)
            {
                if (!Terrain_IsBackground(x + k, y))
                {
                    count++;
                    /* Flatten the pixel above to level the pocket */
                    Terrain_SetPixelBg(x + k, y - 1);
                }
            }
            if (count >= pP->nWidth)
            {
                pP->nX = x;
                pP->nY = y - pP->nHeight;
                break;
            }
        }
    }

    /* Upload any terrain changes made during placement */
    Terrain_MarkDirty(0, W - 1);
}

/* =========================================================================
   Turret helpers  (scorch.js turretX / turretY)
========================================================================= */

int Player_TurretX(const Player* pP, float q)
{
    float rad = (float)pP->nAngle * 3.14159265f / 180.0f;
    return pP->nX + pP->nPivotX
        + (int)((float)pP->nBarrelLen * q * (float)cos(rad));
}

int Player_TurretY(const Player* pP, float q)
{
    float rad = (float)pP->nAngle * 3.14159265f / 180.0f;
    return pP->nY + pP->nPivotY
        - (int)((float)pP->nBarrelLen * q * (float)sin(rad));
}

/* =========================================================================
   Player_GetColor
========================================================================= */

DWORD Player_GetColor(const Player* pP)
{
    int idx = pP->nColorIdx;
    if (idx < 0) idx = 0;
    if (idx > 7) idx = 7;
    return k_playerColors[idx];
}