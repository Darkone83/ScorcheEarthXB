/*---------------------------------------------------------------------------
    ScorchedXB - ai.cpp
    AI turn logic ported from scorch.js.

    Key functions and their JS equivalents:
        AI_Think            takeAiTurn
        ChooseWeapon        chooseAiWeapon
        SimulateMissile     simulateAiMissile
        ScoreHit            scoreAiHit
        SearchCandidates    searchAiCandidates
        ApplyInaccuracy     applyAiInaccuracy
        FallbackShot        closestOpponentFallbackShot

    Weapon choice uses the same Gaussian price-curve weighting as scorch.js.
    Missile simulation uses the same projectilePoint equations as physics.cpp.
    Self-damage penalty = 8x (MAX_PLAYERS_PENALTY in scorch.js).
---------------------------------------------------------------------------*/

#include <math.h>
#include "ai.h"
#include "physics.h"
#include "terrain.h"
#include "render.h"    /* g_dwDisplayW, g_dwDisplayH */

/* =========================================================================
   Constants matching scorch.js
========================================================================= */

static const int   k_accuracy[3] = { 5,    4,    2 };
static const float k_radFactor[3] = { 3.0f, 2.0f, 1.5f };
static const float k_wepCenter[3] = { 0.12f,0.52f,0.86f };
static const float k_wepSpread[3] = { 0.34f,0.36f,0.28f };
static const int   k_angleSpread[3] = { 3,    2,    0 };
static const int   k_powerSpread[3] = { 35,   18,   0 };

#define AI_SELF_PENALTY  8
#define AI_MAX_STEPS     560
#define AI_STEP_SIZE     4

/* =========================================================================
   Effective weapon radius for AI scoring  (aiWeaponRadius in scorch.js)
========================================================================= */

static int AiWeaponRadius(const WeaponDef* pW)
{
    int r = pW->nRadius;
    if (pW->eKind == WKIND_MIRV)
    {
        float scale = 1.0f + (float)pW->nParticles * 0.24f;
        if (scale > 3.2f) scale = 3.2f;
        return (int)((float)r * scale);
    }
    if (pW->eKind == WKIND_NAPALM)
        return (int)((float)r * 0.45f);
    if (pW->eKind == WKIND_DIGGER)
    {
        int dr = (int)((float)r * 0.9f);
        return dr < 18 ? 18 : dr;
    }
    return r;
}

/* =========================================================================
   Fast missile simulation  (simulateAiMissile in scorch.js)
   Wall mode: none (no bounce/wrap -- matches most game sessions).
   Returns 1 on terrain/tank hit; fills *pHitX, *pHitY.
========================================================================= */

static int SimulateMissile(const Player* pP,
    int nAngle, int nPower, float fWind,
    const Player* pPlayers, int nCount,
    int* pHitX, int* pHitY)
{
    float angle = (float)nAngle * 3.14159265f / 180.0f;
    float speed = (float)nPower / 8.0f;
    float vx = speed * (float)cos(angle);
    float vy = speed * (float)sin(angle);
    float startX = (float)Player_TurretX(pP, 2.0f);
    float startY = (float)((int)g_dwDisplayH - Player_TurretY(pP, 2.0f));

    int   prevX = (int)startX;
    int   prevY = Player_TurretY(pP, 2.0f);
    int   step, x, y, W, H;

    W = (int)g_dwDisplayW;
    H = (int)g_dwDisplayH;

    for (step = AI_STEP_SIZE; step < AI_MAX_STEPS; step += AI_STEP_SIZE)
    {
        float t = (float)step * 0.1f;
        float worldY = startY + vy * t - 4.9f * t * t;

        x = (int)(startX + (fWind + vx) * t);
        y = (int)((float)H - worldY);

        if (x < 0 || x >= W) return 0;
        if (y >= H) { *pHitX = x; *pHitY = H - 1; return 1; }

        if (y >= 0 && prevY >= 0)
        {
            if (Phys_IntersectShot(prevX, prevY, x, y,
                pPlayers, nCount,
                pHitX, pHitY)) return 1;
        }
        prevX = x;
        prevY = y;
    }
    return 0;
}

/* =========================================================================
   Score a hit position  (scoreAiHit in scorch.js)
   Enemy damage: positive.  Self damage: penalised x8.
========================================================================= */

static int ScoreHit(int hx, int hy, int nRadius,
    const Player* pShooter,
    const Player* pPlayers, int nCount)
{
    int score = 0;
    int i;

    for (i = 0; i < nCount; i++)
    {
        const Player* pT = &pPlayers[i];
        int dmg;
        if (!pT->bAlive) continue;
        dmg = Phys_RoundDamage(pT, hx, hy, nRadius);
        if (pT->nID == pShooter->nID)
            score -= dmg * AI_SELF_PENALTY;
        else
            score += dmg;
    }
    return score;
}

