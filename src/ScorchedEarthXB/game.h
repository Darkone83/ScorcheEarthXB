#ifndef SCORCHEDXB_GAME_H
#define SCORCHEDXB_GAME_H

#include <xtl.h>
#include "player.h"

/*---------------------------------------------------------------------------
    ScorchedXB - game.h
    Round loop, turn management, and game-session state.

    Default setup: 1 human (Player 1) + 3 AI opponents.
    AI types cycle: Shooter (0), Cyborg (1), Killer (2).

    SFX mapping (matched to scorch.js beep() trigger points):
        Fire     → cannon/weapon sound by weapon kind
        Explode  → explosion_small/medium/heavy by radius
        Roller   → impact_dirt on bounce/land
        Laser    → weapon_fire_click
        Death    → tank_destroyed (random 01/02)
        Ambient  → battlefield_fireworks_loop (round start → round end)

    Audio: Audio_MusicPlayGameplay() called once from main.cpp State_Enter.
           Game module does not restart music; SFX only.

    Call sequence:
        Game_Init()         once per game session
        Game_NewRound()     each round (terrain + place tanks + wind)
        Game_Update(wPressed) each frame
        Game_Draw()         each frame inside BeginFrame/EndFrame
        Game_IsRoundOver()  check for round end → call Game_NewRound or shutdown
        Game_Shutdown()     on exit
---------------------------------------------------------------------------*/

#define GAME_HUMAN_COUNT   1
#define GAME_AI_COUNT      3
#define GAME_PLAYER_COUNT  ( GAME_HUMAN_COUNT + GAME_AI_COUNT )

void    Game_Init(void);
void    Game_Shutdown(void);
void    Game_NewRound(void);
void    Game_Update(WORD wPressed);
void    Game_Draw(void);
int     Game_IsRoundOver(void);
int     Game_IsGameOver(void);
int     Game_IsActive(void);

/* Pre-game / store / results support */
void    Game_PreInit(void);
Player* Game_GetPlayer(int idx);
int     Game_GetPlayerCount(void);
int     Game_GetRound(void);
void    Game_StopRumble(void);   /* call on state exit to kill active rumble */

#endif /* SCORCHEDXB_GAME_H */