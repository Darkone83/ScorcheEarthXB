/*---------------------------------------------------------------------------
    ScorchedXB - game.cpp
    Round loop, turn management, and full game-session state.

    SFX mapping (scorch.js beep() → our pack):
        On fire:   weapon kind + radius → cannon/shot variant
        On explode: radius → explosion_small/medium/heavy (random 01/02)
        On roller: impact_dirt on land
        On laser:  weapon_fire_click
        On death:  tank_destroyed (random 01/02)
        Ambient:   battlefield_fireworks_loop for round duration

    Audio: gameplay music started by main.cpp State_Enter(STATE_GAME).
           Game module only controls SFX.

    Phase machine:
        AIMING      → human inputs angle/power, AI computes shot
        FIRING      → projectile advances each frame
        EXPLODING   → expanding ring animation, then apply+damage+settle
        ROUND_PAUSE → brief pause showing result before advancing
---------------------------------------------------------------------------*/

#include <math.h>
#include <string.h>
#include "game.h"
#include "setup.h"
#include "render.h"
#include "input.h"
#include "terrain.h"
#include "player.h"
#include "tanks.h"
#include "physics.h"
#include "weapons.h"
#include "particles.h"
#include "ai.h"
#include "sfx.h"
#include "audio.h"
#include "ui.h"
#include "random.h"
#include "config.h"


/* =========================================================================
   Constants
========================================================================= */

#define MAX_WIND          10
#define AIM_RATE_MS       50u   /* angle/power change rate when held */
#define EXPL_FRAMES       40    /* explosion animation length (frames)    */
#define ROUND_PAUSE_MS    3000u /* pause after round ends                 */
#define AI_DELAY_MS       1200u /* brief pause before AI fires            */

/* =========================================================================
   Game phase
========================================================================= */

typedef enum
{
    GPHASE_AIMING = 0,
    GPHASE_AI_DELAY,
    GPHASE_FIRING,
    GPHASE_SUBFIRING,   /* MIRV/Funky animated sub-shots */
    GPHASE_LASER,       /* laser beam display before apply */
    GPHASE_EXPLODING,
    GPHASE_ROUND_PAUSE
} GamePhase;

/* =========================================================================
   State
========================================================================= */

static Player    s_players[GAME_PLAYER_COUNT];
static int       s_nPlayerCount = GAME_PLAYER_COUNT;  /* actual players this session */
static int       s_nActive = 0;
static int       s_nWind = 0;
static int       s_nRound = 0;
static int       s_bRoundOver = 0;
static int       s_bGameOver = 0;
static GamePhase s_ePhase = GPHASE_AIMING;
static Rand      s_rand;

/* Firing state */
static Projectile s_proj;
static int        s_nWeaponID = 0;
static int        s_nHitX = 0;
static int        s_nHitY = 0;
static int        s_bHit = 0;

/* Sub-projectiles (MIRV / Funky) */
#define SUB_MAX 10
static Projectile s_subProj[SUB_MAX];
static int        s_subHitX[SUB_MAX];
static int        s_subHitY[SUB_MAX];
static int        s_subDone[SUB_MAX];
static int        s_nSubCount = 0;

/* Laser beam */
static int   s_nLaserX1 = 0, s_nLaserY1 = 0;
static int   s_nLaserX2 = 0, s_nLaserY2 = 0;
static int   s_nLaserAngle = 0;

/* Tracer trail (previous shot path) */
#define TRACER_MAX 300
static int   s_tracerX[TRACER_MAX];
static int   s_tracerY[TRACER_MAX];
static int   s_nTracerLen = 0;

/* Explosion animation */
static int    s_nExplFrame = 0;
static int    s_nExplRadius = 0;
static float  s_fExplX = 0.f;
static float  s_fExplY = 0.f;

/* Timing */
static DWORD  s_dwAimTime = 0;
static DWORD  s_dwPhaseStart = 0;
static DWORD  s_dwRumbleEnd = 0;
static int    s_bMusicStarted = 0;
static int    s_bActive = 0;

/* Round result text */
static char   s_szResult[64] = "";

/* =========================================================================
   SFX helpers
========================================================================= */

/* Map weapon kind + radius to a fire sound */
static void PlayFireSfx(int nWeaponID)
{
    const WeaponDef* pW = &k_weapons[nWeaponID];
    switch (pW->eKind)
    {
    case WKIND_SIMPLE:
        if (pW->nRadius >= 80)      Sfx_Play(SFX_CANNON_FIRE_HEAVY);
        else if (pW->nRadius >= 40) Sfx_Play(SFX_CANNON_FIRE_SHORT);
        else                         Sfx_Play(SFX_CANNON_FIRE_01);
        break;
    case WKIND_ROLLER:  Sfx_Play(SFX_CANNON_FIRE_ALT); break;
    case WKIND_DIGGER:  Sfx_Play(SFX_WEAPON_FIRE_MEDIUM); break;
    case WKIND_FUNKY:   Sfx_Play(SFX_AIRBURST_01); break;
    case WKIND_MIRV:    Sfx_Play(SFX_MISSILE_POP_01); break;
    case WKIND_NAPALM:  Sfx_Play(SFX_CANNON_FIRE_HEAVY); break;
    case WKIND_SAND:    Sfx_Play(SFX_CANNON_FIRE_DRY); break;
    case WKIND_LASER:   Sfx_Play(SFX_WEAPON_FIRE_CLICK); break;
    default:            Sfx_Play(SFX_CANNON_FIRE_01); break;
    }
}