/* =========================================================================
   Grid search  (searchAiCandidates in scorch.js)
   Sweeps angle x power grid; returns best (angle, power, score).
========================================================================= */

typedef struct { int nAngle; int nPower; int nScore; } AiBest;

static AiBest SearchCandidates(const Player* pP,
    int nRadius, float fWind,
    int nAngleCenter, int nAngleSpan, int nAngleStep,
    int nPowerCenter, int nPowerSpan, int nPowerStep,
    const Player* pPlayers, int nCount,
    AiBest prev)
{
    int angleStart = nAngleCenter - nAngleSpan / 2;
    int angleEnd = nAngleCenter + nAngleSpan / 2;
    int powerStart = nPowerCenter - nPowerSpan / 2;
    int powerEnd = nPowerCenter + nPowerSpan / 2;
    int angle, power, hx, hy, score;
    AiBest best = prev;

    if (angleStart < 0) angleStart = 0;
    if (angleEnd > 179) angleEnd = 179;
    if (powerStart < 0) powerStart = 0;
    if (powerEnd > pP->nPowerLimit) powerEnd = pP->nPowerLimit;
    if (nAngleStep < 1) nAngleStep = 1;
    if (nPowerStep < 1) nPowerStep = 1;

    for (angle = angleStart; angle <= angleEnd; angle += nAngleStep)
    {
        for (power = powerStart; power <= powerEnd; power += nPowerStep)
        {
            if (!SimulateMissile(pP, angle, power, fWind,
                pPlayers, nCount, &hx, &hy)) continue;
            score = ScoreHit(hx, hy, nRadius, pP, pPlayers, nCount);
            if (score > best.nScore)
            {
                best.nScore = score;
                best.nAngle = angle;
                best.nPower = power;
            }
        }
    }
    return best;
}

/* =========================================================================
   Inaccuracy  (applyAiInaccuracy in scorch.js)
   Uses simple Rand noise instead of the JS deterministic hash.
========================================================================= */

static void ApplyInaccuracy(const Player* pP, Rand* pRand,
    int* pAngle, int* pPower)
{
    int   t = pP->nAIType;
    int   aSpread = k_angleSpread[t];
    int   pSpread = k_powerSpread[t];
    float aNoise = (Rand_Float(pRand) * 2.0f - 1.0f) * (float)aSpread;
    float pNoise = (Rand_Float(pRand) * 2.0f - 1.0f) * (float)pSpread;
    int   angle = *pAngle + (int)aNoise;
    int   power = *pPower + (int)pNoise;

    if (angle < 0) angle = 0;
    if (angle > 179) angle = 179;
    if (power < 0) power = 0;
    if (power > pP->nPowerLimit) power = pP->nPowerLimit;

    *pAngle = angle;
    *pPower = power;
}

/* =========================================================================
   Fallback shot  (closestOpponentFallbackShot)
   Points directly at the closest alive opponent, estimates power.
========================================================================= */

static void FallbackShot(const Player* pP,
    const Player* pPlayers, int nCount,
    Rand* pRand,
    int* pAngle, int* pPower)
{
    int    i, best = -1;
    float  bestDist = 1e9f;
    float  sx, sy, tx, ty, dx, dy, dist, rawAngle;
    int    angle, power;

    sx = (float)(pP->nX + pP->nWidth / 2);
    sy = (float)(pP->nY + pP->nHeight / 2);

    for (i = 0; i < nCount; i++)
    {
        float ddx, ddy, d;
        if (!pPlayers[i].bAlive || pPlayers[i].nID == pP->nID) continue;
        ddx = (float)(pPlayers[i].nX + pPlayers[i].nWidth / 2) - sx;
        ddy = (float)(pPlayers[i].nY + pPlayers[i].nHeight / 2) - sy;
        d = ddx * ddx + ddy * ddy;
        if (d < bestDist) { bestDist = d; best = i; }
    }

    if (best < 0) { *pAngle = pP->nAngle; *pPower = pP->nPower; return; }

    tx = (float)(pPlayers[best].nX + pPlayers[best].nWidth / 2);
    ty = (float)(pPlayers[best].nY + pPlayers[best].nHeight / 2);
    dx = tx - (float)Player_TurretX(pP, 2.0f);
    dy = (float)Player_TurretY(pP, 2.0f) - ty;   /* scorch.js: startY - targetY */
    dist = (float)sqrt(dx * dx + dy * dy);

    rawAngle = (float)atan2((double)dy, (double)dx) * 180.0f / 3.14159265f;
    if (rawAngle < 0)
        angle = dx < 0 ? 179 : 0;
    else
    {
        angle = (int)rawAngle;
        if (angle < 0) angle = 0;
        if (angle > 179) angle = 179;
    }

    {
        int pNoise = Rand_Int(pRand, 161) - 80;
        int pEst = (int)(dist * 2.2f) + pNoise;
        if (pEst < 300) pEst = 300;
        power = pEst < pP->nPowerLimit ? pEst : pP->nPowerLimit;
    }

    *pAngle = angle;
    *pPower = power;
}

