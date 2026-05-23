/*---------------------------------------------------------------------------
    ScorchedXB - render.cpp
    D3D8 device creation and frame control.

    Mirrors XbJazz / XbTyrian pattern exactly:
      - Direct3DCreate8() then Direct3D_CreateDevice() free functions
      - D3DFMT_X8R8G8B8 backbuffer, no depth/stencil
      - D3DPRESENT_INTERVAL_IMMEDIATE
      - pp.Flags set for progressive/widescreen from XGetVideoFlags()
      - Fallback to plain 480i if requested mode fails

    BeginFrame clears TARGET only (no ZBUFFER - depth stencil is off).
---------------------------------------------------------------------------*/

#include <xtl.h>
#include "render.h"

/* --- exported globals ---------------------------------------------------- */

IDirect3DDevice8* g_pd3dDevice = NULL;
LPDIRECTSOUND     g_pDS = NULL;   /* global DirectSound device     */
DWORD             g_dwDisplayW = 640;
DWORD             g_dwDisplayH = 480;
float             g_fScaleX = 1.0f;
float             g_fScaleY = 1.0f;

/* --- internal: resolve display mode ------------------------------------- */

static DWORD SetupDisplayMode(void)
{
    /*  Returns the D3DPRESENT_* flags for pp.Flags and sets
        g_dwDisplayW / g_dwDisplayH / g_fScaleX / g_fScaleY.          */

    DWORD dwVid = XGetVideoFlags();
    DWORD dwFlags = 0;

    if (dwVid & XC_VIDEO_FLAGS_HDTV_720p)
    {
        g_dwDisplayW = 1280;
        g_dwDisplayH = 720;
        dwFlags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
    }
    else if (dwVid & XC_VIDEO_FLAGS_HDTV_480p)
    {
        g_dwDisplayW = 640;
        g_dwDisplayH = 480;
        dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
    }
    else
    {
        g_dwDisplayW = 640;
        g_dwDisplayH = 480;
        dwFlags = 0;
    }

    g_fScaleX = (float)g_dwDisplayW / (float)GAME_W;
    g_fScaleY = (float)g_dwDisplayH / (float)GAME_H;

    return dwFlags;
}

/* --- public functions ---------------------------------------------------- */

HRESULT Render_Init(void)
{
    D3DPRESENT_PARAMETERS pp;
    DWORD   dwPresentFlags;
    HRESULT hr;

    dwPresentFlags = SetupDisplayMode();

    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = g_dwDisplayW;
    pp.BackBufferHeight = g_dwDisplayH;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.EnableAutoDepthStencil = FALSE;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.Flags = dwPresentFlags;

    Direct3DCreate8(D3D_SDK_VERSION);

    hr = Direct3D_CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &pp,
        &g_pd3dDevice);

    /* Fallback: plain 480i if the requested mode failed */
    if (FAILED(hr))
    {
        g_dwDisplayW = 640;
        g_dwDisplayH = 480;
        g_fScaleX = (float)g_dwDisplayW / (float)GAME_W;
        g_fScaleY = (float)g_dwDisplayH / (float)GAME_H;

        pp.BackBufferWidth = 640;
        pp.BackBufferHeight = 480;
        pp.Flags = 0;

        hr = Direct3D_CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            NULL,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &pp,
            &g_pd3dDevice);
    }

    if (FAILED(hr))
        return hr;

    /* Initialize DirectSound — required before any audio (XMV, SFX, music) */
    DirectSoundCreate(NULL, &g_pDS, NULL);

    return S_OK;
}

void Render_Shutdown(void)
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pDS) { g_pDS->Release();         g_pDS = NULL; }
}

void Render_BeginFrame(D3DCOLOR clearColor)
{
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, clearColor, 1.0f, 0);
    g_pd3dDevice->BeginScene();
}

void Render_BeginFrameNoClear(void)
{
    /* Terrain already written to back buffer by Terrain_PreDraw -- skip Clear */
    g_pd3dDevice->BeginScene();
}

void Render_EndFrame(void)
{
    g_pd3dDevice->EndScene();
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
}