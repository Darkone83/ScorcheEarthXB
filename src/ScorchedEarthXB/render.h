#ifndef SCORCHEDXB_RENDER_H
#define SCORCHEDXB_RENDER_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - render.h
    D3D8 device management and resolution scaling.

    Game logic always works in GAME_W x GAME_H (800x600) space.
    Display resolution is detected at init via XGetVideoFlags() and the
    g_fScaleX / g_fScaleY factors are used to convert game coords to
    screen coords wherever needed (UI, particles, tank positions, etc).
---------------------------------------------------------------------------*/

#define GAME_W  800
#define GAME_H  600

extern IDirect3DDevice8* g_pd3dDevice;
extern LPDIRECTSOUND     g_pDS;        /* global DirectSound device — init in Render_Init */

extern DWORD  g_dwDisplayW;   /* actual backbuffer width  */
extern DWORD  g_dwDisplayH;   /* actual backbuffer height */
extern float  g_fScaleX;      /* g_dwDisplayW / GAME_W    */
extern float  g_fScaleY;      /* g_dwDisplayH / GAME_H    */

/* Convert game-space coordinates to display pixels */
#define GX( x )  ( (float)(x) * g_fScaleX )
#define GY( y )  ( (float)(y) * g_fScaleY )

HRESULT Render_Init(void);
void    Render_Shutdown(void);
void    Render_BeginFrame(D3DCOLOR clearColor);
void    Render_BeginFrameNoClear(void);
void    Render_EndFrame(void);

#endif /* SCORCHEDXB_RENDER_H */