/* =========================================================================
   Weapon choice  (chooseAiWeapon in scorch.js)
   Gaussian weight over price curve, biased by ammo count.
========================================================================= */

static int ChooseWeapon(const Player* pP, Rand* pRand)
{
    float   center = k_wepCenter[pP->nAIType];
    float   spread = k_wepSpread[pP->nAIType];
    int     maxPrice = 1;
    int     i;
    float   total = 0.0f;
    float   weights[WEAPON_COUNT];
    int     indices[WEAPON_COUNT];
    int     nValid = 0;
    float   pick;

    /* Build valid weapon list and find max price */
    for (i = 0; i < WEAPON_COUNT; i++)
    {
        const WeaponDef* pW = &k_weapons[i];
        if (pW->eKind == WKIND_LASER) continue;   /* AI can't use laser */
        if (pP->nWeapons[i] <= 0) continue;
        if (pW->nPrice > maxPrice) maxPrice = pW->nPrice;
        indices[nValid++] = i;
    }

    if (nValid == 0) return 0;    /* always have missile */
    if (nValid == 1) return indices[0];

    /* Gaussian weight for each valid weapon */
    for (i = 0; i < nValid; i++)
    {
        const WeaponDef* pW = &k_weapons[indices[i]];
        int              qty = pP->nWeapons[indices[i]];
        float pricePos = (float)pW->nPrice / (float)maxPrice;
        float d = (pricePos - center) / spread;
        float fit = (float)exp(-0.5f * (double)(d * d));
        float ammoW;

        if (pW->bInfinite)
            ammoW = 1.0f;
        else
        {
            /* log2(qty+1) ammo weight, capped at 2.2 */
            float l = (float)(log((double)(qty + 1)) / log(2.0));
            ammoW = 0.75f + l * 0.18f;
            if (ammoW > 2.2f) ammoW = 2.2f;
        }

        if (fit < 0.04f) fit = 0.04f;
        weights[i] = fit * ammoW;
        total += weights[i];
    }

    if (total <= 0.0f) return indices[0];

    /* Weighted pick using RNG */
    pick = Rand_Float(pRand) * total;
    for (i = 0; i < nValid; i++)
    {
        pick -= weights[i];
        if (pick <= 0.0f) return indices[i];
    }
    return indices[nValid - 1];
}

/* =========================================================================
   AI_Think  (takeAiTurn in scorch.js)
========================================================================= */

void AI_Think(const Player* pAI,
    const Player* pAllPlayers, int nCount,
    float fWind,
    Rand* pRand,
    AIShot* pOut)
{
    int          t = pAI->nAIType;
    int          accuracy = k_accuracy[t];
    int          weaponID = ChooseWeapon(pAI, pRand);
    int          aiRadius = (int)((float)AiWeaponRadius(&k_weapons[weaponID])
        * k_radFactor[t]);
    int          coarseAS = accuracy * 3;
    int          coarsePS = accuracy * 30;
    AiBest       zero = { pAI->nAngle, pAI->nPower, 0 };
    AiBest       best, fine;
    int          angle, power;

    if (coarseAS < 6) coarseAS = 6;
    if (coarsePS < 60) coarsePS = 60;

    /* ── Pass 1: coarse full-range sweep ── */
    best = SearchCandidates(pAI, aiRadius, fWind,
        pAI->nAngle, 180, coarseAS,
        pAI->nPower, pAI->nPowerLimit, coarsePS,
        pAllPlayers, nCount, zero);

    if (best.nScore > 0)
    {
        /* ── Pass 2: fine sweep around best coarse result ── */
        fine = SearchCandidates(pAI, aiRadius, fWind,
            best.nAngle, coarseAS * 2, accuracy,
            best.nPower, coarsePS * 2, accuracy * 10,
            pAllPlayers, nCount, best);

        angle = fine.nAngle;
        power = fine.nPower;
        ApplyInaccuracy(pAI, pRand, &angle, &power);

        pOut->nAngle = angle;
        pOut->nPower = power;
        pOut->nWeaponID = weaponID;
        pOut->bFallback = 0;
    }
    else
    {
        /* No scoring shot found -- aim at closest opponent */
        FallbackShot(pAI, pAllPlayers, nCount, pRand, &angle, &power);
        pOut->nAngle = angle;
        pOut->nPower = power;
        pOut->nWeaponID = ChooseWeapon(pAI, pRand);
        pOut->bFallback = 1;
    }
}