/* Map explosion radius to appropriate boom SFX */
static void PlayExplodeSfx(int nRadius)
{
    int nVar = Rand_Int(&s_rand, 2);   /* 0 or 1 for 01/02 variants */
    if (nRadius >= 80)
        Sfx_Play(nVar ? SFX_EXPLOSION_HEAVY_02 : SFX_EXPLOSION_HEAVY_01);
    else if (nRadius >= 30)
        Sfx_Play(nVar ? SFX_EXPLOSION_MEDIUM_02 : SFX_EXPLOSION_MEDIUM_01);
    else
        Sfx_Play(nVar ? SFX_EXPLOSION_SMALL_02 : SFX_EXPLOSION_SMALL_01);
}

/* Rumble with auto-timeout */
static void DoRumble(WORD wLeft, WORD wRight, DWORD dwMs)
{
    if (!g_cfg.bRumble) return;
    SetRumble(wLeft, wRight);
    s_dwRumbleEnd = GetTickCount() + dwMs;
}

static int NextAlive(void)
{
    int i, idx;
    for (i = 1; i <= s_nPlayerCount; i++)
    {
        idx = (s_nActive + i) % s_nPlayerCount;
        if (s_players[idx].bAlive) return idx;
    }
    return -1;
}

static int CountAlive(void)
{
    int i, n = 0;
    for (i = 0; i < s_nPlayerCount; i++)
        if (s_players[i].bAlive) n++;
    return n;
}

/* =========================================================================
   Apply weapon, damage, settle -- called when explosion animation ends
========================================================================= */

static void ApplyOneExplosion(int hitX, int hitY, int weaponID)
{
    DamageResult results[GAME_PLAYER_COUNT];
    int          dead[GAME_PLAYER_COUNT];
    int          nDead, nResults, i, x0, x1;
    const WeaponDef* pW = &k_weapons[weaponID];

    Weapon_Apply(weaponID, hitX, hitY, &s_rand);

    x0 = hitX - pW->nRadius * 3 - 1;
    x1 = hitX + pW->nRadius * 3 + 1;
    Terrain_Drop(x0, x1);
    Terrain_MarkDirty(x0, x1);

    PlayExplodeSfx(pW->nRadius);
    Particles_SpawnBurst((float)hitX, (float)hitY, 0xFFFF8C00u,
        20 + pW->nRadius / 3, 120.f + pW->nRadius * 2.f, 0.8f);
    Particles_SpawnDirt((float)hitX, (float)hitY,
        Terrain_GetGroundColor(), 10 + pW->nRadius / 5);

    nResults = Phys_DamagePlayers(hitX, hitY, pW->nRadius,
        s_players, s_nPlayerCount,
        s_players[s_nActive].nID,
        results, s_nPlayerCount);

    nDead = Phys_SettleAll(s_players, s_nPlayerCount,
        dead, s_nPlayerCount);

    for (i = 0; i < nDead; i++)
    {
        int j;
        Sfx_Play(Rand_Int(&s_rand, 2) ? SFX_TANK_DESTROYED_02
            : SFX_TANK_DESTROYED_01);
        DoRumble(65535, 40000, 500);
        for (j = 0; j < s_nPlayerCount; j++)
        {
            if (s_players[j].nID == dead[i])
            {
                Particles_SpawnBurst(
                    (float)(s_players[j].nX + s_players[j].nWidth / 2),
                    (float)(s_players[j].nY + s_players[j].nHeight / 2),
                    Player_GetColor(&s_players[j]), 30, 200.f, 1.2f);
                break;
            }
        }
    }
    (void)nResults;
}

static void ApplyExplosion(void)
{
    const WeaponDef* pW = &k_weapons[s_nWeaponID];

    if (pW->eKind == WKIND_LASER)
    {
        /* Laser: carve terrain + damage along beam */
        DamageResult results[GAME_PLAYER_COUNT];
        int dead[GAME_PLAYER_COUNT];
        int nDead, i;

        Weapon_ApplyLaser(s_nLaserX1, s_nLaserY1, s_nLaserAngle, pW->nBeamWidth);
        Terrain_Drop(0, (int)g_dwDisplayW - 1);

        Phys_DamageLaser(s_nLaserX1, s_nLaserY1, s_nLaserAngle,
            s_players, s_nPlayerCount,
            s_players[s_nActive].nID,
            pW->nBeamDamage, results, s_nPlayerCount);

        nDead = Phys_SettleAll(s_players, s_nPlayerCount,
            dead, s_nPlayerCount);
        for (i = 0; i < nDead; i++)
        {
            int j;
            Sfx_Play(Rand_Int(&s_rand, 2) ? SFX_TANK_DESTROYED_02
                : SFX_TANK_DESTROYED_01);
            DoRumble(65535, 40000, 500);
            for (j = 0; j < s_nPlayerCount; j++)
            {
                if (s_players[j].nID == dead[i])
                {
                    Particles_SpawnBurst(
                        (float)(s_players[j].nX + s_players[j].nWidth / 2),
                        (float)(s_players[j].nY + s_players[j].nHeight / 2),
                        Player_GetColor(&s_players[j]), 30, 200.f, 1.2f);
                    break;
                }
            }
        }

        /* Rumble for laser */
        DoRumble(20000, 40000, 300);
    }
    else
    {
        /* Standard explosion */
        WORD wLeft = (WORD)((float)pW->nRadius / 100.f * 55000.f);
        WORD wRight = (WORD)((float)pW->nRadius / 100.f * 20000.f);
        if (wLeft > 55000) wLeft = 55000;
        if (wRight > 20000) wRight = 20000;
        DoRumble(wLeft, wRight, 150 + pW->nRadius * 3);
        ApplyOneExplosion(s_nHitX, s_nHitY, s_nWeaponID);
    }
}

