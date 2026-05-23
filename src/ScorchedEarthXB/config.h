#ifndef SCORCHEDXB_CONFIG_H
#define SCORCHEDXB_CONFIG_H

/*---------------------------------------------------------------------------
    ScorchedXB - config.h
    Persistent settings stored in D:\ScorchedXB.cfg (XBE root).

    Binary format:
        DWORD  dwMagic    'SXBK'
        int    nMusicVol  0-10
        int    nSFXVol    0-10
        int    bRumble    0=off 1=on
        DWORD  dwCheck    XOR of previous three ints

    Defaults (used when file absent or corrupt):
        Music  = 8
        SFX    = 8
        Rumble = 1 (on)
---------------------------------------------------------------------------*/

#define CFG_PATH  "D:\\ScorchedXB.cfg"

typedef struct
{
    int nMusicVol;   /* 0-10 */
    int nSFXVol;     /* 0-10 */
    int bRumble;     /* 0 or 1 */
    int nWallType;   /* 0=none  1=wrap  2=bounce */
} GameConfig;

/* Global config instance -- read by audio, sfx, and game modules */
extern GameConfig g_cfg;

void Config_Load(void);   /* loads from disk; fills defaults on failure */
void Config_Save(void);   /* writes to disk */
void Config_Apply(void);   /* pushes volumes / rumble into subsystems */

#endif /* SCORCHEDXB_CONFIG_H */