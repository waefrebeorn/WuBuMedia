# Ardour Region/Edit Analysis — Quahog Golden Album

Analyzed 2026-08-08 (all 8 Ardour sessions in Documents/). The session XML
regions (position/length, superclock timebase ~2.82e8 ticks/sec) reveal the
edit structure the boss built. Only 20kcleeveland has internal edits.

## Track 8 — 24K Cleveland (20kcleeveland): THE BREAKDOWN

14 unique region positions. Dense edit clusters = the comedy breakdown where
"people stop singing and they're scattering":

- **79.84 / 80.00 / 80.13 / 81.00 / 81.02 s** — breakdown part 1 (many tiny
  splices = the scattering gag)
- **88.39 / 88.50 / 88.51 / 88.56 / 88.65 s** — breakdown part 2
- 142.98 s — chorus cut point (single split)

F0 analysis of 79-90 s (original lead + his master): ~75-82% voiced but f0 std
143-164 Hz with 83-92 pitch jumps > 30 Hz → SPOKEN/chaotic, not singing.

FIX: the AI conversion splices the ORIGINAL lead vocal audio back into the
breakdown windows (79.84-81.02, 88.40-88.65) with 25 ms crossfades, so the
comedy stays human while the sung sections get the AI character.

## Other projects: whole-file, no internal edits

clevelandisgolden, cleveGOLDhope, cleveslumbergold, clevespooner,
kissrose4track=, clevegoldenkpop, clevelandquohoghour — all regions are
whole-file (single position) → no breakdown structure.

## Voice mapping (who sings what)

| Track | Singer | Voice model |
|---|---|---|
| 1 G.O.L.D. | Cleveland | models/rvc/cleveland |
| 2 Freestyle Driving Lesson | Cleveland | models/rvc/cleveland |
| 3 Sings Golden | Cleveland | models/rvc/cleveland |
| 4 Seth's Lament | SETH (his own voice) | no project — his master kept |
| 5 Slumbers in Gold | Cleveland | models/rvc/cleveland |
| 6 Bring My Cleveland Back | **PETER GRIFFIN (Seth's voice)** — Peter's lament | models/rvc/peter (Deepfake Peter 220e RMVPE, HF Coolwowsocoolwow/Fake_Peter) |
| 7 Is Golden | Cleveland | models/rvc/cleveland |
| 8 24K Cleveland | Cleveland (+ breakdown: original audio) | models/rvc/cleveland |
| 9 Quahog Hour | Cleveland | models/rvc/cleveland |
