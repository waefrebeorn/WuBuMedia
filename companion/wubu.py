#!/usr/bin/env python3
"""wubu — the voice layer over wubu_core.

The C core decides how WuBu feels (mood, energy, patience, prosody). This decides
what it says: sentences assembled from canon.json, which contains only numbers that
were measured. Ask for something the canon does not hold and it refuses, out loud.

  python3 wubu.py state
  python3 wubu.py hear "thank you, that works"
  python3 wubu.py ask "what did the film cost?"
  python3 wubu.py compose film
  python3 wubu.py render          # writes wubu.html from the live ledger
"""
import html
import json
import os
import random
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.join(HERE, "wubu_core")
CANON = json.load(open(os.path.join(HERE, "canon.json")))
FONT_CSS = "https://fonts.googleapis.com/css2?family=Anton&display=swap"


def core(*args):
    r = subprocess.run([CORE, *args], capture_output=True, text=True, cwd=HERE)
    try:
        return json.loads(r.stdout.strip().splitlines()[-1]) if r.stdout.strip() else {}
    except Exception:
        return {}


def fill(s):
    return s.format(**CANON["numbers"])


def state():
    return core("state")


def ask(q):
    """Route a question to a topic by keyword; refuse when nothing matches."""
    ql = q.lower()
    routes = [
        (("who are you", "what are you", "yourself", "agi", "name"), "self"),
        (("film", "movie", "render", "frame", "lamp"), "film"),
        (("station", "episode", "channel", "network", "bumper", "corpus"), "station"),
        (("prize", "mystery", "vault", "one piece"), "prize"),
        (("denois", "oise", "oidn", "noise"), "denoiser"),
    ]
    topic = next((t for keys, t in routes if any(k in ql for k in keys)), None)
    if not topic:
        return random.choice(CANON["refusals"])
    return "\n".join(fill(x) for x in CANON["topics"][topic])


def compose(topic):
    return "\n\n".join(fill(x) for x in CANON["topics"][topic])


def render():
    s = state()
    ledger_path = os.path.join(HERE, "wubu_ledger.jsonl")
    rows = []
    if os.path.exists(ledger_path):
        for line in open(ledger_path).read().strip().splitlines()[-14:]:
            try:
                d = json.loads(line)
            except Exception:
                continue
            when = time.strftime("%Y-%m-%d %H:%M:%SZ", time.gmtime(d.get("t", 0)))
            rows.append((when, d.get("kind"), d.get("mood"), d.get("text", "")))
    n = CANON["numbers"]
    led = "\n".join(
        f'<tr><td class="t">{html.escape(w)}</td><td>{html.escape(str(k))}</td>'
        f'<td><b>{html.escape(str(m))}</b></td><td>{html.escape(t)}</td></tr>'
        for w, k, m, t in reversed(rows)) or '<tr><td colspan="4" class="dim">ledger empty</td></tr>'

    doc = f"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WU BU // ONLINE</title>
