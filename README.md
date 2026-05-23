# ScorchedXB

An original Xbox port of **Scorched Earth 2000** (scorch.js v1.2), built with the RXDK/XDK toolchain in C89-compatible C++.

Developed by **Darkone83** · Presented by **Team Resurgent**

---

## Overview

ScorchedXB is a turn-based artillery game for 1–4 players on original Xbox hardware. One human player faces off against up to three AI opponents across procedurally generated terrain. Choose your tank, configure your loadout in the weapon store, set your angle and power, account for wind, and fire.

The game features cel-shaded 3D tanks rendered over a 2D destructible terrain, a full DirectSound audio engine with MP3 music streaming and 25 sampled SFX, and three AI difficulty levels ported directly from the scorch.js source.

---

## Features

- **Destructible terrain** — pixel-level terrain deformation with gravity drop after each explosion
- **19 weapons** — Missile, Baby Nuke, Nuke, Napalm, Hot Napalm, Baby Roller, Roller, Heavy Roller, Sand Bomb, Funky Bomb, Funky Nuke, MIRV, Death Head, Laser, Baby Digger, Digger, Heavy Digger, Shield, Parachute
- **3 AI difficulty levels** — Shooter, Cyborg, Killer; two-pass angle/power grid search with per-difficulty inaccuracy and weapon preference curves
- **5 tank types** — Recon, Medium, Assault, Artillery, Siege; each with distinct silhouette and barrel profile
- **Weapon store** — buy and sell weapons between rounds using cash earned from kills
- **Cel-shaded 3D tanks** — orthographic projection, toon shading with outline pass, per-player colors
- **Point sprite particle system** — explosion sparks and dirt debris with gravity and alpha fade
- **MP3 music streaming** — minimp3-based ring buffer engine; title, gameplay (random shuffle from 5 tracks), and credits tracks
- **Original soundtrack** — composed by Darkone83
- **25 sampled SFX** — per-weapon fire sounds, explosion variants scaled by radius, terrain impact, tank destruction, and looping battlefield ambience
- **Controller rumble** — scaled by explosion radius, configurable in options
- **Wall types** — None, Wrap, or Bounce projectile behavior at screen edges
- **Persistent config** — music volume, SFX volume, rumble toggle, wall type saved to `D:\ScorchedXB.cfg`
- **Intro XMV** — WMV2 video with ADPCM audio via xmvtool
- **Credits sequence** — scrolling credits with dedicated music track

---

## Requirements

### Hardware
- Original Xbox (any revision)
- Modded or dev kit (RXDK or XDK build)

### Build Environment
- Microsoft Xbox SDK (XDK) or RXDK
- Visual Studio 2003 (MSVC 7.1)
- xmvtool (for intro video conversion)
- ffmpeg (for source video → WMV2 conversion)

---

## Building

1. Open `ScorchedXB.sln` in Visual Studio 2003
2. Set build configuration to **Xbox Release**
3. Build solution — all source files are flat in the project root

> **Important:** `ftol2.cpp` must have **Whole Program Optimization (`/GL`) disabled** as a per-file property. This file provides the `_ftol2_sse` stub required by MSVC 2003 on Xbox.

---

## Asset Layout

All assets are loaded from `xberoot` (the game partition). The following directory structure is required:

