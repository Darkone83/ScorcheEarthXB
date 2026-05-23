/*---------------------------------------------------------------------------
    ScorchedXB - physics.cpp
    Projectile simulation and damage ported from scorch.js.

    All equations match scorch.js exactly:
        projectilePoint, intersectShot, isSolidForShot,
        roundDamage, damagePlayers, settleOneTank, applyFallingDamage.

    Wall handling is omitted for now (wall mode = "none" default).
    It can be added to Phys_GetPos / Phys_HasEscaped when needed.
---------------------------------------------------------------------------*/

#include <math.h>
#include "physics.h"
#include "player.h"
#include "terrain.h"
#include "render.h"    /* g_dwDisplayW, g_dwDisplayH */

/* =========================================================================
   Settle base offsets (sprite[0][5] and [0][6] in scorch.js tankData)
   Columns checked for ground support: baseLeft to (width - baseRight).
========================================================================= */

static const int k_baseLeft[TANK_TYPE_COUNT] = { 1, 1, 0, 2, 1 };
static const int k_baseRight[TANK_TYPE_COUNT] = { 1, 1, 0, 2, 1 };

/* =========================================================================
   Phys_InitProjectile
   Matches scorch.js fire():
       startX = turretX(2)
       startY = HEIGHT - turretY(2)      (worldY from bottom)
       angle  = nAngle * PI / 180
       speed  = nPower / 8
       vx     = speed * cos(angle)
       vy     = speed * sin(angle)
========================================================================= */

void Phys_InitProjectile(Projectile* pP, const Player* pShooter)
{
    float angle = (float)pShooter->nAngle * 3.14159265f / 180.0f;
    float speed = (float)pShooter->nPower / 8.0f;
    int   sx = Player_TurretX(pShooter, 2.0f);
    int   sy = Player_TurretY(pShooter, 2.0f);

    pP->fStartX = (float)sx;
    pP->fStartY = (float)((int)g_dwDisplayH - sy);
    pP->fVX = speed * (float)cos(angle);
    pP->fVY = speed * (float)sin(angle);
    pP->nStep = 0;
    pP->nPrevX = sx;
    pP->nPrevY = sy;
    pP->nFrames = 0;
}

/* =========================================================================
   Phys_StepProjectile
========================================================================= */

void Phys_StepProjectile(Projectile* pP)
{
    pP->nStep += PHYS_STEPS_PER_FRAME;
    pP->nFrames++;
}

/* =========================================================================
   Phys_GetPos
   Matches scorch.js projectilePoint():
       t      = step * STEP_SIZE
       screenX = trunc( startX + (wind + vx) * t )
       worldY  = startY + vy*t - 0.5*g*t*t
       screenY = trunc( HEIGHT - worldY )
========================================================================= */

void Phys_GetPos(const Projectile* pP, float fWind, int* pX, int* pY)
{
    float t = (float)pP->nStep * PHYS_STEP_SIZE;
    float worldY = pP->fStartY
        + pP->fVY * t
        - 0.5f * PHYS_GRAVITY * t * t;

    *pX = (int)(pP->fStartX + (fWind + pP->fVX) * t);
    *pY = (int)((float)g_dwDisplayH - worldY);
}

/* =========================================================================
   Phys_HasEscaped
   Returns 1 if the projectile should stop without a terrain hit.
   Matches scorch.js fire() exit conditions (wall mode = none).
========================================================================= */

int Phys_HasEscaped(const Projectile* pP, int nX, int nY)
{
    if (pP->nFrames >= PHYS_MAX_FRAMES)     return 1;  /* timeout */
    if (nX < 0 || nX >= (int)g_dwDisplayW) return 1;  /* left/right */
    /* bottom of screen counts as a hit (handled by caller), not escape */
    return 0;
}

/* =========================================================================
   Phys_IntersectShot
   Bresenham scan from (x1,y1) to (x2,y2).
   Returns 1 on first solid pixel hit; fills *pHitX, *pHitY.
   Matches scorch.js intersectShot + isSolidForShot.
   isSolidForShot: not terrain background, OR inside any alive tank bbox.
========================================================================= */