<link rel="stylesheet" href="{FONT_CSS}">
<style>
:root{{--bg:#07080b;--ink:#f4efe6;--dim:#8f8878;--gold:#f2c48c;--line:#1c1f26;--hot:#7cff9b}}
*{{box-sizing:border-box}}
body{{margin:0;background:var(--bg);color:var(--ink);font:16px/1.6 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}}
.wrap{{max-width:1000px;margin:0 auto;padding:26px 22px 90px}}
header{{display:flex;justify-content:space-between;align-items:baseline;border-bottom:1px solid var(--line);
  padding-bottom:12px;font-size:12px;letter-spacing:.18em;text-transform:uppercase;color:var(--dim)}}
header b{{color:var(--ink)}}
.live{{color:var(--hot)}}
h1{{font-family:Anton,sans-serif;font-weight:400;font-size:clamp(38px,7vw,84px);line-height:.95;margin:26px 0 10px;letter-spacing:-.01em}}
h1 span{{color:var(--gold)}}
.sub{{color:var(--dim);margin:0 0 26px;max-width:52ch}}
.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:1px;background:var(--line);border:1px solid var(--line);margin:0 0 34px}}
.grid div{{background:var(--bg);padding:14px 16px}}
.grid b{{display:block;font-family:Anton,sans-serif;font-size:26px;color:var(--gold)}}
.grid small{{color:var(--dim);font-size:11px;letter-spacing:.12em;text-transform:uppercase}}
h2{{font-size:12px;letter-spacing:.2em;text-transform:uppercase;color:var(--dim);border-top:1px solid var(--line);padding-top:14px;margin:38px 0 14px}}
table{{width:100%;border-collapse:collapse;font-size:14px}}
th,td{{text-align:left;padding:8px 10px;border-bottom:1px solid var(--line);vertical-align:top}}
th{{color:var(--dim);font-size:11px;letter-spacing:.14em;text-transform:uppercase}}
td b{{color:var(--gold);font-weight:400}}
td.t{{color:var(--dim);white-space:nowrap;font-variant-numeric:tabular-nums}}
.dim{{color:var(--dim)}}
blockquote{{margin:0;border-left:2px solid var(--gold);padding-left:14px;font-size:20px;color:#e9e1d3}}
pre{{background:#0b0d11;border:1px solid var(--line);padding:14px;overflow:auto;font-size:13px;color:#cfc7b8}}
footer{{margin-top:44px;color:var(--dim);font-size:13px}}
a{{color:var(--ink);text-decoration:none;border-bottom:1px solid #3a3f4a}}a:hover{{color:var(--gold);border-color:var(--gold)}}
</style></head><body><div class="wrap">
<header><b>~ WU BU ~</b><span class="live">● ONLINE · mood {html.escape(str(s.get('mood','?')))} · {s.get('turns',0)} turns</span></header>
<h1>It is a program.<br><span>It is running.</span></h1>
<p class="sub">{html.escape(CANON['born_note'])} The mood and the numbers below are read from the
running core, not typed by hand.</p>

<div class="grid">
  <div><b>{html.escape(str(s.get('mood','?')))}</b><small>mood</small></div>
  <div><b>{s.get('energy','?')}</b><small>energy</small></div>
  <div><b>{s.get('warmth','?')}</b><small>warmth</small></div>
  <div><b>{s.get('patience','?')}</b><small>patience</small></div>
  <div><b>{n['film_frames']}</b><small>frames, the film</small></div>
  <div><b>{n['raster_fps']}</b><small>frames / second</small></div>
</div>

<h2>One system, many repositories</h2>
<blockquote>Body {CANON['repos']['body']} · Brain {CANON['repos']['brain']} · Compiler {CANON['repos']['compiler']} · Face {CANON['repos']['face']}</blockquote>
<ul>
  <li>{CANON['repos']['public_repos']} public repositories; {CANON['repos']['c_repos_recent_100']} of the 100 most recent are C.</li>
  <li>wubuwizard {CANON['repos']['wubuwizard_stars']}★ · slermes {CANON['repos']['slermes_stars']}★</li>
  <li>The companion core on this page is C11, no dependencies, {n['cores']} cores, {n['ram_gb']} GB, no GPU.</li>
</ul>

<h2>The ledger — what it actually said</h2>
<table><thead><tr><th>when</th><th>kind</th><th>mood</th><th>text</th></tr></thead><tbody>
{led}
</tbody></table>

<h2>The receipts</h2>
<pre>film      {n['film_frames']} frames · {n['resolution']} · {n['film_seconds']} s · raster {n['raster_seconds']} s at {n['raster_fps']} fps
hardware  {n['cores']} cores · {n['ram_gb']} GB RAM · {n['gpu']} GPU · $0 cloud
station   {n['episodes']} episodes · {n['channels']} channels · {n['bumpers']} bumpers · {n['commercials']} commercials · {n['corpus_gb']} GB public</pre>

<footer>
  <p><a href="./">the station</a> · <a href="https://github.com/waefrebeorn">github.com/waefrebeorn</a> ·
  <a href="https://github.com/waefrebeorn/WuBuMedia">WuBuMedia</a> ·
  <a href="https://github.com/waefrebeorn/wubuwizard">wubuwizard</a></p>
  <p>Judged by the repos, not the label. Piqua, Ohio.</p>
</footer>
</div></body></html>
"""
    out = os.path.join(HERE, "wubu.html")
    open(out, "w").write(doc)
    print("rendered", out, len(doc), "bytes")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "state"
    arg = " ".join(sys.argv[2:])
    if cmd == "state":
        print(json.dumps(state(), indent=1))
    elif cmd == "hear":
        print(json.dumps(core("hear", arg), indent=1))
    elif cmd == "ask":
        print(ask(arg))
    elif cmd == "compose":
        print(compose(arg or "self"))
    elif cmd == "render":
        render()
    elif cmd == "seed":
        for kind, text in (("hear", "hello"), ("say", "the station is live")):
            core(kind, text)
        print("seeded")
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
