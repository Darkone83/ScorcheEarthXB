#ifndef SCORCHEDXB_TERRAIN_H
#define SCORCHEDXB_TERRAIN_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - terrain.h
    Pixel-perfect port of scorch.js Bitmap + generateTerrain + drop().

    The terrain is stored as a flat DWORD pixel buffer matching the display
    resolution (g_dwDisplayW x g_dwDisplayH).  A linear D3D texture mirrors
    it for rendering; dirty-rect upload keeps bandwidth in check.

    Pixels are either:
        background  -- sky pattern at that (x,y) position
        ground      -- solid groundColor
        (other)     -- tanks, etc., treated as non-background obstacles

    Background kinds (set per round):
        BG_STARS    black + per-column random-Y white dot, fades with depth
        BG_GRADIENT two-colour top-to-bottom blend across 127 strips
        BG_PLAIN    solid colour

    All colours are packed 0xFFRRGGBB (matching rgb() in scorch.js which
    already ORs in 0xFF000000).

    Public API
    ----------
    Terrain_Init / Terrain_Shutdown -- allocate/free buffers and texture
    Terrain_NewRound                -- generate fresh terrain (full upload)
    Terrain_Upload                  -- dirty-rect copy to D3D texture
    Terrain_Draw                    -- full-screen textured quad
    Terrain_IsBackground(x,y)       -- true if pixel == BackgroundAt(x,y)
    Terrain_IsGround(x,y)           -- true if pixel == groundColor
    Terrain_GetGroundColor()        -- current ground colour
    Terrain_GetPixel(x,y)           -- raw pixel value
    Terrain_SetPixelGround(x,y)     -- write ground pixel (marks dirty)
    Terrain_SetPixelBg(x,y)         -- write background pixel (marks dirty)
    Terrain_Explode(x,y,r)          -- simple circular crater + drop
    Terrain_Drop(x0,x1)             -- gravity pass over column range
    Terrain_MarkDirty(x0,x1)        -- expand dirty rect
---------------------------------------------------------------------------*/

#define TERR_BG_STARS    0
#define TERR_BG_GRADIENT 1
#define TERR_BG_PLAIN    2

void  Terrain_Init(void);
void  Terrain_Shutdown(void);
void  Terrain_NewRound(void);
void  Terrain_Upload(void);
void  Terrain_PreDraw(void);   /* call BEFORE Render_BeginFrame */
void  Terrain_Draw(void);   /* no-op -- terrain already in back buffer */

int   Terrain_IsBackground(int x, int y);
int   Terrain_IsGround(int x, int y);
DWORD Terrain_GetGroundColor(void);
int   Terrain_GetBgKind(void);
DWORD Terrain_GetPixel(int x, int y);
void  Terrain_SetPixelGround(int x, int y);
void  Terrain_SetPixelBg(int x, int y);

void  Terrain_Explode(int x, int y, int nRadius);
void  Terrain_Drop(int nX0, int nX1);
void  Terrain_MarkDirty(int nX0, int nX1);

/* ── Batch primitives (no drop, no dirty) -- used by weapons module ──
   Caller is responsible for Terrain_MarkDirty + Terrain_Drop afterward. */
void  Terrain_CarveCircle(int cx, int cy, int r);
void  Terrain_CarveLine(int x1, int y1, int x2, int y2);
void  Terrain_DrawLineGround(int x1, int y1, int x2, int y2);

#endif /* SCORCHEDXB_TERRAIN_H */