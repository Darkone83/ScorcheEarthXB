/*---------------------------------------------------------------------------
    ScorchedXB - particles.cpp
    Fixed-pool screen-space particle system.

    All particles are rendered as D3DPT_POINTLIST with D3DRS_POINTSIZE = 3,
    no texture.  Alpha is baked into DIFFUSE vertex colour so no separate
    blend-state change is needed between individual particles.

    One vertex per particle; all active particles batched into s_verts[]
    and submitted in a single DrawPrimitiveUP call.

    Gravity (PARTICLE_GRAVITY px/sec^2) acts on all particles in the +Y
    (down) direction matching screen-space convention.

    Particles are born with full alpha and fade linearly to zero over their
    lifetime.  They deactivate when lifetime <= 0 or they leave the screen.
---------------------------------------------------------------------------*/

#include <math.h>
#include "particles.h"
#include "random.h"
#include "render.h"

#define PARTICLE_MAX      512
#define PARTICLE_GRAVITY  300.0f

/* =========================================================================
   Particle pool
========================================================================= */

typedef struct
{
    float fX, fY;
    float fVX, fVY;
    DWORD dwBaseColor;  /* RGB without alpha */
    float fLife;        /* remaining seconds */
    float fMaxLife;
    int   bActive;
} Particle;

static Particle s_pool[PARTICLE_MAX];
static int      s_nActive = 0;

/* =========================================================================
   Vertex buffer (one entry per active particle, filled each Draw call)
========================================================================= */

#define PART_FVF  ( D3DFVF_XYZRHW | D3DFVF_DIFFUSE )

typedef struct { float x, y, z, rhw; DWORD c; } PV;

static PV s_verts[PARTICLE_MAX];

/* =========================================================================
   Global RNG for spawn randomisation
========================================================================= */

static Rand s_rand;

/* =========================================================================
   Particles_Init
========================================================================= */

void Particles_Init(void)
{
    int i;
    for (i = 0; i < PARTICLE_MAX; i++) s_pool[i].bActive = 0;
    s_nActive = 0;
    Rand_Init(&s_rand, 0x5CA77E1D);
}

/* =========================================================================
   Particles_Clear
========================================================================= */

void Particles_Clear(void)
{
    int i;
    for (i = 0; i < PARTICLE_MAX; i++) s_pool[i].bActive = 0;
    s_nActive = 0;
}

/* =========================================================================
   Particles_Spawn
========================================================================= */

void Particles_Spawn(float fX, float fY,
    float fVX, float fVY,
    DWORD dwColor,
    float fLifeSec)
{
    int i;
    Particle* pP = NULL;

    /* Find first inactive slot */
    for (i = 0; i < PARTICLE_MAX; i++)
    {
        if (!s_pool[i].bActive) { pP = &s_pool[i]; break; }
    }
    if (!pP) return;  /* pool full -- drop silently */

    pP->fX = fX;
    pP->fY = fY;
    pP->fVX = fVX;
    pP->fVY = fVY;
    pP->dwBaseColor = dwColor & 0x00FFFFFFu;  /* strip alpha */
    pP->fLife = fLifeSec;
    pP->fMaxLife = fLifeSec > 0.001f ? fLifeSec : 0.001f;
    pP->bActive = 1;
    s_nActive++;
}

/* =========================================================================
   Particles_SpawnBurst
   Radiating spark ring -- nCount particles in random directions.
   Color darkens slightly at lower angles for visual variety.
========================================================================= */

void Particles_SpawnBurst(float fX, float fY,
    DWORD dwColor,
    int   nCount,
    float fSpeed,
    float fLifeSec)
{
    int   i;
    float angle, vx, vy, speed;

    for (i = 0; i < nCount; i++)
    {
        angle = Rand_Float(&s_rand) * 6.28318f;   /* 0 - 2pi */
        speed = fSpeed * (0.5f + Rand_Float(&s_rand) * 0.8f);
        vx = (float)cos(angle) * speed;
        vy = (float)sin(angle) * speed;
        Particles_Spawn(fX, fY, vx, vy, dwColor,
            fLifeSec * (0.6f + Rand_Float(&s_rand) * 0.4f));
    }
}

/* =========================================================================
   Particles_SpawnDirt
   Arc of debris: launched in a upward cone (120-60 degrees from horizontal)
   in a random spread, coloured with the terrain ground colour.
========================================================================= */

void Particles_SpawnDirt(float fX, float fY,
    DWORD dwGroundColor,
    int   nCount)
{
    int   i;
    float angle, speed, vx, vy;

    for (i = 0; i < nCount; i++)
    {
        /* Upward arc: screen Y is down so -Y = upward.
           Angle range roughly 30-150 degrees from horizontal. */
        angle = (0.52f + Rand_Float(&s_rand) * 2.09f);  /* 30-150 deg */
        speed = 80.0f + Rand_Float(&s_rand) * 160.0f;
        vx = (float)cos(angle) * speed;
        vy = -(float)sin(angle) * speed;  /* negative = up on screen */
        Particles_Spawn(fX, fY, vx, vy, dwGroundColor,
            0.6f + Rand_Float(&s_rand) * 0.6f);
    }
}

/* =========================================================================
   Particles_Update
   Apply gravity, move, decay lifetime, deactivate expired or off-screen.
========================================================================= */

void Particles_Update(float fDt)
{
    int i;
    int W = (int)g_dwDisplayW;
    int H = (int)g_dwDisplayH;

    s_nActive = 0;
    for (i = 0; i < PARTICLE_MAX; i++)
    {
        Particle* pP = &s_pool[i];
        if (!pP->bActive) continue;

        pP->fVY += PARTICLE_GRAVITY * fDt;   /* gravity (+Y = down) */
        pP->fX += pP->fVX * fDt;
        pP->fY += pP->fVY * fDt;
        pP->fLife -= fDt;

        if (pP->fLife <= 0.0f ||
            pP->fX < 0.f || pP->fX >= (float)W ||
            pP->fY < 0.f || pP->fY >= (float)H)
        {
            pP->bActive = 0;
            continue;
        }
        s_nActive++;
    }
}

/* =========================================================================
   Particles_Draw
   Collect active particles into s_verts[], submit as one POINTLIST.
========================================================================= */

void Particles_Draw(void)
{
    int i, n;

    if (s_nActive == 0) return;

    n = 0;
    for (i = 0; i < PARTICLE_MAX && n < PARTICLE_MAX; i++)
    {
        Particle* pP = &s_pool[i];
        DWORD     alpha;
        float     fAlpha;

        if (!pP->bActive) continue;

        fAlpha = pP->fLife / pP->fMaxLife;
        if (fAlpha < 0.0f) fAlpha = 0.0f;
        if (fAlpha > 1.0f) fAlpha = 1.0f;
        alpha = (DWORD)(fAlpha * 255.0f);

        s_verts[n].x = pP->fX;
        s_verts[n].y = pP->fY;
        s_verts[n].z = 0.0f;
        s_verts[n].rhw = 1.0f;
        s_verts[n].c = (alpha << 24) | pP->dwBaseColor;
        n++;
    }

    if (n == 0) return;

    g_pd3dDevice->SetVertexShader(PART_FVF);
    g_pd3dDevice->SetTexture(0, NULL);
    {
        float fPtSize = 3.0f;
        g_pd3dDevice->SetRenderState(D3DRS_POINTSIZE, *(DWORD*)&fPtSize);
    }
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);  /* additive */
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    g_pd3dDevice->DrawPrimitiveUP(D3DPT_POINTLIST, (UINT)n, s_verts, sizeof(PV));

    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}