/* =========================================================================
   Start firing
========================================================================= */

static void DoFire(void)
{
    Player* pP = &s_players[s_nActive];
    const WeaponDef* pW;

    s_nWeaponID = pP->nActiveWeapon;
    pW = &k_weapons[s_nWeaponID];

    PlayFireSfx(s_nWeaponID);
    DoRumble(8000, 32000, 120);

    /* Clear tracer for new shot */
    s_nTracerLen = 0;

    /* Decrement ammo */
    if (!pW->bInfinite && pP->nWeapons[s_nWeaponID] > 0)
        pP->nWeapons[s_nWeaponID]--;

    if (pW->eKind == WKIND_LASER)
    {
        /* Laser is instant -- compute beam endpoints now */
        float angle = (float)pP->nAngle * 3.14159265f / 180.0f;
        float range = (float)(g_dwDisplayW > g_dwDisplayH
            ? g_dwDisplayW : g_dwDisplayH) * 1.5f;
        s_nLaserX1 = Player_TurretX(pP, 1.0f);
        s_nLaserY1 = Player_TurretY(pP, 1.0f);
        s_nLaserX2 = (int)((float)s_nLaserX1 + range * (float)cos(angle));
        s_nLaserY2 = (int)((float)s_nLaserY1 - range * (float)sin(angle));
        s_nLaserAngle = pP->nAngle;
        s_nHitX = s_nLaserX1;
        s_nHitY = s_nLaserY1;
        s_nExplFrame = 0;
        s_nExplRadius = 0;
        s_fExplX = (float)s_nLaserX1;
        s_fExplY = (float)s_nLaserY1;
        s_ePhase = GPHASE_LASER;
        s_dwPhaseStart = GetTickCount();
    }
    else
    {
        Phys_InitProjectile(&s_proj, pP);
        s_bHit = 0;
        s_ePhase = GPHASE_FIRING;
        s_dwPhaseStart = GetTickCount();
    }
}

/* =========================================================================
   Advance to next turn
========================================================================= */

static void NextTurn(const char* pszMsg)
{
    int next;

    /* Copy message */
    {
        const char* src = pszMsg ? pszMsg : "";
        int i;
        for (i = 0; i < 63 && src[i]; i++) s_szResult[i] = src[i];
        s_szResult[i] = '\0';
    }

    if (CountAlive() <= 1)
    {
        /* Round over */
        Sfx_Stop(SFX_BATTLEFIELD_LOOP);
        s_bRoundOver = 1;
        s_ePhase = GPHASE_ROUND_PAUSE;
        s_dwPhaseStart = GetTickCount();
        return;
    }

    next = NextAlive();
    if (next < 0) { s_bRoundOver = 1; return; }
    s_nActive = next;

    if (s_players[s_nActive].bAI)
    {
        s_ePhase = GPHASE_AI_DELAY;
        s_dwPhaseStart = GetTickCount();
    }
    else
    {
        s_ePhase = GPHASE_AIMING;
    }
}

/* =========================================================================
   Game_Init
========================================================================= */

void Game_PreInit(void)
{
    /* Partial init: human player only, for pre-game store.
       Called from Store_Init when entering store before first game. */
    Rand_Init(&s_rand, GetTickCount());
    Particles_Init();
    Terrain_Init();
    Tanks_Init();
    Player_Init(&s_players[0], 0, "XBOX",
        g_setup.nTankType, 0, 0);
    s_players[0].nColorIdx = g_setup.nColorIdx;
    s_nPlayerCount = g_setup.nAICount + GAME_HUMAN_COUNT;
    s_nRound = 0;
    s_bGameOver = 0;
    s_bActive = 0;   /* not active until Game_Init */
}

