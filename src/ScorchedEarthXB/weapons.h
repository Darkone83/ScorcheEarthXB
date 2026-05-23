#ifndef SCORCHEDXB_WEAPONS_H
#define SCORCHEDXB_WEAPONS_H

#include <xtl.h>
#include "random.h"
#include "player.h"

/*---------------------------------------------------------------------------
    ScorchedXB - weapons.h
    Weapon definitions and terrain-modification API.

    Weapon table matches scorch.js WEAPONS[] array exactly (index = ID).
    Weapon_Apply handles all terrain carving for a given kind after impact.
    For roller and laser, callers use the kind-specific functions directly.

    Caller must call Terrain_Drop + Terrain_Upload after any apply.
    Weapon_Apply does NOT call Drop (game.cpp owns that step).

    Weapon kinds:
        WKIND_SIMPLE  fillCircle bg                    (missile, nukes)
        WKIND_SAND    sparse upward ground spray       (sand bomb)
        WKIND_ROLLER  simple circle at final pos       (handled by game)
        WKIND_DIGGER  10 random-walk carvers           (digger family)
        WKIND_FUNKY   6 random bg circles              (funky bomb/nuke)
        WKIND_MIRV    5 circles in shallow V pattern   (mirv/death head)
        WKIND_NAPALM  wide vertical-strip carve        (napalm/hot napalm)
        WKIND_LASER   Bresenham line of circles        (laser)
        WKIND_SHIELD  activates shield on round start  (consumable)
        WKIND_CHUTE   sets nParachutes on round start  (consumable)
---------------------------------------------------------------------------*/

typedef enum
{
    WKIND_SIMPLE = 0,
    WKIND_SAND,
    WKIND_ROLLER,
    WKIND_DIGGER,
    WKIND_FUNKY,
    WKIND_MIRV,
    WKIND_NAPALM,
    WKIND_LASER,
    WKIND_SHIELD,
    WKIND_CHUTE
} WeaponKind;

typedef struct
{
    const char* pszName;
    int          nRadius;
    int          nStartAmmo;   /* 999 = infinite */
    int          nPrice;
    WeaponKind   eKind;
    int          bInfinite;
    int          nParticles;   /* funky/mirv sub-projectile count */
    int          nDuration;    /* digger step count */
    int          nBeamWidth;   /* laser */
    int          nBeamDamage;  /* laser */
} WeaponDef;

#define WEAPON_COUNT  19

extern const WeaponDef k_weapons[WEAPON_COUNT];

/* Named indices matching scorch.js WEAPONS order */
#define WID_MISSILE         0
#define WID_BABY_NUKE       1
#define WID_NUKE            2
#define WID_SAND_BOMB       3
#define WID_BABY_ROLLER     4
#define WID_ROLLER          5
#define WID_HEAVY_ROLLER    6
#define WID_BABY_DIGGER     7
#define WID_DIGGER          8
#define WID_HEAVY_DIGGER    9
#define WID_FUNKY_BOMB      10
#define WID_FUNKY_NUKE      11
#define WID_NAPALM          12
#define WID_HOT_NAPALM      13
#define WID_MIRV            14
#define WID_DEATH_HEAD      15
#define WID_LASER           16
#define WID_SHIELD          17   /* activates shield at round start */
#define WID_PARACHUTE       18   /* grants parachutes at round start */

/* Weapon descriptions (also used by store and help screens) */
extern const char* k_weaponDesc[WEAPON_COUNT];

/* ── Terrain modification ──
   Apply the weapon's terrain carve at (x, y).
   Does NOT call Terrain_Drop or Terrain_MarkDirty -- caller owns those.   */
void Weapon_Apply(int nWeaponID, int x, int y, Rand* pRand);

/* Laser: carve along beam line from (x1,y1) to screen edge at angle.
   x1,y1 = turret tip in screen coords, angle in degrees.                  */
void Weapon_ApplyLaser(int x1, int y1, int nAngleDeg, int nBeamWidth);

/* Roller: returns (via pFinalX, pFinalY) where roller stops after rolling.
   Call Weapon_Apply(WID_ROLLER, finalX, finalY, ...) for the explosion.   */
void Weapon_RollerSettle(int startX, int startY, float fVX,
    int* pFinalX, int* pFinalY);

#endif /* SCORCHEDXB_WEAPONS_H */