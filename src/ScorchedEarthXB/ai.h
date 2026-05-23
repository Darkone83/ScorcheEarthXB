#ifndef SCORCHEDXB_AI_H
#define SCORCHEDXB_AI_H

#include <xtl.h>
#include "player.h"
#include "random.h"
#include "weapons.h"

/*---------------------------------------------------------------------------
    ScorchedXB - ai.h
    AI turn logic ported from scorch.js.

    Three difficulty levels (player.nAIType):
        0  Shooter   coarse aim, prefers cheap weapons, high inaccuracy
        1  Cyborg    medium aim, prefers mid-tier weapons
        2  Killer    precise aim, prefers expensive weapons, target priority

    AI_Think() fills an AIShot struct with the chosen angle, power, and
    weapon index.  game.cpp applies those to the player and calls fire.

    The search is a two-pass grid over angle x power space:
        1. Coarse sweep of the full range
        2. Fine sweep around the best coarse result
    Each candidate is scored using roundDamage for all living players
    (self-damage penalised x8).  Inaccuracy noise is applied last.

    If no scoring shot is found a fallback points directly at the closest
    opponent with estimated power.
---------------------------------------------------------------------------*/

/* Result of an AI turn computation */
typedef struct
{
    int nAngle;      /* 0-179 */
    int nPower;      /* MIN_POWER - MAX_POWER */
    int nWeaponID;   /* index into k_weapons[] */
    int bFallback;   /* 1 = couldn't find a scoring shot */
} AIShot;

/*  Compute the AI's shot for this turn.
    pRand is the game's shared RNG (used for weapon choice noise).
    Output written to pOut.                                              */
void AI_Think(const Player* pAI,
    const Player* pAllPlayers, int nCount,
    float fWind,
    Rand* pRand,
    AIShot* pOut);

#endif /* SCORCHEDXB_AI_H */