void Game_Init(void)
{
    int i;
    const char* aiNames[3] = { "Shooter", "Cyborg", "Killer" };

    /* If Game_PreInit was already called the human player is set up --
       only re-init subsystems that haven't been initialised yet.      */
    if (!s_bActive)
    {
        Rand_Init(&s_rand, GetTickCount());
        Particles_Init();
        Terrain_Init();
        Tanks_Init();
        /* Human player -- preserve inventory from store if PreInit ran */
        if (s_players[0].nCash == 0)
            Player_Init(&s_players[0], 0, "XBOX",
                g_setup.nTankType, 0, 0);
    }

    /* AI players from g_setup */
    for (i = 0; i < g_setup.nAICount; i++)
    {
        int diff = g_setup.nAIDiff[i];
        Player_Init(&s_players[1 + i], 1 + i,
            aiNames[diff],
            (1 + i) % TANK_TYPE_COUNT,
            1, diff);
    }

    s_nPlayerCount = g_setup.nAICount + GAME_HUMAN_COUNT;
    s_nRound = 0;
    s_bGameOver = 0;
    s_bMusicStarted = 0;
    s_bActive = 1;

    Game_NewRound();
}

Player* Game_GetPlayer(int idx)
{
    if (idx < 0 || idx >= s_nPlayerCount) return NULL;
    return &s_players[idx];
}

int Game_GetPlayerCount(void)
{
    return s_nPlayerCount;
}

int Game_GetRound(void)
{
    return s_nRound;
}

void Game_StopRumble(void)
{
    SetRumble(0, 0);
    s_dwRumbleEnd = 0;
}

/* =========================================================================
   Game_NewRound
========================================================================= */

void Game_NewRound(void)
{
    int i;

    s_nRound++;
    s_bRoundOver = 0;
    s_szResult[0] = '\0';

    for (i = 0; i < s_nPlayerCount; i++)
        Player_ResetForRound(&s_players[i], s_nPlayerCount);

    Terrain_NewRound();

    s_nWind = Rand_Int(&s_rand, MAX_WIND * 2 + 1) - MAX_WIND;
    Player_PlaceAll(s_players, s_nPlayerCount, &s_rand);

    Terrain_Upload();

    Particles_Clear();
    Sfx_Play(SFX_BATTLEFIELD_LOOP);


    /* First player's turn */
    s_nActive = 0;
    s_ePhase = s_players[0].bAI ? GPHASE_AI_DELAY : GPHASE_AIMING;
    s_dwPhaseStart = GetTickCount();
    s_dwAimTime = GetTickCount();
}

/* =========================================================================
   Game_Shutdown
========================================================================= */

void Game_Shutdown(void)
{
    Sfx_Stop(SFX_BATTLEFIELD_LOOP);
    SetRumble(0, 0);
    Tanks_Shutdown();
    Terrain_Shutdown();
    Particles_Clear();

    /* Reset all state so Game_Init starts clean on re-entry */
    s_nRound = 0;
    s_bRoundOver = 0;
    s_bGameOver = 0;
    s_bMusicStarted = 0;
    s_bHit = 0;
    s_nExplFrame = 0;
    s_dwRumbleEnd = 0;
    s_ePhase = GPHASE_AIMING;
    s_szResult[0] = '\0';
}

/* =========================================================================
   Game_Update
========================================================================= */

