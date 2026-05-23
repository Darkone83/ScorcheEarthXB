#ifndef SCORCHEDXB_TEX_H
#define SCORCHEDXB_TEX_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - tex.h
    DDS texture loading and fullscreen / rect rendering.

    D3DXCreateTextureFromFile on Xbox creates swizzled textures by default,
    which is the correct format for rendering.  No manual swizzle step needed.

    Paths use Xbox HDD notation:  "D:\\tex\\splash.dds"

    Tex_DrawFullscreen / Tex_DrawRect set all required render states and
    restore them afterwards.  dwColor is an ARGB modulate tint; pass
    0xFFFFFFFF for unmodified.
---------------------------------------------------------------------------*/

IDirect3DTexture8* Tex_Load(const char* pszPath);
void               Tex_Free(IDirect3DTexture8* pTex);
void               Tex_DrawFullscreen(IDirect3DTexture8* pTex, DWORD dwColor);
void               Tex_DrawRect(IDirect3DTexture8* pTex,
    float x, float y, float w, float h,
    DWORD dwColor);

/* ── Preload: queue reads before a blocking operation (e.g. video),
      finish after -- hides disk I/O, Tex_Load hits cache instantly ── */
void Tex_PreloadQueue(const char* pszPath);  /* queue a path           */
void Tex_PreloadStart(void);                 /* start background reads */
void Tex_PreloadFinish(void);                 /* wait for reads to end  */

#endif /* SCORCHEDXB_TEX_H */