int Phys_IntersectShot(int x1, int y1, int x2, int y2,
    const Player* pPlayers, int nCount,
    int* pHitX, int* pHitY)
{
    int dx = (x2 > x1 ? x2 - x1 : x1 - x2);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -(y2 > y1 ? y2 - y1 : y1 - y2);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int e2, i, solid;

    for (;;)
    {
        if (y1 >= 0)
        {
            /* Check terrain */
            solid = !Terrain_IsBackground(x1, y1);

            /* Check tank bounding boxes (isSolidForShot) */
            if (!solid)
            {
                for (i = 0; i < nCount; i++)
                {
                    const Player* pT = &pPlayers[i];
                    if (!pT->bAlive) continue;
                    if (x1 >= pT->nX && x1 < pT->nX + pT->nWidth &&
                        y1 >= pT->nY && y1 < pT->nY + pT->nHeight)
                    {
                        solid = 1; break;
                    }
                }
            }

            if (solid)
            {
                *pHitX = x1;
                *pHitY = y1;
                return 1;
            }
        }

        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
    return 0;
}

/* =========================================================================
   Phys_RoundDamage
   Matches scorch.js roundDamage():
       Direct hit (inside bbox): return abs(powerLimit)
       Corner distance:          return MAX_POWER * (1 - dist / splash)
                                 where splash = radius * 1.02
========================================================================= */

int Phys_RoundDamage(const Player* pP, int cx, int cy, int nRadius)
{
    float lx, rx, uy, ly;
    float corners[4][2];
    float dist, minDist, splash, damage;
    int   i;

    lx = (float)pP->nX;
    rx = (float)(pP->nX + pP->nWidth);
    uy = (float)Player_TurretY(pP, 1.0f);
    ly = (float)(pP->nY + pP->nHeight);

    /* Direct hit inside tank bbox */
    if ((float)cx >= lx && (float)cx <= rx &&
        (float)cy >= uy && (float)cy <= ly)
    {
        return pP->nPowerLimit < 0 ? -pP->nPowerLimit : pP->nPowerLimit;
    }

    /* Corner distance splash */
    corners[0][0] = lx; corners[0][1] = uy;
    corners[1][0] = lx; corners[1][1] = ly;
    corners[2][0] = rx; corners[2][1] = uy;
    corners[3][0] = rx; corners[3][1] = ly;

    minDist = 1e9f;
    for (i = 0; i < 4; i++)
    {
        float dx = corners[i][0] - (float)cx;
        float dy = corners[i][1] - (float)cy;
        dist = (float)sqrt(dx * dx + dy * dy);
        if (dist < minDist) minDist = dist;
    }

    splash = (float)nRadius * 1.02f;
    if (minDist >= splash) return 0;

    damage = (float)PHYS_MAX_POWER * (1.0f - minDist / splash);
    return (int)damage;
}

/* =========================================================================
   Phys_DamagePlayers
   Matches scorch.js damagePlayers() + applyDamage().
   Shield absorption included.
   Returns count of DamageResult entries written.
========================================================================= */

int Phys_DamagePlayers(int nCX, int nCY, int nRadius,
    Player* pPlayers, int nCount,
    int nShooterID,
    DamageResult* pOut, int nMaxOut)
{
    int nResults = 0;
    int i;

    for (i = 0; i < nCount && nResults < nMaxOut; i++)
    {
        Player* pP = &pPlayers[i];
        int     raw, applied;

        if (!pP->bAlive) continue;

        raw = Phys_RoundDamage(pP, nCX, nCY, nRadius);
        if (raw <= 0) continue;

        applied = raw;

        /* Shield absorption (applyDamage in scorch.js) */
        if (pP->bShield)
        {
            float absorbed = raw * pP->fShieldDamage;
            float maxAbsorb = pP->fShieldStrength * (float)PHYS_MAX_POWER;
            if (absorbed > maxAbsorb) absorbed = maxAbsorb;
            pP->fShieldStrength -= absorbed / (float)PHYS_MAX_POWER;
            applied = raw - (int)absorbed;
            if (applied < 0) applied = 0;
            if (pP->fShieldStrength <= 0.0f || pP->fShieldStrength < 0.2f)
            {
                pP->bShield = 0; pP->fShieldStrength = 0.0f;
            }
        }

        pP->nPowerLimit -= applied;
        if (pP->nPower > pP->nPowerLimit)
            pP->nPower = pP->nPowerLimit > 0 ? pP->nPowerLimit : 0;

        pOut[nResults].nPlayerID = pP->nID;
        pOut[nResults].nDamage = applied;
        pOut[nResults].bKilled = (pP->nPowerLimit < PHYS_MIN_POWER);

        if (pOut[nResults].bKilled)
        {
            pP->bAlive = 0;

            /* Record kill for shooter */
            if (nShooterID >= 0 && nShooterID != pP->nID)
            {
                int j;
                for (j = 0; j < nCount; j++)
                {
                    if (pPlayers[j].nID == nShooterID)
                    {
                        pPlayers[j].nKills++;
                        pPlayers[j].nOverallKills++;
                        /* Bounty: AI = 10-30k, human = 40k (scorch.js) */
                        {
                            int bounty = pP->bAI
                                ? 10000 + pP->nAIType * 10000
                                : 40000;
                            pPlayers[j].nCash += bounty;
                            pPlayers[j].nEarnedCash += bounty;
                            pPlayers[j].nOverallGain += bounty;
                        }
                        break;
                    }
                }
            }
        }

        nResults++;
    }
    return nResults;
}

/* =========================================================================
   Phys_SettleOne
   Matches scorch.js settleOneTank().
   Checks base columns for ground support; drops 1px per iteration.
   Returns fall distance (pixels).
========================================================================= */

int Phys_SettleOne(Player* pP)
{
    int type = pP->nTankType;
    int baseStart = k_baseLeft[type];
    int baseEnd = pP->nWidth - k_baseRight[type];
    int fallCount = 0;
    int steps, x, supported;
    int H = (int)g_dwDisplayH;

    if (baseEnd <= baseStart) baseEnd = baseStart + 1;

    for (steps = 0; steps < H * 2; steps++)
    {
        supported = 0;
        for (x = baseStart; x < baseEnd; x++)
        {
            if (!Terrain_IsBackground(pP->nX + x, pP->nY + pP->nHeight))
            {
                supported = 1; break;
            }
        }
        if (supported) break;
        if (pP->nY + pP->nHeight >= H - 1) break;

        pP->nY++;
        fallCount++;
    }
    return fallCount;
}

/* =========================================================================
   Phys_SettleAll
   Drops all alive tanks, applies falling damage.
   Matches scorch.js settleTanks + applyFallingDamage.
   Returns number of newly dead players (IDs written to pDeadIDs[]).
========================================================================= */

int Phys_SettleAll(Player* pPlayers, int nCount,
    int* pDeadIDs, int nMaxDead)
{
    int nDead = 0;
    int i, fallCount;

    for (i = 0; i < nCount; i++)
    {
        Player* pP = &pPlayers[i];
        if (!pP->bAlive) continue;

        fallCount = Phys_SettleOne(pP);
        if (fallCount <= 0) continue;

        /* applyFallingDamage: parachute absorbs if fall > 5px */
        if (fallCount > 5 && pP->nParachutes > 0)
        {
            pP->nParachutes--;
        }
        else
        {
            pP->nPowerLimit -= fallCount;
            if (pP->nPower > pP->nPowerLimit)
                pP->nPower = pP->nPowerLimit > 0 ? pP->nPowerLimit : 0;

            if (pP->nPowerLimit < PHYS_MIN_POWER)
            {
                pP->bAlive = 0;
                if (nDead < nMaxDead)
                    pDeadIDs[nDead++] = pP->nID;
            }
        }
    }
    return nDead;
}

/* =========================================================================
   Phys_InitSub  -- init a sub-projectile from screen pos + screen velocity
========================================================================= */

void Phys_InitSub(Projectile* pP, int screenX, int screenY,
    float fVX, float fVY)
{
    pP->fStartX = (float)screenX;
    pP->fStartY = (float)((int)g_dwDisplayH - screenY);  /* world Y */
    pP->fVX = fVX;
    pP->fVY = fVY;   /* world-Y space, positive = upward */
    pP->nStep = 0;
    pP->nPrevX = screenX;
    pP->nPrevY = screenY;
    pP->nFrames = 0;
}

/* =========================================================================
   Phys_BounceX  -- reflect X velocity, restart from wall edge
========================================================================= */

void Phys_BounceX(Projectile* pP, int screenX, int screenY)
{
    float t = (float)pP->nStep * PHYS_STEP_SIZE;
    float worldY = (float)((int)g_dwDisplayH - screenY);
    float curVY = pP->fVY - PHYS_GRAVITY * t;

    pP->fVX = -pP->fVX;
    pP->fStartX = (float)screenX;
    pP->fStartY = worldY;
    pP->fVY = curVY;
    pP->nStep = 0;
    pP->nPrevX = screenX;
    pP->nPrevY = screenY;
}

/* =========================================================================
   Phys_WrapX  -- teleport to other side, keep velocity
========================================================================= */

void Phys_WrapX(Projectile* pP, int newScreenX, int screenY)
{
    float t = (float)pP->nStep * PHYS_STEP_SIZE;
    float worldY = (float)((int)g_dwDisplayH - screenY);
    float curVY = pP->fVY - PHYS_GRAVITY * t;

    pP->fStartX = (float)newScreenX;
    pP->fStartY = worldY;
    pP->fVY = curVY;
    pP->nStep = 0;
    pP->nPrevX = newScreenX;
    pP->nPrevY = screenY;
}

/* =========================================================================
   Phys_DamageLaser  -- apply beam damage to any player bbox intersecting
   the beam.  Simple: check if any player bbox corner is within nBeamWidth/2
   pixels of the line segment.
========================================================================= */

static float PointLineDist(float px, float py,
    float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len2 = dx * dx + dy * dy;
    float t, nx, ny;
    if (len2 < 0.001f) { dx = px - x1; dy = py - y1; return dx * dx + dy * dy; }
    t = ((px - x1) * dx + (py - y1) * dy) / len2;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    nx = x1 + t * dx - px;
    ny = y1 + t * dy - py;
    return nx * nx + ny * ny;
}

int Phys_DamageLaser(int x1, int y1, int nAngleDeg,
    Player* pPlayers, int nCount, int nShooterID,
    int nBeamDamage,
    DamageResult* pOut, int nMaxOut)
{
    float angle = (float)nAngleDeg * 3.14159265f / 180.0f;
    float range = (float)(g_dwDisplayW > g_dwDisplayH
        ? g_dwDisplayW : g_dwDisplayH) * 1.5f;
    float x2 = (float)x1 + range * (float)cos(angle);
    float y2 = (float)y1 - range * (float)sin(angle);
    float halfW = 4.f;   /* beam half-width for hit test */
    int   nOut = 0;
    int   i;

    for (i = 0; i < nCount && nOut < nMaxOut; i++)
    {
        Player* pP = &pPlayers[i];
        float cx, cy, dist2;
        if (!pP->bAlive) continue;

        cx = (float)pP->nX + (float)pP->nWidth * 0.5f;
        cy = (float)pP->nY + (float)pP->nHeight * 0.5f;
        dist2 = PointLineDist(cx, cy, (float)x1, (float)y1, x2, y2);

        if (dist2 <= (halfW + (float)pP->nWidth * 0.5f) *
            (halfW + (float)pP->nWidth * 0.5f))
        {
            int applied = nBeamDamage;

            if (pP->bShield)
            {
                float absorbed = (float)applied * pP->fShieldDamage;
                pP->fShieldStrength -= absorbed / (float)PHYS_MAX_POWER;
                applied -= (int)absorbed;
                if (pP->fShieldStrength <= 0.f)
                {
                    pP->bShield = 0; pP->fShieldStrength = 0.f;
                }
            }

            pP->nPowerLimit -= applied;
            if (pP->nPower > pP->nPowerLimit)
                pP->nPower = pP->nPowerLimit > 0 ? pP->nPowerLimit : 0;

            pOut[nOut].nPlayerID = pP->nID;
            pOut[nOut].nDamage = applied;
            pOut[nOut].bKilled = (pP->nPowerLimit < PHYS_MIN_POWER);
            if (pOut[nOut].bKilled)
            {
                pP->bAlive = 0;
                if (nShooterID >= 0)
                {
                    int j;
                    for (j = 0; j < nCount; j++)
                        if (pPlayers[j].nID == nShooterID)
                        {
                            pPlayers[j].nKills++; pPlayers[j].nOverallKills++; break;
                        }
                }
            }
            nOut++;
        }
    }
    return nOut;
}