void Game_Update(WORD wPressed)
{
    DWORD now = GetTickCount();

    /* Start music on first update -- deferred to avoid blocking State_Enter */
    if (!s_bMusicStarted)
    {
        Audio_MusicPlayGameplay();
        s_bMusicStarted = 1;
    }
    if (s_dwRumbleEnd && now >= s_dwRumbleEnd)
    {
        SetRumble(0, 0);
        s_dwRumbleEnd = 0;
    }

    switch (s_ePhase)
    {
        /* ── AIMING ── human adjusts angle/power, AI computes shot ── */
    case GPHASE_AIMING:
    {
        Player* pP = &s_players[s_nActive];

        /* Rate-limited continuous aim for held buttons */
        if (now - s_dwAimTime >= AIM_RATE_MS)
        {
            WORD wRaw = GetButtons();
            if (wRaw & BTN_DPAD_LEFT) { pP->nAngle--; if (pP->nAngle < 0)   pP->nAngle = 0; }
            if (wRaw & BTN_DPAD_RIGHT) { pP->nAngle++; if (pP->nAngle > 179) pP->nAngle = 179; }
            if (wRaw & BTN_DPAD_UP) { pP->nPower += 10; if (pP->nPower > pP->nPowerLimit) pP->nPower = pP->nPowerLimit; }
            if (wRaw & BTN_DPAD_DOWN) { pP->nPower -= 10; if (pP->nPower < PHYS_MIN_POWER)  pP->nPower = PHYS_MIN_POWER; }
            s_dwAimTime = now;
        }

        /* Weapon cycle (White = prev, Black = next) */
        if (wPressed & BTN_WHITE)
        {
            pP->nActiveWeapon = (pP->nActiveWeapon - 1 + WEAPON_COUNT) % WEAPON_COUNT;
            while (pP->nWeapons[pP->nActiveWeapon] <= 0 && pP->nActiveWeapon != 0)
                pP->nActiveWeapon = (pP->nActiveWeapon - 1 + WEAPON_COUNT) % WEAPON_COUNT;
        }
        if (wPressed & BTN_BLACK)
        {
            pP->nActiveWeapon = (pP->nActiveWeapon + 1) % WEAPON_COUNT;
            while (pP->nWeapons[pP->nActiveWeapon] <= 0 && pP->nActiveWeapon != 0)
                pP->nActiveWeapon = (pP->nActiveWeapon + 1) % WEAPON_COUNT;
        }

        /* Fire */
        if (wPressed & BTN_A)
            DoFire();

        break;
    }

    /* ── AI_DELAY ── brief pause then AI computes and fires ── */
    case GPHASE_AI_DELAY:
    {
        if (now - s_dwPhaseStart >= AI_DELAY_MS)
        {
            Player* pP = &s_players[s_nActive];
            AIShot  shot;
            AI_Think(pP, s_players, s_nPlayerCount,
                (float)s_nWind, &s_rand, &shot);
            pP->nAngle = shot.nAngle;
            pP->nPower = shot.nPower;
            pP->nActiveWeapon = shot.nWeaponID;
            s_ePhase = GPHASE_AIMING;   /* show result briefly */
            s_dwPhaseStart = now;
            /* Immediately fire after showing aim */
            DoFire();
        }
        break;
    }

    /* ── FIRING ── advance projectile each frame ── */
    case GPHASE_FIRING:
    {
        int x, y, hitX, hitY;
        int prevX = s_proj.nPrevX;
        int prevY = s_proj.nPrevY;
        int W = (int)g_dwDisplayW;

        Phys_StepProjectile(&s_proj);
        Phys_GetPos(&s_proj, (float)s_nWind, &x, &y);

        /* Wall handling before escape check */
        if (g_cfg.nWallType == 1)   /* wrap */
        {
            if (x < 0) { Phys_WrapX(&s_proj, x + W, y); x += W; }
            else if (x >= W) { Phys_WrapX(&s_proj, x - W, y); x -= W; }
        }
        else if (g_cfg.nWallType == 2)   /* bounce */
        {
            if (x < 0 || x >= W)
            {
                int cx = x < 0 ? 0 : W - 1;
                Phys_BounceX(&s_proj, cx, y);
                x = cx;
            }
        }

        /* Escape checks (top/bottom only for wrap/bounce) */
        if (Phys_HasEscaped(&s_proj, x, y))
        {
            if (g_cfg.nWallType != 0)
            {
                /* Only escape on top/bottom for wrap/bounce modes */
                if (y > -(int)g_dwDisplayH / 2 && x >= 0 && x < W)
                    goto firing_continue;
            }
            NextTurn("Missed.");
            break;
        }
    firing_continue:

        /* Record tracer point */
        if (s_nTracerLen < TRACER_MAX)
        {
            s_tracerX[s_nTracerLen] = x;
            s_tracerY[s_nTracerLen] = y;
            s_nTracerLen++;
        }

        /* Bottom of screen */
        if (y >= (int)g_dwDisplayH)
        {
            y = (int)g_dwDisplayH - 1; s_bHit = 1; s_nHitX = x; s_nHitY = y;
        }

        /* Terrain/tank collision */
        if (!s_bHit && s_proj.nStep > 0 && y >= 0 && prevY >= 0)
        {
            if (Phys_IntersectShot(prevX, prevY, x, y,
                s_players, s_nPlayerCount,
                &hitX, &hitY))
            {
                s_bHit = 1; s_nHitX = hitX; s_nHitY = hitY;
                Sfx_Play(Rand_Int(&s_rand, 2) ? SFX_IMPACT_DIRT_02
                    : SFX_IMPACT_DIRT_01);
            }
        }

        if (s_bHit)
        {
            const WeaponDef* pW = &k_weapons[s_nWeaponID];

            /* MIRV / Funky: spawn animated sub-projectiles */
            if (pW->eKind == WKIND_MIRV || pW->eKind == WKIND_FUNKY)
            {
                int   n = pW->nParticles < SUB_MAX ? pW->nParticles : SUB_MAX;
                int   si;
                float baseSpeed = 180.f + (float)pW->nRadius * 1.5f;

                s_nSubCount = n;
                for (si = 0; si < n; si++)
                {
                    float vx, vy;
                    s_subDone[si] = 0;
                    if (pW->eKind == WKIND_MIRV)
                    {
                        /* V-spread: -2,-1,0,1,2 slots */
                        float slot = (float)(si - n / 2);
                        vx = slot * baseSpeed * 0.4f;
                        vy = baseSpeed * 0.6f;
                    }
                    else
                    {
                        /* Funky: random hemisphere */
                        float angle = (float)Rand_Int(&s_rand, 314) * 0.01f;
                        vx = (float)cos(angle) * baseSpeed;
                        vy = (float)sin(angle) * baseSpeed * 0.5f + baseSpeed * 0.3f;
                    }
                    Phys_InitSub(&s_subProj[si], s_nHitX, s_nHitY, vx, vy);
                }
                s_ePhase = GPHASE_SUBFIRING;
                s_dwPhaseStart = now;
            }
            else
            {
                s_nExplRadius = pW->nRadius;
                s_fExplX = (float)s_nHitX;
                s_fExplY = (float)s_nHitY;
                s_nExplFrame = 0;
                s_ePhase = GPHASE_EXPLODING;
                s_dwPhaseStart = now;
            }
        }
        else
        {
            s_proj.nPrevX = x;
            s_proj.nPrevY = y;
        }
        break;
    }

    /* ── SUBFIRING ── animate MIRV/Funky sub-shots ── */
    case GPHASE_SUBFIRING:
    {
        int   si, allDone = 1;
        float wind = (float)s_nWind;

        for (si = 0; si < s_nSubCount; si++)
        {
            int x, y, hitX, hitY;
            int prevX, prevY;

            if (s_subDone[si]) continue;
            allDone = 0;

            prevX = s_subProj[si].nPrevX;
            prevY = s_subProj[si].nPrevY;

            Phys_StepProjectile(&s_subProj[si]);
            Phys_GetPos(&s_subProj[si], wind, &x, &y);

            if (Phys_HasEscaped(&s_subProj[si], x, y) || y >= (int)g_dwDisplayH)
            {
                if (y >= (int)g_dwDisplayH) y = (int)g_dwDisplayH - 1;
                s_subHitX[si] = x;
                s_subHitY[si] = y;
                s_subDone[si] = 1;
                continue;
            }

            if (s_subProj[si].nStep > 0 && y >= 0 && prevY >= 0)
            {
                if (Phys_IntersectShot(prevX, prevY, x, y,
                    s_players, s_nPlayerCount,
                    &hitX, &hitY))
                {
                    s_subHitX[si] = hitX;
                    s_subHitY[si] = hitY;
                    s_subDone[si] = 1;
                    Sfx_Play(Rand_Int(&s_rand, 2) ? SFX_IMPACT_DIRT_02
                        : SFX_IMPACT_DIRT_01);
                    continue;
                }
            }

            s_subProj[si].nPrevX = x;
            s_subProj[si].nPrevY = y;
        }

        if (allDone)
        {
            /* Apply all sub-explosions */
            int si2;
            for (si2 = 0; si2 < s_nSubCount; si2++)
                ApplyOneExplosion(s_subHitX[si2], s_subHitY[si2], s_nWeaponID);
            Terrain_Upload();
            NextTurn("Turn complete.");
        }
        break;
    }

    /* ── LASER ── brief beam display then apply ── */
    case GPHASE_LASER:
    {
        s_nExplFrame++;
        if (s_nExplFrame >= EXPL_FRAMES)
        {
            ApplyExplosion();
            Terrain_Upload();
            NextTurn("Turn complete.");
        }
        break;
    }

    /* ── EXPLODING ── animation then apply effects ── */
    case GPHASE_EXPLODING:
    {
        s_nExplFrame++;
        if (s_nExplFrame >= EXPL_FRAMES)
        {
            ApplyExplosion();
            Terrain_Upload();
            NextTurn("Turn complete.");
        }
        break;
    }

    /* ── ROUND_PAUSE ── wait then signal round over ── */
    case GPHASE_ROUND_PAUSE:
    {
        Particles_Update(0.016f);
        if (now - s_dwPhaseStart >= ROUND_PAUSE_MS)
            s_bRoundOver = 1;
        break;
    }
    }

    /* Particle physics (all phases except ROUND_PAUSE which does it above) */
    if (s_ePhase != GPHASE_ROUND_PAUSE)
        Particles_Update(0.016f);
}

