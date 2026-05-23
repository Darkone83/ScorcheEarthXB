#ifndef SCORCHEDXB_RANDOM_H
#define SCORCHEDXB_RANDOM_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - random.h
    LCG matching scorch.js Random class exactly:
        seed = (1664525 * seed + 1013904223) & 0xFFFFFFFF
        float = seed / 2^32
        int(n) = floor(float * n)

    Two usage styles:

    Instance (preferred for reproducible sequences -- terrain, AI, etc.):
        Rand r;
        Rand_Init( &r, GetTickCount() );
        int x = Rand_Int( &r, WIDTH );
        float f = Rand_Float( &r );

    Global (convenience for one-off calls):
        Rand_SeedGlobal( GetTickCount() );
        int x = Rand_IntG( WIDTH );
---------------------------------------------------------------------------*/

typedef struct { DWORD seed; } Rand;

/* ── Instance API ── */
void  Rand_Init(Rand* pR, DWORD dwSeed);
DWORD Rand_Next(Rand* pR);              /* raw 32-bit value              */
int   Rand_Int(Rand* pR, int n);       /* [0, n)                        */
float Rand_Float(Rand* pR);              /* [0.0f, 1.0f)                  */

/* ── Global convenience ── */
void  Rand_SeedGlobal(DWORD dwSeed);
int   Rand_IntG(int n);
float Rand_FloatG(void);

#endif /* SCORCHEDXB_RANDOM_H */