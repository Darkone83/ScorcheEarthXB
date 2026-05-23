/*---------------------------------------------------------------------------
    ScorchedXB - audio.cpp
    DirectSound SFX pool + MP3 music streaming with shuffle playlist.

    Track layout:
        D:\tracks\track0.mp3   title    (loop)
        D:\tracks\track1-5.mp3 gameplay (shuffled, one-shot each)
        D:\tracks\track6.mp3   credits  (one-shot)

    Streaming design (XBMC-inspired):
        256KB ring buffer split into two 128KB halves.
        Background thread fills the dead half whenever the play cursor
        leaves it.  On EOF in one-shot mode the thread sets s_bEOF via
        InterlockedExchange; Audio_Update() picks it up on the next frame
        and advances the playlist.  Loop mode (title) simply wraps the
        decode position back to zero at EOF -- no signal sent.

    minimp3 intrin.h conflict fix:
        Hiding _M_IX86 before the include prevents minimp3 from pulling in
        <intrin.h>, which re-declares Interlocked functions without volatile
        and conflicts with RXDK WinBase.h (C2733).
---------------------------------------------------------------------------*/

#include <xtl.h>

#pragma push_macro("_M_IX86")
#pragma push_macro("__i386__")
#undef _M_IX86
#undef __i386__
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"
#pragma pop_macro("__i386__")
#pragma pop_macro("_M_IX86")

#include "audio.h"
#include "render.h"   /* g_pDS */

/* =========================================================================
   Constants
========================================================================= */

#define AUDIO_STREAM_BUFSIZE  ( 256 * 1024 )
#define AUDIO_STREAM_HALF     ( AUDIO_STREAM_BUFSIZE / 2 )

#define TRACK_DIR     "D:\\tracks\\track"
#define TRACK_EXT     ".mp3"
#define TRACK_TITLE   0
#define TRACK_CREDITS 6
#define TRACK_GP_FIRST 1
#define TRACK_GP_LAST  5
#define TRACK_GP_COUNT 5   /* tracks 1-5 */

/* =========================================================================
   Helpers
========================================================================= */

static long VolToDB(int nVol)
{
    if (nVol <= 0) return DSBVOLUME_MIN;
    if (nVol >= 100) return 0L;
    return (long)(nVol * 100) - 10000L;
}

/* Build "D:\tracks\trackN.mp3" without sprintf */
static void BuildTrackPath(char* pszOut, int nTrack)
{
    const char* pre = TRACK_DIR;
    const char* suf = TRACK_EXT;
    int i = 0;
    int j;
    while (pre[i]) { pszOut[i] = pre[i]; i++; }
    pszOut[i++] = (char)('0' + nTrack);
    for (j = 0; suf[j]; j++) pszOut[i++] = suf[j];
    pszOut[i] = '\0';
}

/* =========================================================================
   SFX pool
========================================================================= */

typedef struct { IDirectSoundBuffer* pBuf; int bUsed; } SfxSlot;
static SfxSlot s_sfx[AUDIO_SFX_MAX];

typedef struct { char id[4]; DWORD size; } RiffChunk;

static int FourCC(const char* p, const char* t)
{
    return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3];
}

