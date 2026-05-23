/*---------------------------------------------------------------------------
    ScorchedXB - weapons.cpp
    Weapon table and terrain-carving implementations.
    Ported from scorch.js applyWeaponToBitmap and related functions.

    All functions modify the terrain pixel buffer only.
    Caller must call Terrain_Drop(x0, x1) and Terrain_MarkDirty(x0, x1)
    after any Weapon_Apply call.

    Sand bomb density: 0.2 (20% of pixels in each scan line are drawn),
    matching scorch.js bitmap.setDensity(0.2) behaviour.
---------------------------------------------------------------------------*/

#include <math.h>
#include "weapons.h"
#include "terrain.h"
#include "render.h"    /* g_dwDisplayW, g_dwDisplayH */

/* =========================================================================
   Weapon definition table  (matches scorch.js WEAPONS[] exactly)
========================================================================= */

const char* k_weaponDesc[WEAPON_COUNT] =
{
    "Standard explosive. Reliable and always free.",
    "Small nuclear blast. Good all-round opener.",
    "Large nuclear detonation. Heavy splash damage.",
    "Rains debris upward from impact. Buries targets.",
    "Rolls along terrain toward the target.",
    "Larger roller. Follows terrain contours.",
    "Massive roller. Devastating on flat ground.",
    "Bores into the ground. Digs under cover.",
    "Deeper burrowing charge. Collapses terrain.",
    "Maximum depth burrowing. Cave collapse.",
    "Scatters 6 sub-explosions randomly on impact.",
    "Scatters 10 sub-explosions. Wide area denial.",
    "Wide vertical burn. Strips terrain columns.",
    "Double-width napalm. Massive terrain strip.",
    "Splits into 5 warheads in a spread pattern.",
    "9 warheads spread wide. Covers a huge area.",
    "Instant beam of destruction. Cuts through anything.",
    "Absorbs 75% of incoming damage for one round.",
    "Prevents fall damage when knocked off a ledge.",
};

/*  name               radius  ammo   price   kind          inf  parts  dur   bw   bdmg */
const WeaponDef k_weapons[WEAPON_COUNT] =
{
  { "Missile",           10,   999,       0,  WKIND_SIMPLE,   1,   0,    0,   0,   0   },
  { "Baby Nuke",         60,     0,   20000,  WKIND_SIMPLE,   0,   0,    0,   0,   0   },
  { "Nuke",             100,     0,   40000,  WKIND_SIMPLE,   0,   0,    0,   0,   0   },
  { "Sand Bomb",         60,     0,    5000,  WKIND_SAND,     0,   0,    0,   0,   0   },
  { "Baby Roller",       15,     0,    7000,  WKIND_ROLLER,   0,   0,    0,   0,   0   },
  { "Roller",            30,     0,   13000,  WKIND_ROLLER,   0,   0,    0,   0,   0   },
  { "Heavy Roller",      55,     0,   20000,  WKIND_ROLLER,   0,   0,    0,   0,   0   },
  { "Baby Digger",       24,     0,    2000,  WKIND_DIGGER,   0,   0, 1000,   0,   0   },
  { "Digger",            38,     0,    4000,  WKIND_DIGGER,   0,   0, 3000,   0,   0   },
  { "Heavy Digger",      54,     0,    6000,  WKIND_DIGGER,   0,   0, 5000,   0,   0   },
  { "Funky Bomb",        38,     0,   30000,  WKIND_FUNKY,    0,   6,    0,   0,   0   },
  { "Funky Nuke",        62,     0,   50000,  WKIND_FUNKY,    0,  10,    0,   0,   0   },
  { "Napalm",           140,     0,   10000,  WKIND_NAPALM,   0,   0,    0,   0,   0   },
  { "Hot Napalm",       280,     0,   20000,  WKIND_NAPALM,   0,   0,    0,   0,   0   },
  { "MIRV",              25,     0,   35000,  WKIND_MIRV,     0,   5,    0,   0,   0   },
  { "Death Head",        55,     0,   90000,  WKIND_MIRV,     0,   9,    0,   0,   0   },
  { "Laser",              0,     0,   25000,  WKIND_LASER,    0,   0,    0,   5, 880   },
  { "Shield",             0,     0,   15000,  WKIND_SHIELD,   0,   0,    0,   0,   0   },
  { "Parachute",          0,     0,    3000,  WKIND_CHUTE,    0,   0,    0,   0,   0   },
};

/* =========================================================================
   Simple -- fillCircle with bg  (applyWeaponToBitmap default)
========================================================================= */

static void ApplySimple(int x, int y, int r)
{
    Terrain_CarveCircle(x, y, r);
}

/* =========================================================================
   Napalm -- wide vertical strips  (applyWeaponToBitmap napalm branch)
   for dx in [-radius*2, +radius*2]:
       drop = (1 - abs(dx)/(radius*2)) * radius * 0.65
       drawLine(x+dx, y-drop, x+dx, y+drop)   -- background
========================================================================= */

