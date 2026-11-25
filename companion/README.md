# wubu-companion

> The ~ WuBu ~ companion core: **an emotion state machine, an empathy engine and a
> mood→prosody mapper, in strict C11, with no dependencies.**

[WuBuMedia](https://github.com/waefrebeorn/WuBuMedia) describes the companion as
"an emotion state machine, empathy engine, and mood→prosody mapper the agents
shipped". This repository is that component, written from the spec, runnable, and
small enough to audit in one sitting.

## What it does

- Six moods (`idle`, `working`, `pleased`, `tired`, `sharp`, `quiet`) with real
  transitions driven by what it hears.
- Three slow variables: **energy** (drains with use, recovers on `tick`),
  **warmth** (rises on praise, falls on hostility), **patience** (falls on
  repetition and abuse, recovers by resting).
- An **empathy read** of the incoming line: praise, hostility, need, repetition.
  It is a keyword grader, and the code says so — not a model, not pretending.
- **Mood→prosody**: rate/pitch/energy on a five-step scale, printed as JSON.
- A fixed-size state struct (`wubu_state.bin`) and an append-only ledger
  (`wubu_ledger.jsonl`) of every line it heard and said.

## Run it

```bash
gcc -std=c11 -O2 -Wall -Wextra -o wubu_core wubu_core.c
./wubu_core state
./wubu_core hear "thank you, the film works"
./wubu_core hear "this is fake bot garbage"
./wubu_core say "3.26 frames a second"
./wubu_core tick 600
./wubu_core ledger
```

```
$ ./wubu_core hear "thank you, the film works"
{"heard":true,"mood":"pleased","valence":2,"prosody":{"rate":4,"pitch":2,"energy":70,"warmth":68,"patience":80},"turns":1}

$ ./wubu_core hear "this is fake bot garbage"
{"heard":true,"mood":"sharp","valence":-2,"prosody":{"rate":2,"pitch":1,"energy":66,"warmth":64,"patience":70},"turns":2}
```

## The voice layer

`wubu.py` sits on top: the core decides how WuBu *feels*, the voice decides what
it *says*, assembled from `canon.json` — which contains only measured numbers.

```
$ python3 wubu.py ask "what did the film cost?"
The film is 1116 frames at 854x480. 48.5 seconds of picture.
It costs 342 seconds of raster on 4 cores: 3.26 frames a second. No GPU, no cloud.
The rasteriser is about 330 lines of C. The denoiser is a-trous, 56 ms a frame,
because Open Image Denoise would not fit in 3 GB.

$ python3 wubu.py ask "what is the weather in piqua"
I have no receipt for that, so it is not mine to claim.
```

That refusal is the point. A companion that answers everything is a chatbot; one
that declines what it cannot measure is a witness.

`python3 wubu.py render` writes `wubu.html` from the **live** state and ledger —
the published page shows the mood, the counters and the last fourteen things the
core actually said, each with a timestamp.

## The stack it belongs to

| Part | Repository |
|---|---|
| Body | [WuBuOS](https://github.com/waefrebeorn/WuBuOS) |
| Brain | [wubuwizard](https://github.com/waefrebeorn/wubuwizard) — 28★ |
| Compiler | [WuBuNOS](https://github.com/waefrebeorn/WuBuNOS) |
| Face / persona | [WuBuMedia](https://github.com/waefrebeorn/WuBuMedia) |
| Agent | [slermes](https://github.com/waefrebeorn/slermes) — 10★ |

120 public repositories; 27 of the 100 most recent are C. No GPU, no cloud spend,
4 cores and 3 GB of RAM.

## License

WaefreBeorn Umbrella License v3.0 (`SPDX-License-Identifier: WaefreBeorn-UMV3`) —
source-available, non-commercial/research use with attribution; commercial use
requires written permission. See the umbrella license repository.