static IDirectSoundBuffer* LoadWAV(const char* pszPath)
{
    HANDLE              hFile;
    DWORD               dwRead;
    char                hdr[12];
    RiffChunk           chunk;
    WAVEFORMATEX        wfx;
    BYTE* pData = NULL;
    DWORD               dwDataSize = 0;
    DSBUFFERDESC        desc;
    IDirectSoundBuffer* pBuf = NULL;
    void* pLock;
    DWORD               dwLock;
    int                 bGotFmt = 0;
    int                 bGotData = 0;

    hFile = CreateFile(pszPath, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    ReadFile(hFile, hdr, 12, &dwRead, NULL);
    if (dwRead < 12 || !FourCC(hdr, "RIFF") || !FourCC(hdr + 8, "WAVE"))
        goto cleanup;

    while (ReadFile(hFile, &chunk, 8, &dwRead, NULL) && dwRead == 8)
    {
        if (FourCC(chunk.id, "fmt "))
        {
            DWORD n = chunk.size < sizeof(wfx) ? chunk.size : sizeof(wfx);
            ZeroMemory(&wfx, sizeof(wfx));
            ReadFile(hFile, &wfx, n, &dwRead, NULL);
            if (chunk.size > n)
                SetFilePointer(hFile, (LONG)(chunk.size - n), NULL, FILE_CURRENT);
            bGotFmt = 1;
        }
        else if (FourCC(chunk.id, "data"))
        {
            dwDataSize = chunk.size;
            pData = (BYTE*)malloc(dwDataSize);
            if (pData) ReadFile(hFile, pData, dwDataSize, &dwRead, NULL);
            bGotData = 1;
        }
        else
        {
            DWORD skip = (chunk.size + 1) & ~1u;
            SetFilePointer(hFile, (LONG)skip, NULL, FILE_CURRENT);
        }
        if (bGotFmt && bGotData) break;
    }

    if (!bGotFmt || !bGotData || !pData) goto cleanup;

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = dwDataSize;
    desc.lpwfxFormat = &wfx;
    /* lpMixBins intentionally NULL -- Xbox routes to default L+R */

    if (!g_pDS) goto cleanup;
    if (FAILED(g_pDS->CreateSoundBuffer(&desc, &pBuf, NULL)))
    {
        pBuf = NULL; goto cleanup;
    }

    if (SUCCEEDED(pBuf->Lock(0, dwDataSize, &pLock, &dwLock, NULL, NULL, 0)))
    {
        memcpy(pLock, pData, dwDataSize); pBuf->Unlock(pLock, dwLock, NULL, 0);
    }

cleanup:
    if (pData) free(pData);
    CloseHandle(hFile);
    return pBuf;
}

/* =========================================================================
   Music streaming state
========================================================================= */

static IDirectSoundBuffer* s_pMusicBuf = NULL;
static BYTE* s_pMusicData = NULL;
static DWORD               s_dwMusicSize = 0;
static DWORD               s_dwMusicPos = 0;
static mp3dec_t             s_mp3Dec;

static HANDLE               s_hMusicThread = NULL;
static int                  s_nCurrentTrack = 0;
static DWORD                s_dwTrackStart = 0;
static LONG                 s_bMusicStop = 0;
static LONG                 s_bEOF = 0;
static int                  s_bLoopTrack = 0;
static int                  s_nWriteHalf = 0;
static CRITICAL_SECTION     s_cs;

/* ── PCM carry-over: leftover samples from a clipped frame boundary ──
   When a decoded frame doesn't fit exactly at a half boundary we write
   what fits and save the remainder here.  Next FillHalf drains this
   first so no PCM is ever lost between fills (the main source of pops). */
static short s_pcmCarry[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
static DWORD s_dwCarryBytes = 0;   /* bytes remaining in s_pcmCarry     */
static int   s_nCarryChan = 2;   /* channels of carried data          */

/* ── Playlist state ── */
static int  s_nPlaylist[TRACK_GP_COUNT];
static int  s_nPlaylistPos = 0;
static int  s_bInGameplay = 0;

/* ── Fisher-Yates shuffle seeded from GetTickCount() ── */
static void ShufflePlaylist(void)
{
    DWORD seed = GetTickCount();
    int   i, j, tmp;
    for (i = 0; i < TRACK_GP_COUNT; i++)
        s_nPlaylist[i] = TRACK_GP_FIRST + i;   /* [1,2,3,4,5] */
    for (i = TRACK_GP_COUNT - 1; i > 0; i--)
    {
        seed = seed * 1664525UL + 1013904223UL; /* LCG */
        j = (int)((seed >> 16) % (DWORD)(i + 1));
        tmp = s_nPlaylist[i]; s_nPlaylist[i] = s_nPlaylist[j]; s_nPlaylist[j] = tmp;
    }
    s_nPlaylistPos = 0;
}

/* =========================================================================
   Streaming internals
========================================================================= */

static void FillHalf(int nHalf)
{
    void* pData1;
    DWORD  dwLen1;
    BYTE* pDst;
    DWORD  dwFilled;
    short  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

    if (FAILED(s_pMusicBuf->Lock(
        (DWORD)nHalf * AUDIO_STREAM_HALF, AUDIO_STREAM_HALF,
        &pData1, &dwLen1, NULL, NULL, 0))) return;

    pDst = (BYTE*)pData1;
    dwFilled = 0;

    EnterCriticalSection(&s_cs);

    /* ── Drain carry-over from previous half boundary first ──────────────
       When the last decoded frame straddled the boundary we saved the
       remainder here.  Writing it now means zero PCM data is ever lost,
       which is the primary cause of the pop at every half transition.     */
    if (s_dwCarryBytes > 0)
    {
        DWORD canWrite = s_dwCarryBytes < dwLen1 ? s_dwCarryBytes : dwLen1;
        memcpy(pDst, s_pcmCarry, canWrite);
        dwFilled = canWrite;
        if (canWrite < s_dwCarryBytes)
            memmove(s_pcmCarry, (BYTE*)s_pcmCarry + canWrite,
                s_dwCarryBytes - canWrite);
        s_dwCarryBytes -= canWrite;
    }

    /* ── Decode new MP3 frames ─────────────────────────────────────────── */
    while (dwFilled < dwLen1)
    {
        mp3dec_frame_info_t info;
        int   nSamples;
        DWORD dwFrameBytes;
        DWORD dwCanWrite;

        if (s_dwMusicPos >= s_dwMusicSize)
        {
            if (!s_bLoopTrack)
            {
                InterlockedExchange(&s_bEOF, 1);
                memset(pDst + dwFilled, 0, dwLen1 - dwFilled);
                dwFilled = dwLen1;
                break;
            }
            s_dwMusicPos = 0;
            s_dwCarryBytes = 0;   /* reset carry on loop */
        }

        nSamples = mp3dec_decode_frame(&s_mp3Dec,
            s_pMusicData + s_dwMusicPos,
            (int)(s_dwMusicSize - s_dwMusicPos),
            pcm, &info);

        if (info.frame_bytes > 0)
            s_dwMusicPos += (DWORD)info.frame_bytes;
        else
            s_dwMusicPos = 0;

        if (nSamples <= 0) continue;

        dwFrameBytes = (DWORD)(nSamples * info.channels * (int)sizeof(short));
        dwCanWrite = dwLen1 - dwFilled;

        if (dwFrameBytes <= dwCanWrite)
        {
            memcpy(pDst + dwFilled, pcm, dwFrameBytes);
            dwFilled += dwFrameBytes;
        }
        else
        {
            /* Frame straddles half boundary -- write what fits, carry rest */
            memcpy(pDst + dwFilled, pcm, dwCanWrite);
            dwFilled = dwLen1;
            s_dwCarryBytes = dwFrameBytes - dwCanWrite;
            s_nCarryChan = info.channels;
            memcpy(s_pcmCarry, (BYTE*)pcm + dwCanWrite, s_dwCarryBytes);
        }
    }

    LeaveCriticalSection(&s_cs);
    s_pMusicBuf->Unlock(pData1, dwLen1, NULL, 0);
}

static DWORD WINAPI MusicThreadProc(LPVOID pParam)
{
    (void)pParam;
    while (!InterlockedCompareExchange(&s_bMusicStop, 0, 0))
    {
        DWORD dwPlay, dwWrite, dwHalfStart, dwHalfEnd;

        /* Service the Xbox DS mixer every loop -- required on Xbox hardware.
           Without this the play cursor stalls and the buffer pops/underruns. */
        DirectSoundDoWork();

        if (!s_pMusicBuf) { Sleep(4); continue; }
        if (InterlockedCompareExchange(&s_bEOF, 1, 1)) { Sleep(4); continue; }

        dwHalfStart = (DWORD)s_nWriteHalf * AUDIO_STREAM_HALF;
        dwHalfEnd = dwHalfStart + AUDIO_STREAM_HALF;

        s_pMusicBuf->GetCurrentPosition(&dwPlay, &dwWrite);
        if (dwPlay >= dwHalfStart && dwPlay < dwHalfEnd)
        {
            Sleep(4); continue;
        }

        FillHalf(s_nWriteHalf);
        s_nWriteHalf = 1 - s_nWriteHalf;
        Sleep(4);
    }
    return 0;
}

/* ── Internal: load file and start streaming ── */
static void StartStream(int nTrack, int bLoop)
{
    HANDLE              hFile;
    DWORD               dwFileSize, dwRead;
    short               pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    mp3dec_frame_info_t info;
    WAVEFORMATEX        wfx;
    DSBUFFERDESC        desc;
    char                szPath[32];

    Audio_MusicStop();

    s_nCurrentTrack = nTrack;
    s_dwTrackStart = GetTickCount();
    BuildTrackPath(szPath, nTrack);

    hFile = CreateFile(szPath, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    dwFileSize = GetFileSize(hFile, NULL);
    if (!dwFileSize || dwFileSize == 0xFFFFFFFF)
    {
        CloseHandle(hFile); return;
    }

    s_pMusicData = (BYTE*)malloc(dwFileSize);
    if (!s_pMusicData) { CloseHandle(hFile); return; }

    ReadFile(hFile, s_pMusicData, dwFileSize, &dwRead, NULL);
    CloseHandle(hFile);
    s_dwMusicSize = dwRead;
    s_dwMusicPos = 0;

    mp3dec_init(&s_mp3Dec);
    mp3dec_decode_frame(&s_mp3Dec, s_pMusicData, (int)s_dwMusicSize,
        pcm, &info);
    if (!info.hz || !info.channels)
    {
        free(s_pMusicData); s_pMusicData = NULL; return;
    }

    mp3dec_init(&s_mp3Dec);
    s_dwMusicPos = 0;
    s_bLoopTrack = bLoop;
    s_dwCarryBytes = 0;
    InterlockedExchange(&s_bEOF, 0);

    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)info.channels;
    wfx.nSamplesPerSec = (DWORD)info.hz;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * 2;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = AUDIO_STREAM_BUFSIZE;
    desc.lpwfxFormat = &wfx;
    /* lpMixBins intentionally NULL */

    if (FAILED(g_pDS->CreateSoundBuffer(&desc, &s_pMusicBuf, NULL)))
    {
        free(s_pMusicData); s_pMusicData = NULL; return;
    }

    s_nWriteHalf = 0;
    FillHalf(0);
    FillHalf(1);

    s_pMusicBuf->Play(0, 0, DSBPLAY_LOOPING);

    InterlockedExchange(&s_bMusicStop, 0);
    s_hMusicThread = CreateThread(NULL, 0, MusicThreadProc, NULL, 0, NULL);
    if (s_hMusicThread)
        SetThreadPriority(s_hMusicThread, THREAD_PRIORITY_HIGHEST);
}

/* =========================================================================
   Public API
========================================================================= */

void Audio_Init(void)
{
    int i;

    ZeroMemory(s_sfx, sizeof(s_sfx));
    for (i = 0; i < AUDIO_SFX_MAX; i++) s_sfx[i].bUsed = 0;
    InitializeCriticalSection(&s_cs);
    s_pMusicBuf = NULL;
    s_pMusicData = NULL;
    s_hMusicThread = NULL;
    s_bMusicStop = 0;
    s_bEOF = 0;
    s_bInGameplay = 0;
}

void Audio_Update(void)
{
    /* Playlist advancement removed -- gameplay music loops a single track.
       Reserved for future per-frame audio work (fade, ducking, etc.)    */
}

void Audio_Shutdown(void)
{
    int i;
    Audio_MusicStop();
    DeleteCriticalSection(&s_cs);
    for (i = 0; i < AUDIO_SFX_MAX; i++)
    {
        if (s_sfx[i].pBuf) { s_sfx[i].pBuf->Release(); s_sfx[i].pBuf = NULL; }
        s_sfx[i].bUsed = 0;
    }
}

/* ── Music entry points ── */

void Audio_MusicPlayTrack(int n)
{
    if (n < 0) n = 0;
    if (n > 6) n = 6;
    s_bInGameplay = 0;
    StartStream(n, 1 /* loop */);
}

int Audio_MusicGetTrack(void)
{
    return s_nCurrentTrack;
}

DWORD Audio_MusicGetElapsedMs(void)
{
    if (!Audio_MusicIsPlaying()) return 0;
    return GetTickCount() - s_dwTrackStart;
}

void Audio_MusicPlayTitle(void)
{
    s_bInGameplay = 0;
    StartStream(TRACK_TITLE, 1 /* loop */);
}

void Audio_MusicPlayGameplay(void)
{
    int nTrack = TRACK_GP_FIRST + (int)((float)(TRACK_GP_COUNT) * ((float)(GetTickCount() & 0xFFFF) / 65535.0f));
    if (nTrack < TRACK_GP_FIRST) nTrack = TRACK_GP_FIRST;
    if (nTrack > TRACK_GP_LAST) nTrack = TRACK_GP_LAST;
    s_bInGameplay = 0;   /* no playlist advancement needed */
    StartStream(nTrack, 1 /* loop */);
}

void Audio_MusicPlayCredits(void)
{
    s_bInGameplay = 0;
    StartStream(TRACK_CREDITS, 0 /* one-shot */);
}

void Audio_MusicStop(void)
{
    s_bInGameplay = 0;

    if (s_hMusicThread)
    {
        InterlockedExchange(&s_bMusicStop, 1);
        WaitForSingleObject(s_hMusicThread, 2000);
        CloseHandle(s_hMusicThread);
        s_hMusicThread = NULL;
    }
    InterlockedExchange(&s_bMusicStop, 0);

    if (s_pMusicBuf)
    {
        s_pMusicBuf->Stop(); s_pMusicBuf->Release(); s_pMusicBuf = NULL;
    }

    if (s_pMusicData)
    {
        free(s_pMusicData); s_pMusicData = NULL;
    }

    s_dwMusicSize = 0;
    s_dwMusicPos = 0;
}

void Audio_MusicVolume(int nVol)
{
    if (s_pMusicBuf) s_pMusicBuf->SetVolume(VolToDB(nVol));
}

int Audio_MusicIsPlaying(void)
{
    DWORD dwStatus;
    if (!s_pMusicBuf) return 0;
    s_pMusicBuf->GetStatus(&dwStatus);
    return (dwStatus & DSBSTATUS_PLAYING) ? 1 : 0;
}

/* ── SFX ── */

int Audio_SfxLoad(const char* pszPath)
{
    IDirectSoundBuffer* pBuf;
    int i;
    for (i = 0; i < AUDIO_SFX_MAX; i++)
        if (!s_sfx[i].bUsed) break;
    if (i == AUDIO_SFX_MAX) return -1;
    pBuf = LoadWAV(pszPath);
    if (!pBuf) return -1;
    s_sfx[i].pBuf = pBuf;
    s_sfx[i].bUsed = 1;
    return i;
}

void Audio_SfxPlay(int iSlot)
{
    if (iSlot < 0 || iSlot >= AUDIO_SFX_MAX || !s_sfx[iSlot].pBuf) return;
    s_sfx[iSlot].pBuf->SetCurrentPosition(0);
    s_sfx[iSlot].pBuf->Play(0, 0, 0);
}

void Audio_SfxPlayLooping(int iSlot)
{
    if (iSlot < 0 || iSlot >= AUDIO_SFX_MAX || !s_sfx[iSlot].pBuf) return;
    s_sfx[iSlot].pBuf->SetCurrentPosition(0);
    s_sfx[iSlot].pBuf->Play(0, 0, DSBPLAY_LOOPING);
}

void Audio_SfxStop(int iSlot)
{
    if (iSlot < 0 || iSlot >= AUDIO_SFX_MAX || !s_sfx[iSlot].pBuf) return;
    s_sfx[iSlot].pBuf->Stop();
}

void Audio_SfxVolume(int iSlot, int nVol)
{
    if (iSlot < 0 || iSlot >= AUDIO_SFX_MAX || !s_sfx[iSlot].pBuf) return;
    s_sfx[iSlot].pBuf->SetVolume(VolToDB(nVol));
}