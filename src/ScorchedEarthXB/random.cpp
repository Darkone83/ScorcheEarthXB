/*---------------------------------------------------------------------------
    ScorchedXB - random.cpp
    LCG matching scorch.js Random class.

    terrain.cpp has its own inline copy of this seeded at NewRound time so
    terrain generation is independent of the game RNG.  Everything else
    (AI, particle spread, weapon offsets) uses Rand instances from here.
---------------------------------------------------------------------------*/

#include "random.h"

/* =========================================================================
   Core step -- inlined into every caller for speed
========================================================================= */

static DWORD Step(DWORD seed)
{
    return (DWORD)(1664525UL * seed + 1013904223UL);
}

/* =========================================================================
   Instance API
========================================================================= */

void Rand_Init(Rand* pR, DWORD dwSeed)
{
    pR->seed = dwSeed;
}

DWORD Rand_Next(Rand* pR)
{
    pR->seed = Step(pR->seed);
    return pR->seed;
}

int Rand_Int(Rand* pR, int n)
{
    DWORD raw;
    if (n <= 1) return 0;
    raw = Rand_Next(pR);
    return (int)((unsigned __int64)raw * (unsigned __int64)n >> 32);
}

float Rand_Float(Rand* pR)
{
    return (float)Rand_Next(pR) / 4294967296.0f;
}

/* =========================================================================
   Global convenience
========================================================================= */

static Rand s_global;

void Rand_SeedGlobal(DWORD dwSeed)
{
    Rand_Init(&s_global, dwSeed);
}

int Rand_IntG(int n)
{
    return Rand_Int(&s_global, n);
}

float Rand_FloatG(void)
{
    return Rand_Float(&s_global);
}