```
D:\
├── ScorchedXB.xbe
├── ScorchedXB.cfg          (written on first options save)
├── xmv\
│   └── Intro.xmv           (converted with Convert_to_XMV.bat)
├── tracks\
│   ├── track0.mp3           (title music, loops)
│   ├── track1.mp3           (gameplay, random pick)
│   ├── track2.mp3
│   ├── track3.mp3
│   ├── track4.mp3
│   ├── track5.mp3
│   └── track6.mp3           (credits, plays once)
├── sfx\
│   ├── cannon_fire_01.wav
│   ├── cannon_fire_short.wav
│   ├── cannon_fire_heavy.wav
│   ├── cannon_fire_alt.wav
│   ├── cannon_fire_dry.wav
│   ├── weapon_fire_medium.wav
│   ├── weapon_fire_click.wav
│   ├── airburst_01.wav
│   ├── airburst_02.wav
│   ├── missile_pop_01.wav
│   ├── cluster_pop_01.wav
│   ├── explosion_small_01.wav
│   ├── explosion_small_02.wav
│   ├── explosion_medium_01.wav
│   ├── explosion_medium_02.wav
│   ├── explosion_heavy_01.wav
│   ├── explosion_heavy_02.wav
│   ├── impact_dirt_01.wav
│   ├── impact_dirt_02.wav
│   ├── tank_destroyed_01.wav
│   ├── tank_destroyed_02.wav
│   └── battlefield_loop.wav
└── tex\
    ├── splash.dds           (title screen background)
    └── ui.dds               (menu/options background)
```

## Controls

### Menu / Options
| Button | Action |
|---|---|
| D-Pad Up / Down | Navigate |
| D-Pad Left / Right | Change value (options) |
| A | Confirm / select |
| B | Back |

### In-Game
| Button | Action |
|---|---|
| D-Pad Left / Right | Adjust angle |
| D-Pad Up / Down | Adjust power |
| Black | Next weapon |
| White | Previous weapon |
| A | Fire |
| Back | Return to menu |

---

## Source Structure

All source files are flat in the project root.

| File | Description |
|---|---|
| `main.cpp` | Entry point, state machine (Video → Title → Menu → Setup → Store → Game → Results → Credits) |
| `render.h/cpp` | D3D8 device init, display mode detection |
| `input.h` | XInput wrapper (provided) |
| `font.h/cpp` | Custom 5×7 "Combat Stencil" bitmap font, no external dependencies |
| `ui.h/cpp` | Texture backgrounds, font size wrappers, title screen |
| `tex.h/cpp` | DDS texture loading via D3DX |
| `video.h/cpp` | Blocking XMV playback via XMVDecoder |
| `audio.h/cpp` | minimp3 MP3 streaming engine, DirectSound ring buffer |
| `sfx.h/cpp` | 25-slot DirectSound SFX pool |
| `terrain.h/cpp` | Procedural terrain generation and destruction |
| `random.h/cpp` | LCG RNG matching scorch.js Random class exactly |
| `player.h/cpp` | Player state, tank placement, turret position |
| `tanks.h/cpp` | Cel-shaded 3D tank rendering (orthographic, toon + outline) |
| `physics.h/cpp` | Projectile simulation, terrain/tank intersection, damage, settling |
| `weapons.h/cpp` | 19-weapon dispatch table, terrain carve implementations |
| `particles.h/cpp` | Fixed-pool point sprite particle system |
| `ai.h/cpp` | Two-pass grid search AI with Gaussian weapon selection |
| `game.h/cpp` | Game loop, turn management, SFX/rumble wiring |
| `setup.h/cpp` | Pre-game setup screen (tank type, color, AI config) |
| `store.h/cpp` | Weapon store (pre-game loadout and between-round restock) |
| `results.h/cpp` | Round and game-over results screen |
| `menu.h/cpp` | Main menu |
| `help.h/cpp` | Controls reference screen |
| `options.h/cpp` | Options screen |
| `credits.h/cpp` | Scrolling credits sequence |
| `config.h/cpp` | Persistent config read/write (`D:\ScorchedXB.cfg`) |
| `ftol2.cpp` | `_ftol2_sse` stub for MSVC 2003 (build with `/GL` disabled) |
| `Convert_to_XMV.bat` | Intro video conversion script |

---

## Credits

| Role | Credit |
|---|---|
| Xbox Port | Darkone83 |
| Presented by | Team Resurgent |
| Original Game | scorch.js v1.2 |
| Xbox Community | Xbox-Scene |

---

## License

This project is a fan port for preservation and homebrew purposes on original Xbox hardware. The original Scorched Earth 2000 game logic (scorch.js) belongs to its respective author.