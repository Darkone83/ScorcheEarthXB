/*---------------------------------------------------------------------------
    ScorchedXB - sfx.cpp
    Loads the SFX pack from D:\sfx\ and wraps Audio_Sfx* with named IDs.

    All 25 files are loaded once at Sfx_Init() into the audio SFX pool.
    Sfx_Play / Sfx_Stop / Sfx_Volume delegate to Audio_Sfx* by slot index.
    SFX_BATTLEFIELD_LOOP uses Audio_SfxPlayLooping (DSBPLAY_LOOPING).

    If a file fails to load its slot stays -1 and calls are silently ignored.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include "sfx.h"
#include "audio.h"

/* -------------------------------------------------------------------------
   File table -- order matches SfxId enum exactly
------------------------------------------------------------------------- */

static const char* k_sfxPaths[SFX_COUNT] =
{
    /* Fire */
    "D:\\sfx\\weapon_fire_light.wav",
    "D:\\sfx\\weapon_fire_medium.wav",
    "D:\\sfx\\weapon_fire_click.wav",
    "D:\\sfx\\cannon_fire_01.wav",
    "D:\\sfx\\cannon_fire_heavy.wav",
    "D:\\sfx\\cannon_fire_short.wav",
    "D:\\sfx\\cannon_fire_dry.wav",
    "D:\\sfx\\cannon_fire_alt.wav",

    /* Explosions */
    "D:\\sfx\\explosion_small_01.wav",
    "D:\\sfx\\explosion_small_02.wav",
    "D:\\sfx\\explosion_medium_01.wav",
    "D:\\sfx\\explosion_medium_02.wav",
    "D:\\sfx\\explosion_heavy_01.wav",
    "D:\\sfx\\explosion_heavy_02.wav",

    /* Impacts */
    "D:\\sfx\\impact_dirt_01.wav",
    "D:\\sfx\\impact_dirt_02.wav",

    /* Tank destroyed */
    "D:\\sfx\\tank_destroyed_01.wav",
    "D:\\sfx\\tank_destroyed_02.wav",

    /* Special weapon effects */
    "D:\\sfx\\airburst_01.wav",
    "D:\\sfx\\airburst_02.wav",
    "D:\\sfx\\cluster_pop_01.wav",
    "D:\\sfx\\cluster_pop_long.wav",
    "D:\\sfx\\missile_pop_01.wav",
    "D:\\sfx\\missile_pop_02.wav",

    /* Ambient loop */
    "D:\\sfx\\battlefield_fireworks_loop.wav",
};

/* 1 = looping, 0 = one-shot */
static const int k_sfxLoop[SFX_COUNT] =
{
    0, 0, 0, 0, 0, 0, 0, 0,   /* fire */
    0, 0, 0, 0, 0, 0,          /* explosions */
    0, 0,                       /* impacts */
    0, 0,                       /* tank destroyed */
    0, 0, 0, 0, 0, 0,          /* special effects */
    1                           /* battlefield loop */
};

/* -------------------------------------------------------------------------
   Slot map -- index = SfxId, value = audio pool slot (-1 = failed)
------------------------------------------------------------------------- */

static int s_slots[SFX_COUNT];

/* -------------------------------------------------------------------------
   Sfx_Init
------------------------------------------------------------------------- */

void Sfx_Init(void)
{
    int i;
    for (i = 0; i < SFX_COUNT; i++)
    {
        s_slots[i] = Audio_SfxLoad(k_sfxPaths[i]);
    }
}

/* -------------------------------------------------------------------------
   Sfx_Play
------------------------------------------------------------------------- */

void Sfx_Play(SfxId id)
{
    int slot;
    if ((int)id < 0 || (int)id >= SFX_COUNT) return;
    slot = s_slots[id];
    if (slot < 0) return;

    if (k_sfxLoop[id])
        Audio_SfxPlayLooping(slot);
    else
        Audio_SfxPlay(slot);
}

/* -------------------------------------------------------------------------
   Sfx_Stop
------------------------------------------------------------------------- */

void Sfx_Stop(SfxId id)
{
    int slot;
    if ((int)id < 0 || (int)id >= SFX_COUNT) return;
    slot = s_slots[id];
    if (slot < 0) return;
    Audio_SfxStop(slot);
}

/* -------------------------------------------------------------------------
   Sfx_Volume
------------------------------------------------------------------------- */

void Sfx_Volume(SfxId id, int nVol)
{
    int slot;
    if ((int)id < 0 || (int)id >= SFX_COUNT) return;
    slot = s_slots[id];
    if (slot < 0) return;
    Audio_SfxVolume(slot, nVol);
}

/* -------------------------------------------------------------------------
   Sfx_SetGlobalVolume
   Scales all loaded SFX slots to nVol (0-100).
   Stored so newly played sounds also get the right level via Sfx_Play
   calling Audio_SfxVolume on each trigger.
------------------------------------------------------------------------- */

static int s_nGlobalVol = 100;

void Sfx_SetGlobalVolume(int nVol)
{
    int i;
    if (nVol < 0) nVol = 0;
    if (nVol > 100) nVol = 100;
    s_nGlobalVol = nVol;
    for (i = 0; i < SFX_COUNT; i++)
    {
        if (s_slots[i] >= 0)
            Audio_SfxVolume(s_slots[i], nVol);
    }
}