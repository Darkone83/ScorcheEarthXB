#ifndef SCORCHEDXB_PHYSICS_H
#define SCORCHEDXB_PHYSICS_H

#include <xtl.h>
#include "player.h"

/*---------------------------------------------------------------------------
    ScorchedXB - physics.h
    Projectile simulation and damage ported from scorch.js.

    Coordinate conventions
    ----------------------
    Screen:  X right, Y down, (0,0) = top-left.
    World Y: measured from bottom of screen upward (Y = HEIGHT - screenY).
    All Phys_ functions use SCREEN coordinates for input/output positions.
    Internally, worldY is used for the ballistic equation.

    Projectile equation (matches scorch.js projectilePoint exactly):
        t       = nStep * PHYS_STEP_SIZE          (0.1 sec per step unit)
        screenX = (int)(startX + (wind + vx) * t)
        worldY  = startY + vy*t - 0.5 * PHYS_GRAVITY * t^2
        screenY = (int)(HEIGHT - worldY)

    Launch: Player_TurretX/Y with q=2 (2x barrel length, outside sprite).
    step += PHYS_STEPS_PER_FRAME (2) each frame, matching scorch.js step += 2.
    Max flight: PHYS_MAX_FRAMES (1800) frames before escape.

    Damage (roundDamage / damagePlayers in scorch.js):
        Direct hit (inside tank bbox):  damage = abs(powerLimit)  [instant kill]
        Splash (corner distance < r*1.02): damage = MAX_POWER * (1 - dist/splash)
        powerLimit -= damage; if powerLimit < MIN_POWER -> dead.

    Settling (settleOneTank):
        Scan bottom row of tank sprite base for ground.
        Drop 1px per iteration until supported or at screen bottom.
        Fall damage = fall distance (pixels); parachute absorbs if > 5px.
---------------------------------------------------------------------------*/

#define PHYS_GRAVITY          9.8f
#define PHYS_STEP_SIZE        0.1f
#define PHYS_STEPS_PER_FRAME  1
#define PHYS_MAX_FRAMES       1800
#define PHYS_MAX_POWER        1000
#define PHYS_MIN_POWER        200

/* ── Projectile ── */
typedef struct
{
    float fStartX;      /* launch X in screen coords                     */
    float fStartY;      /* launch worldY (from screen bottom)            */
    float fVX;          /* initial X velocity                            */
    float fVY;          /* initial Y velocity (worldY space, +ve = up)  */
    int   nStep;        /* step counter (advance by PHYS_STEPS_PER_FRAME)*/
    int   nPrevX;       /* previous screen X (for collision line)        */
    int   nPrevY;       /* previous screen Y                             */
    int   nFrames;      /* frames in flight                              */
} Projectile;

/* ── Damage record (one per affected player) ── */
typedef struct
{
    int nPlayerID;
    int nDamage;
    int bKilled;
} DamageResult;

/* ── API ── */

/* Set up projectile from the firing player (q=2 launch point) */
void Phys_InitProjectile(Projectile* pP, const Player* pShooter);

/* Advance one frame (nStep += PHYS_STEPS_PER_FRAME) */
void Phys_StepProjectile(Projectile* pP);

/* Compute current screen position from step and wind */
void Phys_GetPos(const Projectile* pP, float fWind, int* pX, int* pY);

/* Returns 1 if projectile should stop (out of bounds or max frames) */
int  Phys_HasEscaped(const Projectile* pP, int nX, int nY);

/* Bresenham line collision: terrain + tank bboxes.
   Returns 1 on hit; fills *pHitX, *pHitY with impact pixel. */
int  Phys_IntersectShot(int nX1, int nY1, int nX2, int nY2,
    const Player* pPlayers, int nCount,
    int* pHitX, int* pHitY);

/* Apply explosion damage to all players.
   Returns number of results written to pOut (killed and injured).
   nShooterID = pPlayers[i].nID of the shooter (-1 if none). */
int  Phys_DamagePlayers(int nCX, int nCY, int nRadius,
    Player* pPlayers, int nCount,
    int nShooterID,
    DamageResult* pOut, int nMaxOut);

/* Compute raw damage a single explosion at (cx,cy,r) deals to one player.
   Used by the AI scorer; does not modify player state. */
int  Phys_RoundDamage(const Player* pP, int cx, int cy, int nRadius);

/* Drop all living tanks to terrain surface.
   Returns number of dead players; fills pDeadIDs[] with their nID values. */
int  Phys_SettleAll(Player* pPlayers, int nCount,
    int* pDeadIDs, int nMaxDead);

/* Settle one tank; returns fall distance in pixels (for fall damage) */
int  Phys_SettleOne(Player* pP);

/* Init a sub-projectile from a world position with explicit screen velocities.
   Used for MIRV/Funky animated sub-shots after main impact.               */
void Phys_InitSub(Projectile* pP, int screenX, int screenY,
    float fVX, float fVY);

/* Bounce: negate X velocity and restart trajectory from (screenX, screenY).
   Call when projectile hits left/right wall in bounce mode.               */
void Phys_BounceX(Projectile* pP, int screenX, int screenY);

/* Wrap: restart trajectory from (newScreenX, screenY) with same velocities.
   Call when projectile exits left/right wall in wrap mode.                */
void Phys_WrapX(Projectile* pP, int newScreenX, int screenY);

/* Damage players along a laser beam.  Walks from (x1,y1) at angleDeg,
   applies nBeamDamage to any player whose bbox intersects the beam.
   Returns number of results written to pOut.                              */
int  Phys_DamageLaser(int x1, int y1, int nAngleDeg,
    Player* pPlayers, int nCount, int nShooterID,
    int nBeamDamage,
    DamageResult* pOut, int nMaxOut);

#endif /* SCORCHEDXB_PHYSICS_H */