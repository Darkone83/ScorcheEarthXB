/*---------------------------------------------------------------------------
    ScorchedXB - tex.cpp
    DDS texture loading, 2D quad rendering, and background preload cache.

    Preload system
    --------------
    Tex_PreloadQueue / Tex_PreloadStart / Tex_PreloadFinish hide disk I/O
    behind the intro video:

        Tex_PreloadQueue( "D:\\tex\\splash.dds" );
        Tex_PreloadQueue( "D:\\tex\\ui.dds"     );
        Tex_PreloadStart();                    // spawns background read thread
        Video_PlayBlocking( "D:\\xmv\\Intro.xmv" );
        Tex_PreloadFinish();                   // waits; data already in RAM
        UI_Init();          // Tex_Load hits the cache -- D3D creation only,
                            // no disk I/O wait

    Notes
    -----
    The Ex D3DX loaders below explicitly request one mip level and no filtering.
    This prevents D3DX from doing extra runtime work for the large DDS files.

    For odd/non-power-of-two DDS source images, D3DX may allocate a larger
    power-of-two backing texture.  We track the original source width/height and
    clamp fullscreen UVs so padded texture area is not sampled.

    Link: d3dx8.lib, xgraphics.lib
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <d3dx8.h>
#include "tex.h"
#include "render.h"

/* -------------------------------------------------------------------------
   Textured vertex
------------------------------------------------------------------------- */

#define TEX_FVF  ( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 )

typedef struct
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
} TexVert;

/* -------------------------------------------------------------------------
   Preload cache
------------------------------------------------------------------------- */

#define TEX_PRELOAD_MAX  8
#define TEX_PATH_MAX     64

typedef struct
{
    char  szPath[TEX_PATH_MAX];
    BYTE* pData;
    DWORD dwSize;
    int   bReady;
} TexPreloadSlot;

static TexPreloadSlot  s_cache[TEX_PRELOAD_MAX];
static int             s_nCacheCount = 0;
static HANDLE          s_hReadThread = NULL;

/* -------------------------------------------------------------------------
   Texture metadata

   D3DX may create a larger backing texture for odd DDS sizes.  If fullscreen
   drawing uses UV 0..1, the padded area can show and the image no longer fills
   correctly.  Store the real source dimensions so fullscreen drawing can clamp
   UVs to the valid image region.
------------------------------------------------------------------------- */

#define TEX_META_MAX 32

typedef struct
{
    IDirect3DTexture8* pTex;
    DWORD             dwSrcW;
    DWORD             dwSrcH;
} TexMeta;

static TexMeta s_meta[TEX_META_MAX];
static int     s_nMetaCount = 0;

static void Tex_ParseDDSSize(const BYTE* pData, DWORD dwSize,
    DWORD* pdwW, DWORD* pdwH)
{
    const DWORD* p;

    *pdwW = 0;
    *pdwH = 0;

    if (!pData || dwSize < 128)
        return;

    /* DDS magic */
    if (pData[0] != 'D' || pData[1] != 'D' ||
        pData[2] != 'S' || pData[3] != ' ')
        return;

    /*
        DDS layout:
            magic         0
            DDSD size     4
            flags         8
            height       12
            width        16
    */
    p = (const DWORD*)pData;
    *pdwH = p[3];
    *pdwW = p[4];
}

static void Tex_RegisterMeta(IDirect3DTexture8* pTex,
    DWORD dwSrcW, DWORD dwSrcH)
{
    int i;

    if (!pTex || !dwSrcW || !dwSrcH)
        return;

    for (i = 0; i < s_nMetaCount; i++)
    {
        if (s_meta[i].pTex == pTex)
        {
            s_meta[i].dwSrcW = dwSrcW;
            s_meta[i].dwSrcH = dwSrcH;
            return;
        }
    }

    if (s_nMetaCount >= TEX_META_MAX)
        return;

    s_meta[s_nMetaCount].pTex = pTex;
    s_meta[s_nMetaCount].dwSrcW = dwSrcW;
    s_meta[s_nMetaCount].dwSrcH = dwSrcH;
    s_nMetaCount++;
}

