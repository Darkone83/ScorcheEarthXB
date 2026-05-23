/*---------------------------------------------------------------------------
    ScorchedXB - main.cpp
    Xbox entry point, top-level game state machine, main loop.

    State flow:
        STATE_VIDEO    Intro XMV, blocking, then auto-transitions to TITLE.
        STATE_TITLE    Splash screen, title music loops, waits for START.
        STATE_MENU     Main menu, title music continues.
        STATE_GAME     In-game, shuffled gameplay tracks.
        STATE_SHUTDOWN Clean exit.

    Fade system
    -----------
    All state transitions go through a two-phase fade:
        FADE_OUT  black overlay alpha 0→255  (FADE_MS ms)
        FADE_IN   black overlay alpha 255→0  (FADE_MS ms)
    State_Enter is called at full-black so the swap is invisible.
    Input is blocked during any active fade.
    StartFadeOut(targetState) triggers the sequence.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include "render.h"
#include "input.h"
#include "font.h"
#include "ui.h"
#include "video.h"
#include "audio.h"
#include "tex.h"
#include "menu.h"
#include "sfx.h"
#include "game.h"
#include "terrain.h"
#include "options.h"
#include "credits.h"
#include "help.h"
#include "config.h"
#include "setup.h"
#include "store.h"
#include "results.h"

/* =========================================================================
   Game state
========================================================================= */

typedef enum
{
    STATE_VIDEO = 0,
    STATE_TITLE,
    STATE_MENU,
    STATE_OPTIONS,
    STATE_HELP,
    STATE_SETUP,      /* pre-game: tank / color / AI config   */
    STATE_STORE,      /* weapon store (pre-game + between rounds) */
    STATE_GAME,
    STATE_RESULTS,    /* round / game-over results screen     */
    STATE_CREDITS,
    STATE_SHUTDOWN
} GameState;

static GameState s_eState = STATE_VIDEO;
static GameState s_ePrevState = STATE_VIDEO;
static WORD      s_wPrevButtons = 0;
static int       s_bResuming = 0;
static int       s_bStorePreGame = 1;   /* 1=pre-game loadout, 0=between rounds */
static int       s_bNewRound = 0;   /* set when entering game for next round */

static void State_Enter(GameState eNew);

/* =========================================================================
   Fade system
========================================================================= */

#define FADE_MS  350u

typedef enum { FADE_NONE = 0, FADE_OUT, FADE_IN } FadeMode;

static FadeMode   s_eFade = FADE_NONE;
static DWORD      s_dwFadeStart = 0;
static GameState  s_eFadeTarget = STATE_TITLE;

/* Fullscreen black quad at given alpha */
static void DrawFadeOverlay(BYTE nAlpha)
{
    typedef struct { float x, y, z, rhw; DWORD c; } FV;
    float w = (float)g_dwDisplayW;
    float h = (float)g_dwDisplayH;
    DWORD c = ((DWORD)nAlpha << 24);   /* black with alpha */
    FV    v[4];

    v[0].x = 0.f; v[0].y = 0.f; v[0].z = 0.f; v[0].rhw = 1.f; v[0].c = c;
    v[1].x = w;   v[1].y = 0.f; v[1].z = 0.f; v[1].rhw = 1.f; v[1].c = c;
    v[2].x = 0.f; v[2].y = h;   v[2].z = 0.f; v[2].rhw = 1.f; v[2].c = c;
    v[3].x = w;   v[3].y = h;   v[3].z = 0.f; v[3].rhw = 1.f; v[3].c = c;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FV));
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

/* Trigger a fade-out → [state switch at full black] → fade-in */
static void StartFadeOut(GameState eTarget)
{
    if (s_eFade != FADE_NONE) return;   /* ignore if already fading */
    s_eFade = FADE_OUT;
    s_dwFadeStart = GetTickCount();
    s_eFadeTarget = eTarget;
}

/* Call every frame before drawing; drives the fade state machine */
static void FadeUpdate(void)
{
    DWORD elapsed;

    if (s_eFade == FADE_NONE) return;

    elapsed = GetTickCount() - s_dwFadeStart;

    if (s_eFade == FADE_OUT && elapsed >= FADE_MS)
    {
        /* Full black reached -- swap state then start fade-in */
        State_Enter(s_eFadeTarget);
        s_eFade = FADE_IN;
        s_dwFadeStart = GetTickCount();
    }
    else if (s_eFade == FADE_IN && elapsed >= FADE_MS)
    {
        s_eFade = FADE_NONE;
    }
}

