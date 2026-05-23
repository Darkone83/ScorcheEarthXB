/*---------------------------------------------------------------------------
    ScorchedXB - config.cpp
---------------------------------------------------------------------------*/

#include <stdio.h>
#include "config.h"
#include "audio.h"
#include "sfx.h"

GameConfig g_cfg = { 8, 8, 1, 0 };   /* defaults: wall=none */

#define CFG_MAGIC  0x5358424Cu   /* 'SXBL' -- bump magic on format change */

typedef struct
{
    unsigned int dwMagic;
    int          nMusicVol;
    int          nSFXVol;
    int          bRumble;
    int          nWallType;
    unsigned int dwCheck;
} CfgFile;

static unsigned int CalcCheck(const CfgFile* p)
{
    return (unsigned int)(p->nMusicVol ^ p->nSFXVol ^ p->bRumble ^ p->nWallType);
}

void Config_Load(void)
{
    FILE* f;
    CfgFile  cf;
    int      n;

    f = fopen(CFG_PATH, "rb");
    if (!f) return;

    n = (int)fread(&cf, 1, sizeof(cf), f);
    fclose(f);

    if (n != sizeof(cf))            return;
    if (cf.dwMagic != CFG_MAGIC)    return;
    if (cf.dwCheck != CalcCheck(&cf)) return;

    g_cfg.nMusicVol = cf.nMusicVol < 0 ? 0 : cf.nMusicVol > 10 ? 10 : cf.nMusicVol;
    g_cfg.nSFXVol = cf.nSFXVol < 0 ? 0 : cf.nSFXVol   > 10 ? 10 : cf.nSFXVol;
    g_cfg.bRumble = cf.bRumble ? 1 : 0;
    g_cfg.nWallType = cf.nWallType < 0 ? 0 : cf.nWallType > 2 ? 2 : cf.nWallType;
}

void Config_Save(void)
{
    FILE* f;
    CfgFile cf;

    cf.dwMagic = CFG_MAGIC;
    cf.nMusicVol = g_cfg.nMusicVol;
    cf.nSFXVol = g_cfg.nSFXVol;
    cf.bRumble = g_cfg.bRumble;
    cf.nWallType = g_cfg.nWallType;
    cf.dwCheck = CalcCheck(&cf);

    f = fopen(CFG_PATH, "wb");
    if (!f) return;
    fwrite(&cf, 1, sizeof(cf), f);
    fclose(f);
}

void Config_Apply(void)
{
    Audio_MusicVolume(g_cfg.nMusicVol * 10);
    Sfx_SetGlobalVolume(g_cfg.nSFXVol * 10);
}