static void Tex_UnregisterMeta(IDirect3DTexture8* pTex)
{
    int i;

    if (!pTex)
        return;

    for (i = 0; i < s_nMetaCount; i++)
    {
        if (s_meta[i].pTex == pTex)
        {
            s_meta[i] = s_meta[s_nMetaCount - 1];
            s_nMetaCount--;
            return;
        }
    }
}

static void Tex_GetValidUV(IDirect3DTexture8* pTex,
    float* pfU1, float* pfV1)
{
    D3DSURFACE_DESC desc;
    int             i;

    *pfU1 = 1.0f;
    *pfV1 = 1.0f;

    if (!pTex)
        return;

    for (i = 0; i < s_nMetaCount; i++)
    {
        if (s_meta[i].pTex == pTex)
        {
            if (SUCCEEDED(pTex->GetLevelDesc(0, &desc)) &&
                desc.Width && desc.Height)
            {
                if (s_meta[i].dwSrcW < desc.Width)
                    *pfU1 = (float)s_meta[i].dwSrcW / (float)desc.Width;

                if (s_meta[i].dwSrcH < desc.Height)
                    *pfV1 = (float)s_meta[i].dwSrcH / (float)desc.Height;
            }
            return;
        }
    }
}

/* Simple string compare -- avoids pulling in strcmp */
static int PathEq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Background thread: raw file reads, no D3D */
static DWORD WINAPI PreloadThreadProc(LPVOID pParam)
{
    int   i;
    (void)pParam;

    for (i = 0; i < s_nCacheCount; i++)
    {
        HANDLE hFile;
        DWORD  dwSize, dwRead;

        hFile = CreateFile(s_cache[i].szPath, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile == INVALID_HANDLE_VALUE) continue;

        dwSize = GetFileSize(hFile, NULL);
        if (dwSize && dwSize != 0xFFFFFFFF)
        {
            s_cache[i].pData = (BYTE*)malloc(dwSize);
            if (s_cache[i].pData)
            {
                ReadFile(hFile, s_cache[i].pData, dwSize, &dwRead, NULL);
                s_cache[i].dwSize = dwRead;
                s_cache[i].bReady = 1;
            }
        }
        CloseHandle(hFile);
    }
    return 0;
}

/* -------------------------------------------------------------------------
   Preload public API
------------------------------------------------------------------------- */

void Tex_PreloadQueue(const char* pszPath)
{
    const char* src;
    char* dst;
    int         i;

    if (s_nCacheCount >= TEX_PRELOAD_MAX) return;

    i = s_nCacheCount++;
    dst = s_cache[i].szPath;
    src = pszPath;
    while (*src && (dst - s_cache[i].szPath) < TEX_PATH_MAX - 1)
        *dst++ = *src++;
    *dst = '\0';

    s_cache[i].pData = NULL;
    s_cache[i].dwSize = 0;
    s_cache[i].bReady = 0;
}