/* Call every frame after State_Draw to overlay the fade quad */
static void FadeDraw(void)
{
    DWORD elapsed;
    BYTE  alpha;

    if (s_eFade == FADE_NONE) return;

    elapsed = GetTickCount() - s_dwFadeStart;
    if (elapsed > FADE_MS) elapsed = FADE_MS;

    if (s_eFade == FADE_OUT)
        alpha = (BYTE)(elapsed * 255u / FADE_MS);
    else
        alpha = (BYTE)((FADE_MS - elapsed) * 255u / FADE_MS);

    DrawFadeOverlay(alpha);
}

/* =========================================================================
   Loading screen
========================================================================= */


static void ShowLoadingScreen(void)
{
    Font* pFont = Font_Load(NULL, 28);
    float tw;

    Render_BeginFrame(0xFF000000);

    if (pFont)
    {
        tw = Font_Width(pFont, "LOADING PLEASE WAIT");
        Font_Draw(pFont, "LOADING PLEASE WAIT",
            ((float)g_dwDisplayW - tw) * 0.5f,
            (float)g_dwDisplayH * 0.5f - 14.0f,
            0xFFFFFFFF);
        Font_Free(pFont);
    }

    Render_EndFrame();
}

/* =========================================================================
   Per-state update  (input blocked during any fade)
========================================================================= */

static void State_Update(WORD wPressed)
{
    if (s_eFade != FADE_NONE) return;

    switch (s_eState)
    {
    case STATE_TITLE:
        if (UI_TitleUpdate(wPressed))
            StartFadeOut(STATE_MENU);
        break;

    case STATE_MENU:
    {
        int nAction = Menu_Update(wPressed);
        if (nAction == MENU_ACTION_RESUME) { s_bResuming = 1; StartFadeOut(STATE_GAME); }
        if (nAction == MENU_ACTION_NEWGAME) { s_bResuming = 0; StartFadeOut(STATE_SETUP); }
        if (nAction == MENU_ACTION_OPTIONS) StartFadeOut(STATE_OPTIONS);
        if (nAction == MENU_ACTION_HELP) StartFadeOut(STATE_HELP);
        if (nAction == MENU_ACTION_EXIT) StartFadeOut(STATE_CREDITS);
        if (nAction == MENU_ACTION_BACK) StartFadeOut(STATE_TITLE);
        break;
    }

    case STATE_OPTIONS:
        if (Options_Update(wPressed) == OPTS_ACTION_BACK)
            StartFadeOut(STATE_MENU);
        break;

    case STATE_HELP:
        if (wPressed & BTN_B) StartFadeOut(STATE_MENU);
        break;

    case STATE_SETUP:
        if (Setup_Update(wPressed) == SETUP_ACTION_DONE)
        {
            s_bStorePreGame = 1;
            StartFadeOut(STATE_STORE);
        }
        break;

    case STATE_STORE:
        if (Store_Update(wPressed) == STORE_ACTION_DONE)
        {
            if (s_bStorePreGame)
                StartFadeOut(STATE_GAME);
            else
            {
                s_bNewRound = 1;
                StartFadeOut(STATE_GAME);
            }
        }
        break;

    case STATE_GAME:
        Game_Update(wPressed);
        if (Game_IsRoundOver())
            StartFadeOut(STATE_RESULTS);
        if (wPressed & BTN_BACK)
        {
            s_bResuming = 1;
            StartFadeOut(STATE_MENU);
        }
        break;

    case STATE_RESULTS:
    {
        int nAction = Results_Update(wPressed);
        if (nAction == RESULTS_ACTION_MENU)
            StartFadeOut(STATE_MENU);
        if (nAction == RESULTS_ACTION_CONTINUE)
        {
            s_bStorePreGame = 0;
            StartFadeOut(STATE_STORE);
        }
        break;
    }

    case STATE_CREDITS:
        if (Credits_Update(wPressed) == CRED_ACTION_DONE)
            StartFadeOut(STATE_SHUTDOWN);
        break;

    default:
        break;
    }
}