static void ApplyNapalm(int x, int y, int r)
{
    int dx, drop;
    int r2 = r * 2;

    for (dx = -r2; dx <= r2; dx++)
    {
        float fAbs = dx < 0 ? (float)(-dx) : (float)dx;
        drop = (int)((1.0f - fAbs / (float)r2) * (float)r * 0.65f);
        Terrain_CarveLine(x + dx, y - drop, x + dx, y + drop);
    }
}

/* =========================================================================
   Sand -- sparse upward ground spray (drawSandExplosionToBitmap)
   Writes ground pixels upward from y with 20% density, widening each row.
   size = 250 lines (hardcoded in scorch.js).
========================================================================= */

static void ApplySand(int x, int y, Rand* pRand)
{
    int  yl = y;
    int  currentSize = 3;
    int  lineCount = 0;
    int  px;
    int  half;

    while (yl >= 0 && lineCount < 250)
    {
        half = currentSize / 2;
        for (px = x - half; px <= x + half; px++)
        {
            if (Rand_Float(pRand) < 0.2f)
                Terrain_DrawLineGround(px, yl, px, yl);
        }
        lineCount++;
        yl--;
        currentSize++;
    }
}

/* =========================================================================
   Funky -- 6 random bg circles in the area (applyWeaponToBitmap funky)
========================================================================= */

static void ApplyFunky(int x, int y, int r, Rand* pRand)
{
    int   i, ox, oy, cr;
    float f;

    for (i = 0; i < 6; i++)
    {
        ox = (int)((Rand_Float(pRand) - 0.5f) * (float)r * 2.0f);
        oy = (int)((Rand_Float(pRand) - 0.5f) * (float)r * 2.0f);
        f = 0.35f + Rand_Float(pRand) * 0.35f;
        cr = (int)((float)r * f);
        if (cr < 8) cr = 8;
        Terrain_CarveCircle(x + ox, y + oy, cr);
    }
}

/* =========================================================================
   MIRV -- 5 circles in a shallow V pattern (applyWeaponToBitmap mirv)
   i = -2,-1,0,1,2:
       circle at (x + i*floor(r*0.8), y + abs(i)*floor(r*0.18)), r
========================================================================= */

static void ApplyMirv(int x, int y, int r)
{
    int i, cx, cy;
    int r08 = (int)((float)r * 0.8f);
    int r18 = (int)((float)r * 0.18f);

    for (i = -2; i <= 2; i++)
    {
        int absi = i < 0 ? -i : i;
        cx = x + i * r08;
        cy = y + absi * r18;
        Terrain_CarveCircle(cx, cy, r);
    }
}

/* =========================================================================
   Digger -- 10 random-walk carvers (drawDiggerExplosionToBitmap)
   Each walker takes a random step (right/left/up with ground check, always
   moves down). Carves a 3-pixel cross at current position.
   duration = number of successful steps across all walkers.
========================================================================= */

static void ApplyDigger(int x, int y, int nDuration, Rand* pRand)
{
    int xs[10], ys[10];
    int num = 10;
    int frameNum = 0;
    int i, d, moved;

    for (i = 0; i < num; i++) { xs[i] = x; ys[i] = y; }

    while (frameNum < nDuration)
    {
        for (i = 0; i < num && frameNum < nDuration; i++)
        {
            /* stepDiggerWalker */
            d = Rand_Int(pRand, 4);
            moved = 0;

            if (d == 0 && Terrain_IsGround(xs[i] + 2, ys[i]))
            {
                xs[i]++; moved = 1;
            }
            else if (d == 1 && Terrain_IsGround(xs[i] - 2, ys[i]))
            {
                xs[i]--; moved = 1;
            }
            else if (d == 2 && Terrain_IsGround(xs[i], ys[i] - 2))
            {
                ys[i]--; moved = 1;
            }
            else if (d == 3)
            {
                ys[i]++; moved = 1;
            }

            if (moved) frameNum++;

            /* clearDiggerBrush -- 5-pixel cross of background */
            Terrain_SetPixelBg(xs[i], ys[i]);
            Terrain_SetPixelBg(xs[i] + 1, ys[i]);
            Terrain_SetPixelBg(xs[i] - 1, ys[i]);
            Terrain_SetPixelBg(xs[i], ys[i] + 1);
            Terrain_SetPixelBg(xs[i], ys[i] - 1);
        }
    }
}

/* =========================================================================
   Laser -- Bresenham line of fillCircle calls (carveLaserBeam)
========================================================================= */

