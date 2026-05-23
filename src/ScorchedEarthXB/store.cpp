/*---------------------------------------------------------------------------
    ScorchedXB - store.cpp
    Weapon store.  Operates on Game_GetPlayer(0) directly.
    A=buy  X=sell(50%)  B/Start=done.
---------------------------------------------------------------------------*/

#include "store.h"
#include "game.h"
#include "weapons.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "input.h"

#define STORE_VISIBLE  9

static int s_nSel = 0;
static int s_nScroll = 0;

/* =========================================================================
   Integer → string, no sprintf
========================================================================= */
static void IStr(int n, char* buf, int cap)
{
    char tmp[12];
    int  i = 0, j = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (n < 0) { if (j < cap - 1) buf[j++] = '-'; n = -n; }
    while (n > 0 && i < 12) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0 && j < cap - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* =========================================================================
   Store_Init / Update
========================================================================= */

void Store_Init(void)
{
    s_nSel = 0;
    s_nScroll = 0;
}

int Store_Update(WORD wPressed)
{
    Player* pP = Game_GetPlayer(0);
    if (!pP) return STORE_ACTION_DONE;

    if (wPressed & BTN_DPAD_DOWN)
    {
        if (s_nSel < WEAPON_COUNT - 1) s_nSel++;
        if (s_nSel >= s_nScroll + STORE_VISIBLE)
            s_nScroll = s_nSel - STORE_VISIBLE + 1;
    }
    if (wPressed & BTN_DPAD_UP)
    {
        if (s_nSel > 0) s_nSel--;
        if (s_nSel < s_nScroll) s_nScroll = s_nSel;
    }

    if (wPressed & BTN_A)
    {
        const WeaponDef* pW = &k_weapons[s_nSel];
        if (!pW->bInfinite && pP->nCash >= pW->nPrice)
        {
            pP->nCash -= pW->nPrice;
            pP->nWeapons[s_nSel]++;
        }
    }
    if (wPressed & BTN_X)
    {
        const WeaponDef* pW = &k_weapons[s_nSel];
        if (!pW->bInfinite && pP->nWeapons[s_nSel] > 0)
        {
            pP->nWeapons[s_nSel]--;
            pP->nCash += pW->nPrice / 2;
        }
    }

    if ((wPressed & BTN_B) || (wPressed & BTN_START))
        return STORE_ACTION_DONE;

    return STORE_ACTION_NONE;
}

/* =========================================================================
   Store_Draw
========================================================================= */

void Store_Draw(void)
{
    Font* pBig = UI_GetFontLarge();
    Font* pSmall = UI_GetFontSmall();
    float   dw = (float)g_dwDisplayW;
    float   dh = (float)g_dwDisplayH;
    Player* pP = Game_GetPlayer(0);
    char    buf[24];
    int     i;

    /* ── Background ──────────────────────────────────────────────── */
    UI_DrawUIBackground(220);

    /* ── Title ───────────────────────────────────────────────────── */
    {
        float tw = Font_Width(pBig, "ARMORY");
        Font_Draw(pBig, "ARMORY", (dw - tw) * 0.5f, dh * 0.03f, 0xFFFFCC00u);
    }

    /* ── Cash display ─────────────────────────────────────────────── */
    if (pP)
    {
        buf[0] = '$'; IStr(pP->nCash, buf + 1, 12);
        {
            float tw = Font_Width(pBig, buf);
            Font_Draw(pBig, buf, (dw - tw) * 0.5f, dh * 0.11f, 0xFF88FF88u);
        }
    }

    /* ── Column headers ───────────────────────────────────────────── */
    {
        float hy = dh * 0.20f;
        Font_Draw(pSmall, "WEAPON", dw * 0.04f, hy, 0xFFAA9977u);
        Font_Draw(pSmall, "COST", dw * 0.62f, hy, 0xFFAA9977u);
        Font_Draw(pSmall, "OWNED", dw * 0.82f, hy, 0xFFAA9977u);
    }

    /* ── Weapon rows ──────────────────────────────────────────────── */
    {
        float listY = dh * 0.26f;
        float rowH = (dh * 0.62f) / (float)STORE_VISIBLE;

        for (i = 0; i < STORE_VISIBLE; i++)
        {
            int   wid = i + s_nScroll;
            float iy = listY + (float)i * rowH;
            DWORD rbg, nc, vc;

            if (wid >= WEAPON_COUNT) break;

            rbg = (wid == s_nSel) ? 0xFF3A2800u : 0xFF181818u;
            nc = (wid == s_nSel) ? 0xFFFFCC00u : 0xFFCCCCCCu;
            vc = (wid == s_nSel) ? 0xFFFFFFFFu : 0xFF888888u;

            /* Row bg */
            {
                typedef struct { float x, y, z, rhw; DWORD c; }BV;
                BV v[4];
                v[0].x = dw * 0.02f; v[0].y = iy;       v[0].z = 0; v[0].rhw = 1; v[0].c = rbg;
                v[1].x = dw * 0.98f; v[1].y = iy;       v[1].z = 0; v[1].rhw = 1; v[1].c = rbg;
                v[2].x = dw * 0.02f; v[2].y = iy + rowH - 3; v[2].z = 0; v[2].rhw = 1; v[2].c = rbg;
                v[3].x = dw * 0.98f; v[3].y = iy + rowH - 3; v[3].z = 0; v[3].rhw = 1; v[3].c = rbg;
                g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
                g_pd3dDevice->SetTexture(0, NULL);
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
                g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BV));
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
            }

            /* Name */
            Font_Draw(pSmall, k_weapons[wid].pszName, dw * 0.04f, iy + 3.f, nc);

            /* Cost */
            if (k_weapons[wid].bInfinite)
                Font_Draw(pSmall, "FREE", dw * 0.62f, iy + 3.f, 0xFF88FF88u);
            else
            {
                buf[0] = '$'; IStr(k_weapons[wid].nPrice, buf + 1, 10);
                Font_Draw(pSmall, buf, dw * 0.62f, iy + 3.f, vc);
            }

            /* Owned qty */
            if (pP)
            {
                if (k_weapons[wid].bInfinite)
                    Font_Draw(pSmall, "inf", dw * 0.82f, iy + 3.f, 0xFF88FF88u);
                else
                {
                    IStr(pP->nWeapons[wid], buf, 8);
                    Font_Draw(pSmall, buf, dw * 0.82f, iy + 3.f, vc);
                }
            }

            /* Can't afford indicator */
            if (pP && !k_weapons[wid].bInfinite &&
                pP->nCash < k_weapons[wid].nPrice && wid == s_nSel)
                Font_Draw(pSmall, "NO FUNDS", dw * 0.82f, iy + 3.f, 0xFFFF4444u);
        }
    }

    /* ── Scroll hint ──────────────────────────────────────────────── */
    if (WEAPON_COUNT > STORE_VISIBLE)
    {
        char idx[4], tot[4], comb[12];
        int  j = 0, k = 0;
        IStr(s_nSel + 1, idx, 4);
        IStr(WEAPON_COUNT, tot, 4);
        while (idx[k]) comb[j++] = idx[k++];
        comb[j++] = '/'; k = 0;
        while (tot[k]) comb[j++] = tot[k++];
        comb[j] = '\0';
        {
            float tw = Font_Width(pSmall, comb);
            Font_Draw(pSmall, comb, (dw - tw) * 0.5f, dh * 0.91f, 0xFF555555u);
        }
    }

    /* ── Hints ────────────────────────────────────────────────────── */
    Font_Draw(pSmall, "A=Buy   X=Sell(50%)   B=Done", dw * 0.04f, dh * 0.94f, 0xFF555555u);
}