/* =========================================================================
   Per-state draw
========================================================================= */

static void State_Draw(void)
{
    switch (s_eState)
    {
    case STATE_TITLE:    UI_TitleDraw();    break;
    case STATE_MENU:     Menu_Draw();       break;
    case STATE_OPTIONS:  Options_Draw();    break;
    case STATE_HELP:     Help_Draw();       break;
    case STATE_SETUP:    Setup_Draw();      break;
    case STATE_STORE:    Store_Draw();      break;
    case STATE_GAME:     Game_Draw();       break;
    case STATE_RESULTS:  Results_Draw();    break;
    case STATE_CREDITS:  Credits_Draw();    break;
    default:                                break;
    }
}

static void State_Enter(GameState eNew)
{
    s_ePrevState = s_eState;
    s_eState = eNew;

    switch (eNew)
    {
    case STATE_VIDEO:
        Video_PlayBlocking("D:\\xmv\\Intro.xmv");
        s_eState = STATE_TITLE;
        UI_TitleReset();
        Audio_MusicPlayTitle();
        s_eFade = FADE_IN;
        s_dwFadeStart = GetTickCount();
        break;

    case STATE_TITLE:
        UI_TitleReset();
        if (!Audio_MusicIsPlaying())
            Audio_MusicPlayTitle();
        break;

    case STATE_MENU:
        if (s_ePrevState == STATE_GAME)
            Sfx_Stop(SFX_BATTLEFIELD_LOOP);
        else
            Game_Shutdown();
        /* Don't restart music when coming from screens that leave it running */
        if (s_ePrevState != STATE_TITLE &&
            s_ePrevState != STATE_OPTIONS &&
            s_ePrevState != STATE_HELP)
        {
            Audio_MusicStop();
            Audio_MusicPlayTitle();
        }
        Menu_Init();
        break;

    case STATE_OPTIONS:
        Options_Init();
        break;

    case STATE_HELP:
        break;

    case STATE_SETUP:
        Setup_Init();
        break;

    case STATE_STORE:
        /* Pre-game: init human player first so store has someone to modify */
        if (s_bStorePreGame)
            Game_PreInit();
        Store_Init();
        break;

    case STATE_GAME:
        if (s_bResuming)
        {
            s_bResuming = 0;
            Audio_MusicStop();
            Audio_MusicPlayGameplay();
        }
        else if (s_bNewRound)
        {
            s_bNewRound = 0;
            Game_NewRound();
            Audio_MusicStop();
            Audio_MusicPlayGameplay();
        }
        else
        {
            /* First game -- player was pre-inited via store, finish init */
            Game_Init();
            Audio_MusicStop();
            Audio_MusicPlayGameplay();
        }
        break;

    case STATE_RESULTS:
        Results_Init();
        break;

    case STATE_CREDITS:
        Credits_Init();
        break;

    case STATE_SHUTDOWN:
        Audio_MusicStop();
        break;

    default:
        break;
    }
}

/* =========================================================================
   Entry point
========================================================================= */

void __cdecl main(void)
{
    WORD wCur;
    WORD wPressed;

    if (FAILED(Render_Init()))
        return;

    InitInput();
    Audio_Init();
    Sfx_Init();
    Config_Load();
    Config_Apply();

    /* Play intro immediately -- no texture wait before the XMV */
    Video_PlayBlocking("D:\\xmv\\Intro.xmv");

    /* Textures load after the video -- sequential DVD reads, no competition */
    ShowLoadingScreen();
    UI_Init();

    /* Transition straight to title */
    s_eState = STATE_TITLE;
    UI_TitleReset();
    Audio_MusicPlayTitle();
    s_eFade = FADE_IN;
    s_dwFadeStart = GetTickCount();

    while (s_eState != STATE_SHUTDOWN)
    {
        PumpInput();

        wCur = GetButtons();
        wPressed = wCur & ~s_wPrevButtons;
        s_wPrevButtons = wCur;

        Audio_Update();
        FadeUpdate();
        State_Update(wPressed);

        Render_BeginFrame(0xFF000000);
        State_Draw();
        FadeDraw();
        Render_EndFrame();
    }

    UI_Shutdown();
    Audio_Shutdown();
    Render_Shutdown();
}