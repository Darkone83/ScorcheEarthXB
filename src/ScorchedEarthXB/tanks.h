#ifndef SCORCHEDXB_TANKS_H
#define SCORCHEDXB_TANKS_H

#include <xtl.h>
#include "player.h"

/*---------------------------------------------------------------------------
    ScorchedXB - tanks.h
    Cel-shaded 3D tank rendering.

    Each tank is three boxes (tracks, hull, turret) plus a rotated barrel
    quad, rendered in two passes:

        Pass 1 -- outline
            Slightly inflated geometry, D3DCULL_CW (back-faces only),
            solid black.  Creates the ink-line outline.

        Pass 2 -- fill (toon shading)
            Normal geometry, D3DCULL_CCW (front-faces), three colour
            bands (bright/mid/dark) derived from face-normal dot product
            with a fixed upper-left light.

    Projection: orthographic, 1 world unit = 1 screen pixel, top-left
    origin.  Tanks render at their terrain pixel position with no
    additional scaling.  Z test disabled; tanks always draw over terrain.

    Barrel: rotated quad from turret pivot to tip in player colour.
    Shield: ellipse outline drawn when player has a shield active.
---------------------------------------------------------------------------*/

void Tanks_Init(void);
void Tanks_Shutdown(void);
void Tanks_DrawAll(const Player* pPlayers, int nCount);
void Tanks_DrawPreview(int nTankType, DWORD dwColor, float cx, float cy, float fScale);

#endif /* SCORCHEDXB_TANKS_H */