/* =========================================================================
   DrawExplosionRing  -- expanding circle overlay during GPHASE_EXPLODING
========================================================================= */

#define EXPL_FVF  ( D3DFVF_XYZRHW | D3DFVF_DIFFUSE )
typedef struct { float x, y, z, rhw; DWORD c; } EV;

static void DrawExplosionRing(void)
{
    EV    verts[26];   /* center + 24 segments + wrap */
    float t = (float)s_nExplFrame / (float)EXPL_FRAMES;
    float r = t * (float)s_nExplRadius;
    float alpha = t < 0.5f ? 1.0f : 2.0f * (1.0f - t);
    DWORD col;
    DWORD da;
    int   i;

    /* Colour: bright orange fading to yellow then transparent */
    {
        int ia = (int)(alpha * 230.f);
        int ir = 255;
        int ig = (int)(100.f + 155.f * t);
        if (ig > 255) ig = 255;
        col = ((DWORD)ia << 24) | ((DWORD)ir << 16) | ((DWORD)ig << 8);
    }
    da = ((DWORD)(alpha * 180.f)) << 24;

    /* Centre vertex (slightly transparent) */
    verts[0].x = s_fExplX; verts[0].y = s_fExplY;
    verts[0].z = 0.f; verts[0].rhw = 1.f; verts[0].c = da;

    for (i = 1; i <= 25; i++)
    {
        float a = (float)(i - 1) / 24.f * 6.28318f;
        verts[i].x = s_fExplX + (float)cos(a) * r;
        verts[i].y = s_fExplY + (float)sin(a) * r;
        verts[i].z = 0.f;
        verts[i].rhw = 1.f;
        verts[i].c = col;
    }

    g_pd3dDevice->SetVertexShader(EXPL_FVF);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 24, verts, sizeof(EV));
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   DrawTracer -- fading dot trail from previous shot (shown during aiming)
========================================================================= */

