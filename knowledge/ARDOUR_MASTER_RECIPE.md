# Learning the Boss's Ardour Masters — Quahog Golden Album

Extracted 2026-08-08 from the boss's own Ardour 8.10 sessions (Documents/*.ardour XML).
Goal: replicate his mastering so the Cleveland AI album matches his reference masters.

## The recipe (per session, e.g. clevelandisgolden.ardour)

The AI vocal lives on its own track named "cleveland" and carries THREE LV2
processors (Ardour's built-in ACE series) before hitting the mix bus:

1. **ACE Expander (stereo)** — `urn:ardour:a-exp#stereo`
   - Attack 0.1 ms, Release 35.1 ms, Knee 0
   - Ratio 2.76, Threshold -47.3 dB
   - **Makeup gain +8.01 dB** ← THE stage presence
2. **ACE EQ** — `urn:ardour:a-eq` — enabled, ALL bands at 0 dB (flat pass)
3. **ACE Reverb** — `urn:ardour:a-reverb` — Blend 0.0165 (1.6%), Room 0.5

Every instrument stem sits at unity fader (0.0 dB), pans L/R, nothing muted.
The master bus is clean (no plugins) → the loudness is achieved at EXPORT
(the boss's "extended LTS -18db" normalization).

## Measured reference (his 9 OGG masters)

- RMS: **-17.6 to -20.3 dBFS** ("extended LTS -18 db")
- Peaks: **0.86 - 0.98** (hot; Apple-class -1 dBTP headroom)
- Vocal-band energy 300-3400 Hz: **0.46 - 0.78** of total (voice carries the mix)
- Stereo corr 0.86 - 0.95

## The mix fix (what was wrong before)

Old mixes: lead vocal at gain 1.1 over 16 instrument channels → RMS -22 dBFS,
vocal-band 0.26 — the singer was DROWNED ("dont drown my singer out").

Fixed recipe (album_build.py):
- Lead vocal gain **2.512 (= +8.01 dB)** — the ACE Expander makeup, replicated
- All other stems at unity, stereo pairs panned L/R
- Master: -18 dBFS RMS target, -1 dBTP ceiling (matches his exports)

Result on vocal sections: **-19.0 dBFS vs his -18.3 dBFS** (0.7 dB) — matched.

## Track → Ardour project mapping (lyrics + f0 + duration)

| Track | Song | Ardour project | Master ogg |
|---|---|---|---|
| 1 | G.O.L.D. | cleveGOLDhope | 241.0 s |
| 2 | Freestyle Driving Lesson | clevespooner | 206.6 s |
| 3 | Cleveland Sings Golden | clevegoldenkpop | 112.8 s |
| 4 | Seth's Lament | (no project) | 251.4 s (his master as-is) |
| 5 | Slumbers in Gold | cleveslumbergold | 131.8 s |
| 6 | Bring My Cleveland Back | kissrose4track= | 311.2 s |
| 7 | Cleveland Is Golden | clevelandisgolden | 209.2 s |
| 8 | 24K Cleveland | 20kcleeveland | 199.6 s |
| 9 | Cleveland's Quahog Hour | clevelandquohoghour | 219.3 s |

## Tools

- `tools/album_build.py` — slice → Cleveland RVC convert (--autokey) →
  Ardour-recipe mix → -18 master. Usage:
  `python tools/album_build.py <project_dir> <lead_pattern> <out_name>`
- `tools/wubu_mixmaster.c` — stem parser fixed to parse `path:gain:pan` from
  the RIGHT (strrchr) so Windows drive letters ("C:\...") no longer break.
