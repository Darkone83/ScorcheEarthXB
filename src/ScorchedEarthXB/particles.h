#ifndef SCORCHEDXB_PARTICLES_H
#define SCORCHEDXB_PARTICLES_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - particles.h
    Screen-space particle system for explosion sparks and debris.

    Fixed pool of PARTICLE_MAX particles.  All active particles rendered
    in one D3DPT_POINTLIST call with D3DRS_POINTSIZE = 3.

    Gravity: PARTICLE_GRAVITY pixels/sec^2 (screen space, Y down).
    Alpha:   linear fade from full to zero over particle lifetime.

    Spawn helpers:
        Particles_SpawnBurst  -- radiating spark ring (impact flash)
        Particles_SpawnDirt   -- arc of ground-coloured chunks

    Lifecycle:
        Particles_Init()      -- once at startup
        Particles_Spawn()     -- add one particle
        Particles_Update(dt)  -- call every frame with delta time in seconds
        Particles_Draw()      -- call inside BeginFrame/EndFrame
        Particles_Clear()     -- remove all (on round reset)
---------------------------------------------------------------------------*/

void Particles_Init(void);
void Particles_Clear(void);
void Particles_Update(float fDt);
void Particles_Draw(void);

/* Spawn one particle directly */
void Particles_Spawn(float fX, float fY,
    float fVX, float fVY,
    DWORD dwColor,
    float fLifeSec);

/* Burst of sparks radiating from (x, y) in nCount random directions */
void Particles_SpawnBurst(float fX, float fY,
    DWORD dwColor,
    int   nCount,
    float fSpeed,
    float fLifeSec);

/* Arc of dirt-coloured debris launched upward from (x, y) */
void Particles_SpawnDirt(float fX, float fY,
    DWORD dwGroundColor,
    int   nCount);

#endif /* SCORCHEDXB_PARTICLES_H */