static void DrawTracer(void)
{
    typedef struct { float x, y, z, rhw; DWORD c; } TV;
    static TV buf[TRACER_MAX];
    int i, n = s_nTracerLen;
    float fPtSize = 2.0f;

    if (n < 2) return;

    for (i = 0; i < n; i++)
    {
        int   alpha = 30 + (int)(180.f * (float)i / (float)(n - 1));
        buf[i].x = (float)s_tracerX[i];
        buf[i].y = (float)s_tracerY[i];
        buf[i].z = 0.f;
        buf[i].rhw = 1.f;
        buf[i].c = ((DWORD)alpha << 24) | 0xFFFFFFu;
    }

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_POINTSIZE, *(DWORD*)&fPtSize);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_POINTLIST, (UINT)n, buf, sizeof(TV));
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   DrawLaserBeam -- bright fading line during GPHASE_LASER
========================================================================= */

static void DrawLaserBeam(void)
{
    typedef struct { float x, y, z, rhw; DWORD c; } LV;
    float frac = 1.f - (float)s_nExplFrame / (float)EXPL_FRAMES;
    int   alpha = (int)(frac * 255.f);
    DWORD headCol = ((DWORD)alpha << 24) | 0xFF88FFFFu;
    DWORD tailCol = ((DWORD)(alpha / 4) << 24) | 0xFF0044FFu;
    LV seg[2];

    seg[0].x = (float)s_nLaserX1; seg[0].y = (float)s_nLaserY1; seg[0].z = 0.f; seg[0].rhw = 1.f; seg[0].c = headCol;
    seg[1].x = (float)s_nLaserX2; seg[1].y = (float)s_nLaserY2; seg[1].z = 0.f; seg[1].rhw = 1.f; seg[1].c = tailCol;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_LINELIST, 1, seg, sizeof(LV));
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   HUD helpers
========================================================================= */

static void DrawWindIndicator(void)
{
    float cx = (float)g_dwDisplayW * 0.5f;
    float y = 8.f;
    Font* pFont = UI_GetFontSmall();
    char  buf[16];
    int   i;
    const char* arrow = s_nWind < 0 ? "<<< " : ">>> ";

    if (s_nWind == 0) return;

    /* Arrow string */
    {
        char* dst = buf;
        int   mag = s_nWind < 0 ? -s_nWind : s_nWind;
        if (s_nWind < 0) { *dst++ = '<'; *dst++ = '<'; *dst++ = ' '; }
        *dst++ = (char)('0' + (mag / 10) % 10);
        *dst++ = (char)('0' + mag % 10);
        if (s_nWind > 0) { *dst++ = ' '; *dst++ = '>'; *dst++ = '>'; }
        *dst = '\0';
    }
    {
        float tw = Font_Width(pFont, buf);
        Font_Draw(pFont, buf, cx - tw * 0.5f, y, 0xFFCCCCCC);
    }
}

