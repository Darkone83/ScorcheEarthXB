#ifndef SCORCHEDXB_AUDIO_H
#define SCORCHEDXB_AUDIO_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - audio.h
    DirectSound SFX pool + MP3 music streaming with track management.

    Track layout  (D:\tracks\)
      track0.mp3   Title screen     -- loops continuously
      track1-5.mp3 Gameplay         -- shuffled, each plays once; reshuffles
                                       when all five have played
      track6.mp3   Credits          -- plays once, stops

    Shuffle uses a Fisher-Yates pass seeded from GetTickCount().
    Audio_Update() must be called once per frame to service playlist
    advancement (detects EOF signal from streaming thread).

    SFX: up to AUDIO_SFX_MAX pre-loaded WAV slots.

    Volume: 0 = silent, 100 = full.

    Dependency: minimp3.h (single header, MIT)
      https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h
---------------------------------------------------------------------------*/

#define AUDIO_SFX_MAX  32

/* ── System ── */
void Audio_Init(void);
void Audio_Update(void);   /* call once per frame -- drives playlist    */
void Audio_Shutdown(void);

/* ── Music -- named entry points ── */
void Audio_MusicPlayTitle(void);   /* track0, loops                    */
void Audio_MusicPlayGameplay(void);   /* shuffle tracks 1-5, auto-advance */
void Audio_MusicPlayCredits(void);   /* track6, plays once               */
void Audio_MusicStop(void);
void Audio_MusicVolume(int nVol);   /* 0-100                        */
int  Audio_MusicIsPlaying(void);

/* ── SFX ── */
int  Audio_SfxLoad(const char* pszPath);   /* returns slot or -1       */
void Audio_SfxPlay(int iSlot);
void Audio_SfxPlayLooping(int iSlot);   /* loops until Audio_SfxStop */
void Audio_SfxStop(int iSlot);
void Audio_SfxVolume(int iSlot, int nVol);   /* 0-100                    */

#endif /* SCORCHEDXB_AUDIO_H */