void Tex_PreloadStart(void)
{
    if (s_nCacheCount == 0) return;
    s_hReadThread = CreateThread(NULL, 0, PreloadThreadProc, NULL, 0, NULL);
    if (s_hReadThread)
        SetThreadPriority(s_hReadThread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Tex_PreloadFinish(void)
{
    if (!s_hReadThread) return;
    WaitForSingleObject(s_hReadThread, INFINITE);
    CloseHandle(s_hReadThread);
    s_hReadThread = NULL;
}

/* -------------------------------------------------------------------------
   Tex_Load
------------------------------------------------------------------------- */

IDirect3DTexture8* Tex_Load(const char* pszPath)
{
    IDirect3DTexture8* pTex = NULL;
    HRESULT            hr;
    int                i;

    for (i = 0; i < s_nCacheCount; i++)
    {
        if (s_cache[i].bReady && PathEq(s_cache[i].szPath, pszPath))
        {
            DWORD dwSrcW = 0;
            DWORD dwSrcH = 0;

            Tex_ParseDDSSize(s_cache[i].pData, s_cache[i].dwSize,
                &dwSrcW, &dwSrcH);

            hr = D3DXCreateTextureFromFileInMemoryEx(
                g_pd3dDevice,
                s_cache[i].pData,
                s_cache[i].dwSize,
                D3DX_DEFAULT,
                D3DX_DEFAULT,
                1,
                0,
                D3DFMT_UNKNOWN,
                D3DPOOL_MANAGED,
                D3DX_FILTER_NONE,
                D3DX_FILTER_NONE,
                0,
                NULL,
                NULL,
                &pTex);

            if (FAILED(hr) || !pTex)
            {
                D3DXCreateTextureFromFileInMemory(
                    g_pd3dDevice,
                    s_cache[i].pData,
                    s_cache[i].dwSize,
                    &pTex);
            }

            Tex_RegisterMeta(pTex, dwSrcW, dwSrcH);

            free(s_cache[i].pData);
            s_cache[i].pData = NULL;
            s_cache[i].bReady = 0;
            return pTex;
        }
    }

    /* Not preloaded -- synchronous fallback */
    hr = D3DXCreateTextureFromFileEx(
        g_pd3dDevice,
        pszPath,
        D3DX_DEFAULT,
        D3DX_DEFAULT,
        1,
        0,
        D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED,
        D3DX_FILTER_NONE,
        D3DX_FILTER_NONE,
        0,
        NULL,
        NULL,
        &pTex);

    if (FAILED(hr) || !pTex)
    {
        D3DXCreateTextureFromFile(g_pd3dDevice, pszPath, &pTex);
    }

    return pTex;
}

/* -------------------------------------------------------------------------
   Tex_Free
------------------------------------------------------------------------- */

void Tex_Free(IDirect3DTexture8* pTex)
{
    if (pTex)
    {
        Tex_UnregisterMeta(pTex);
        pTex->Release();
    }
}

/* -------------------------------------------------------------------------
   Render state helpers
------------------------------------------------------------------------- */

static void SetTexStates(IDirect3DTexture8* pTex)
{
    g_pd3dDevice->SetVertexShader(TEX_FVF);
    g_pd3dDevice->SetTexture(0, pTex);

    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
}

static void RestoreTexStates(void)
{
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

static void DrawQuad(float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    DWORD dwColor)
{
    TexVert v[4];

    v[0].x = x;     v[0].y = y;     v[0].z = 0.f; v[0].rhw = 1.f; v[0].color = dwColor; v[0].u = u0; v[0].v = v0;
    v[1].x = x + w; v[1].y = y;     v[1].z = 0.f; v[1].rhw = 1.f; v[1].color = dwColor; v[1].u = u1; v[1].v = v0;
    v[2].x = x;     v[2].y = y + h; v[2].z = 0.f; v[2].rhw = 1.f; v[2].color = dwColor; v[2].u = u0; v[2].v = v1;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 0.f; v[3].rhw = 1.f; v[3].color = dwColor; v[3].u = u1; v[3].v = v1;

    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TexVert));
}

/* -------------------------------------------------------------------------
   Public draw API
------------------------------------------------------------------------- */

void Tex_DrawFullscreen(IDirect3DTexture8* pTex, DWORD dwColor)
{
    float u1;
    float v1;

    if (!pTex) return;

    Tex_GetValidUV(pTex, &u1, &v1);

    SetTexStates(pTex);
    DrawQuad(0.f, 0.f, (float)g_dwDisplayW, (float)g_dwDisplayH,
        0.f, 0.f, u1, v1, dwColor);
    RestoreTexStates();
}

void Tex_DrawRect(IDirect3DTexture8* pTex,
    float x, float y, float w, float h,
    DWORD dwColor)
{
    if (!pTex) return;
    SetTexStates(pTex);
    DrawQuad(x, y, w, h, 0.f, 0.f, 1.f, 1.f, dwColor);
    RestoreTexStates();
}