static void DrawHUD(void)
{
    Font* pSmall = UI_GetFontSmall();
    float dw = (float)g_dwDisplayW;
    float dh = (float)g_dwDisplayH;
    Player* pP = &s_players[s_nActive];
    char    buf[64];
    float   tw;
    int     i;

    /* Wind -- centred at very top */
    DrawWindIndicator();

    /* Player health bars + names
       Layout: name at y=16, bar at y=30 (10px tall) */
    {
        float barW = dw / (float)s_nPlayerCount - 6.f;
        float nameY = 52.f;
        float barY = 68.f;
        float barH = 10.f;

        for (i = 0; i < s_nPlayerCount; i++)
        {
            float bx = 3.f + (float)i * (barW + 6.f);
            float ratio = s_players[i].bAlive
                ? (float)s_players[i].nPowerLimit / 1000.f
                : 0.f;
            DWORD col = Player_GetColor(&s_players[i]);

            /* Name above bar */
            Font_Draw(pSmall, s_players[i].szName, bx, nameY,
                s_players[i].bAlive ? col : 0xFF444444u);

            /* Bar background */
            {
                typedef struct { float x, y, z, rhw; DWORD c; } HV;
                HV vb[4];
                DWORD bg = 0xFF222222u;
                vb[0].x = bx;      vb[0].y = barY;      vb[0].z = 0; vb[0].rhw = 1; vb[0].c = bg;
                vb[1].x = bx + barW; vb[1].y = barY;      vb[1].z = 0; vb[1].rhw = 1; vb[1].c = bg;
                vb[2].x = bx;      vb[2].y = barY + barH; vb[2].z = 0; vb[2].rhw = 1; vb[2].c = bg;
                vb[3].x = bx + barW; vb[3].y = barY + barH; vb[3].z = 0; vb[3].rhw = 1; vb[3].c = bg;
                g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
                g_pd3dDevice->SetTexture(0, NULL);
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
                g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vb, sizeof(HV));
                /* Fill */
                vb[0].c = vb[1].c = vb[2].c = vb[3].c = col;
                vb[1].x = vb[3].x = bx + barW * ratio;
                g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vb, sizeof(HV));
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
            }
        }
    }

    /* Bottom HUD -- weapon / ANG+PWR / result, well spaced */
    if (s_ePhase == GPHASE_AIMING || s_ePhase == GPHASE_AI_DELAY)
    {
        /* Weapon name + stock */
        {
            char wbuf[32];
            int  wid = pP->nActiveWeapon;
            int  j = 0;
            const char* wn = k_weapons[wid].pszName;
            while (wn[j] && j < 24) { wbuf[j] = wn[j]; j++; }
            if (!k_weapons[wid].bInfinite)
            {
                int qty = pP->nWeapons[wid];
                wbuf[j++] = ' '; wbuf[j++] = '(';
                if (qty >= 100) wbuf[j++] = (char)('0' + qty / 100);
                if (qty >= 10) wbuf[j++] = (char)('0' + (qty / 10) % 10);
                wbuf[j++] = (char)('0' + qty % 10);
                wbuf[j++] = ')';
            }
            wbuf[j] = '\0';
            tw = Font_Width(pSmall, wbuf);
            Font_Draw(pSmall, wbuf, (dw - tw) * 0.5f, dh - 68.f, Player_GetColor(pP));
        }

        /* Angle / Power */
        {
            char a[4], p[5];
            int ang = pP->nAngle, pow = pP->nPower;
            a[0] = (char)('0' + ang / 100); a[1] = (char)('0' + (ang / 10) % 10); a[2] = (char)('0' + ang % 10); a[3] = '\0';
            p[0] = (char)('0' + pow / 1000); p[1] = (char)('0' + (pow / 100) % 10);
            p[2] = (char)('0' + (pow / 10) % 10); p[3] = (char)('0' + pow % 10); p[4] = '\0';
            buf[0] = 'A'; buf[1] = 'N'; buf[2] = 'G'; buf[3] = ':'; buf[4] = ' ';
            buf[5] = a[0]; buf[6] = a[1]; buf[7] = a[2]; buf[8] = ' ';
            buf[9] = 'P'; buf[10] = 'W'; buf[11] = 'R'; buf[12] = ':'; buf[13] = ' ';
            buf[14] = p[0]; buf[15] = p[1]; buf[16] = p[2]; buf[17] = p[3]; buf[18] = '\0';
            tw = Font_Width(pSmall, buf);
            Font_Draw(pSmall, buf, (dw - tw) * 0.5f, dh - 46.f, 0xFFFFFFFF);
        }
    }

    /* Result message */
    if (s_szResult[0])
    {
        tw = Font_Width(pSmall, s_szResult);
        Font_Draw(pSmall, s_szResult, (dw - tw) * 0.5f, dh - 22.f, 0xFFFFCC00);
    }
}

/* =========================================================================
   DrawProjectile -- white 3px dot at current position
========================================================================= */

static void DrawProjectile(void)
{
    typedef struct { float x, y, z, rhw; DWORD c; } ProjV;
    float fPtSize = 3.0f;
    int x, y;
    ProjV v;

    Phys_GetPos(&s_proj, (float)s_nWind, &x, &y);
    v.x = (float)x; v.y = (float)y; v.z = 0.f; v.rhw = 1.f;
    v.c = 0xFFFFFFFF;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_POINTSIZE, *(DWORD*)&fPtSize);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_POINTLIST, 1, &v, sizeof(ProjV));
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* =========================================================================
   Game_Draw
========================================================================= */

void Game_Draw(void)
{
    Terrain_Upload();
    Terrain_Draw();
    Tanks_DrawAll(s_players, s_nPlayerCount);

    /* Tracer trail -- shown while aiming (human player only) */
    if ((s_ePhase == GPHASE_AIMING || s_ePhase == GPHASE_AI_DELAY)
        && !s_players[s_nActive].bAI)
        DrawTracer();

    if (s_ePhase == GPHASE_FIRING)
        DrawProjectile();

    /* Sub-shot dots */
    if (s_ePhase == GPHASE_SUBFIRING)
    {
        typedef struct { float x, y, z, rhw; DWORD c; } PV;
        float fSz = 3.0f;
        int si;
        for (si = 0; si < s_nSubCount; si++)
        {
            int x, y;
            PV v;
            if (s_subDone[si]) continue;
            Phys_GetPos(&s_subProj[si], (float)s_nWind, &x, &y);
            v.x = (float)x; v.y = (float)y; v.z = 0.f; v.rhw = 1.f; v.c = 0xFFFFFFFFu;
            g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pd3dDevice->SetTexture(0, NULL);
            g_pd3dDevice->SetRenderState(D3DRS_POINTSIZE, *(DWORD*)&fSz);
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_pd3dDevice->DrawPrimitiveUP(D3DPT_POINTLIST, 1, &v, sizeof(PV));
            g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        }
    }

    if (s_ePhase == GPHASE_LASER)
        DrawLaserBeam();

    if (s_ePhase == GPHASE_EXPLODING)
        DrawExplosionRing();

    Particles_Draw();
    DrawHUD();
}

/* =========================================================================
   Queries
========================================================================= */

int Game_IsRoundOver(void) { return s_bRoundOver; }
int Game_IsGameOver(void) { return s_bGameOver; }
int Game_IsActive(void) { return s_bActive; }