void Weapon_ApplyLaser(int x1, int y1, int nAngleDeg, int nBeamWidth)
{
    float  angle = (float)nAngleDeg * 3.14159265f / 180.0f;
    float  range = (float)(g_dwDisplayW > g_dwDisplayH
        ? g_dwDisplayW : g_dwDisplayH) * 1.5f;
    int    x2 = (int)((float)x1 + range * (float)cos(angle));
    int    y2 = (int)((float)y1 - range * (float)sin(angle));
    int    r = nBeamWidth / 2;
    if (r < 1) r = 1;

    /* Bresenham scan -- carve circle at every point on the beam */
    {
        int dx = (x2 > x1 ? x2 - x1 : x1 - x2);
        int sx = x1 < x2 ? 1 : -1;
        int dy = -(y2 > y1 ? y2 - y1 : y1 - y2);
        int sy = y1 < y2 ? 1 : -1;
        int err = dx + dy;
        int e2;
        int bx = x1, by = y1;

        for (;;)
        {
            if (bx >= 0 && bx < (int)g_dwDisplayW &&
                by >= 0 && by < (int)g_dwDisplayH)
                Terrain_CarveCircle(bx, by, r);

            if (bx == x2 && by == y2) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; bx += sx; }
            if (e2 <= dx) { err += dx; by += sy; }
        }
    }

    Terrain_MarkDirty(0, (int)g_dwDisplayW - 1);
}

/* =========================================================================
   Roller settle  (rollAndExplode -- terrain contact, no animation here)
   Drops the roller to terrain, rolls in the direction of initial vx.
   Returns final screen position for the explosion.
========================================================================= */

void Weapon_RollerSettle(int startX, int startY, float fVX,
    int* pFinalX, int* pFinalY)
{
    int x = startX, y = startY;
    int halfW = 2, halfH = 2;
    int direction = 0;
    int ticks, i;
    int prevX = x, prevY = y;
    int canLeft, canRight;

    /* Drop to terrain surface */
    for (ticks = 0; ticks < (int)g_dwDisplayH; ticks++)
    {
        int falling = 0;
        for (i = 0; i < halfW * 2; i++)
        {
            if (Terrain_IsBackground(x - halfW + i, y + halfH))
                falling = 1;
            else
            {
                falling = 0; break;
            }
        }
        if (!falling) break;
        if (y + halfH >= (int)g_dwDisplayH - 1) break;
        y++;
    }

    /* Roll */
    for (ticks = 0; ticks < 520; ticks++)
    {
        int falling = 1;
        for (i = 0; i < halfW * 2 && falling; i++)
        {
            if (!Terrain_IsBackground(x - halfW + i, y + halfH))
                falling = 0;
        }

        if (falling)
        {
            y++;
        }
        else if (direction == 0)
        {
            canRight = Terrain_IsBackground(x + halfW + 1, y);
            canLeft = Terrain_IsBackground(x - halfW - 1, y);
            if (fVX < 0.0f)
                direction = canLeft ? -1 : (canRight ? 1 : 0);
            else
                direction = canRight ? 1 : (canLeft ? -1 : 0);
            x += direction;
        }
        else
        {
            if (Terrain_IsBackground(x + direction * (halfW + 1), y))
                x += direction;
            else
                break;
        }

        /* Stuck detection (same position as 2 steps ago) */
        if (x == prevX && y == prevY) break;
        prevX = x;
        prevY = y;
    }

    *pFinalX = x;
    *pFinalY = y;
}

/* =========================================================================
   Weapon_Apply  -- dispatch by kind
========================================================================= */

void Weapon_Apply(int nWeaponID, int x, int y, Rand* pRand)
{
    const WeaponDef* pW;
    int              x0, x1, r;

    if (nWeaponID < 0 || nWeaponID >= WEAPON_COUNT) return;
    pW = &k_weapons[nWeaponID];
    r = pW->nRadius;

    switch (pW->eKind)
    {
    case WKIND_SIMPLE:
    case WKIND_ROLLER:  /* roller explodes as simple at final position */
        ApplySimple(x, y, r);
        x0 = x - r - 1;  x1 = x + r + 1;
        break;

    case WKIND_NAPALM:
        ApplyNapalm(x, y, r);
        x0 = x - r * 2 - 1;  x1 = x + r * 2 + 1;
        break;

    case WKIND_SAND:
        ApplySand(x, y, pRand);
        x0 = x - 130;  x1 = x + 130;  /* sand sprays ~250px wide */
        break;

    case WKIND_FUNKY:
        ApplyFunky(x, y, r, pRand);
        x0 = x - r * 2 - 1;  x1 = x + r * 2 + 1;
        break;

    case WKIND_MIRV:
    {
        int r08 = (int)((float)r * 0.8f);
        ApplyMirv(x, y, r);
        x0 = x - r08 * 2 - r - 1;
        x1 = x + r08 * 2 + r + 1;
        break;
    }

    case WKIND_DIGGER:
        ApplyDigger(x, y, pW->nDuration, pRand);
        x0 = x - 60;  x1 = x + 60;  /* walkers spread ~60px */
        break;

    case WKIND_LASER:
        /* Laser handled by Weapon_ApplyLaser separately */
        return;

    default:
        return;
    }

    Terrain_MarkDirty(x0, x1);
}