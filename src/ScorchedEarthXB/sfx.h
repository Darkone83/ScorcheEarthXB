#ifndef SCORCHEDXB_SFX_H
#define SCORCHEDXB_SFX_H

/*---------------------------------------------------------------------------
    ScorchedXB - sfx.h
    Named SFX constants and playback API.

    All files live in D:\sfx\ as PCM 16-bit 44100Hz mono WAV.
    Loaded once at Sfx_Init(); slots managed inside audio.cpp SFX pool.

    SFX_BATTLEFIELD_LOOP loops continuously -- call Sfx_Stop on it to end.
    All other sounds play once.
---------------------------------------------------------------------------*/

typedef enum
{
    /* Fire sounds */
    SFX_WEAPON_FIRE_LIGHT = 0,
    SFX_WEAPON_FIRE_MEDIUM,
    SFX_WEAPON_FIRE_CLICK,
    SFX_CANNON_FIRE_01,
    SFX_CANNON_FIRE_HEAVY,
    SFX_CANNON_FIRE_SHORT,
    SFX_CANNON_FIRE_DRY,
    SFX_CANNON_FIRE_ALT,

    /* Explosions */
    SFX_EXPLOSION_SMALL_01,
    SFX_EXPLOSION_SMALL_02,
    SFX_EXPLOSION_MEDIUM_01,
    SFX_EXPLOSION_MEDIUM_02,
    SFX_EXPLOSION_HEAVY_01,
    SFX_EXPLOSION_HEAVY_02,

    /* Impacts */
    SFX_IMPACT_DIRT_01,
    SFX_IMPACT_DIRT_02,

    /* Tank destroyed */
    SFX_TANK_DESTROYED_01,
    SFX_TANK_DESTROYED_02,

    /* Special weapon effects */
    SFX_AIRBURST_01,
    SFX_AIRBURST_02,
    SFX_CLUSTER_POP_01,
    SFX_CLUSTER_POP_LONG,
    SFX_MISSILE_POP_01,
    SFX_MISSILE_POP_02,

    /* Ambient loop */
    SFX_BATTLEFIELD_LOOP,   /* looping -- call Sfx_Stop() to end */

    SFX_COUNT
} SfxId;

void Sfx_Init(void);   /* loads all files into audio SFX pool */
void Sfx_Play(SfxId id);
void Sfx_Stop(SfxId id);
void Sfx_Volume(SfxId id, int nVol);   /* 0-100 */
void Sfx_SetGlobalVolume(int nVol);             /* 0-100, scales all slots */

#endif /* SCORCHEDXB_SFX_H */