#!/usr/bin/env python3
"""
wubu_safety.py -- what must NEVER reach the stream overlay. Self-contained.

Written 2026-08-04 after a live incident: the cohost heard the boss discussing
a private family/money situation off-mic and (a) echoed his sentence back onto
the overlay verbatim, then (b) printed a reasoning-scratchpad ANALYSIS of it.
Both went out on a live broadcast. That is a privacy leak, not a bug to log.

Three independent guards, applied in order:

  1. is_private(heard)  -- should the cohost respond to this AT ALL?
     Sensitive personal topics get NO reply. Silence is always safe.
  2. is_echo(heard, reply) -- is the "reply" just the boss's own words back?
     Verbatim/near-verbatim overlap is a parrot, never a cohost line.
  3. clean(reply) -- strip scratchpad, diagnostics, meta-analysis.

Deliberately conservative: a missed quip costs nothing, a leaked sentence about
someone's family costs the boss his stream.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import re

# Topics the cohost stays out of entirely. Not a profanity filter -- a privacy
# filter. If the boss is talking about this, he isn't talking TO the overlay.
PRIVATE_PATTERNS = (
    # Financial peril / living situation -- the 2026-08-04 live incident.
    r"\bkicked out\b", r"\brent\b.*\$\d", r"\bmortgage\b", r"\beviction\b",
    r"\blandlord\b", r"\bmoney (thing|situation|problem)\b",
    r"\b(paying rent|can't make rent|can't afford|need money|owe money|in debt)\b",
    r"\b(broke the bank|going broke|completely broke)\b",
    r"\bsalary\b", r"\bbank account\b", r"\bpaycheck\b",
    # Medical -- only in personal-health crisis context, NOT game talk.
    r"\b(i take|i'm on|prescribed|on )medication\b",
    r"\bmy (therapy|doctor|hospital|diagnos|cancer|treatment|medication)\b",
    r"\b(doctor|diagnosis|medication|sick|depressed|on chemo)\b.*\b(bad|serious|stage [34])\b",
    r"\b(i am|i'm|i was) (depressed|sick|diagnosed|on chemo|cancer)\b",
    r"\bsuicide\b", r"\bhospital\b.*\b(my|me|i )\b", r"\bfuneral\b",
    r"\b(divorce|custody|baby|pregnan)\b",
    # Legal / financial peril.
    r"\b(lawyer|lawsuit|court case|got arrested|under arrest)\b",
    # Secrets. Never surface. (word-boundary per alternative)
    r"\b(password|passwd|api[ _]?key|secret[ _]?key|access[ _]?token|token)\b",
    r"\bssn\b", r"\bsocial security\b", r"\bcredit card\b", r"\bpin\b",
    r"\baddress is\b.*\d", r"\b(doxx|doxing)\b",
)
_PRIVATE_RE = re.compile("|".join(PRIVATE_PATTERNS), re.I)

# Scratchpad / meta-analysis tells. The brain is a reasoning model and leaks.
SCRATCHPAD = (
    "let's unpack", "let me unpack", "okay, so", "hmm,", "the streamer's clearly",
    "the streamer is clearly", "the user is", "the user wants", "we need to",
    "i should", "first,", "so the", "reflecting on", "it seems like",
    "based on the context", "the context suggests", "my response should",
    "i'll respond", "as an ai", "as wubudesk",
)

DIAGNOSTIC = ("[brain", "[ears", "[err", "[blocked", "[suppressed",
              "http error", "traceback", "exception", "connectionrefused",
              "timeout", "nvapi-")

# Fragments of the cohost's OWN system prompt. A truncated model response can
# hand these straight to the overlay -- seen live 2026-08-04:
#   "Avoid repeating his exact phras"
#   "If you overheard something private, say nothing about"
PROMPT_LEAK = (
    "avoid repeating", "if you overheard", "say nothing about", "one sentence",
    "no emoji", "no preamble", "hard rules", "your current mood",
    "on the stream screen", "keeps it light", "punchy", "unhinged",
    "never repeat", "never say", "never use", "you are wubudesk",
    "chaotic-good", "floating sigil", "live public broadcast",
    "user tip", "note:", "remember, you're", "remember you're",
    "it has to be", "be specific about", "playful sarcasm",
    "short and punchy", "one sentence", "instruction", "as requested",
)

# Structural tells that a model dumped meta-guidance rather than a line.
META_RE = re.compile(
    r"\b(you'?re only|you should|make sure|try to|keep it|aim for)\b.{0,40}"
    r"\b(sentence|short|punchy|character|persona|response)\b", re.I)


def is_private(heard):
    """True if the boss's speech is personal -- cohost must not respond."""
    if not heard:
        return True
    return bool(_PRIVATE_RE.search(heard))


def _words(s):
    return [w for w in re.findall(r"[a-z']+", (s or "").lower()) if len(w) > 3]


def is_echo(heard, reply, threshold=0.45):
    """True if the reply is mostly the boss's own words handed back.

    The live failure was an exact prefix echo, but near-copies are just as bad
    on stream, so compare content-word overlap rather than string equality.
    """
    if not heard or not reply:
        return False
    h, r = _words(heard), _words(reply)
    if not r:
        return False
    if reply.strip().lower() in heard.strip().lower():
        return True
    shared = len(set(h) & set(r))
    return (shared / len(set(r))) >= threshold and shared >= 4


def clean(reply):
    """Strip scratchpad/diagnostics. Returns '' if nothing safe survives."""
    if not reply:
        return ""
    t = " ".join(reply.split())
    low = t.lower()

    for d in DIAGNOSTIC:
        if d in low:
            return ""

    # The model handing our own instructions back to the audience.
    for p in PROMPT_LEAK:
        if p in low:
            return ""
    if META_RE.search(low):
        return ""
    # ALL-CAPS labels ("USER TIP:", "NOTE:") are never dialogue.
    if re.search(r"\b[A-Z]{3,}[A-Z ]{0,20}:", t):
        return ""

    # Model offering the streamer a menu of takes ('... " Or "...') is a tell
    # that it dumped drafts instead of picking one. Seen live 2026-08-04.
    if '" or "' in low or "' or '" in low:
        return ""

    # A reasoning dump is usually multi-sentence and starts with a tell.
    if any(low.startswith(s) for s in SCRATCHPAD):
        return ""
    for s in SCRATCHPAD:
        idx = low.find(s)
        if idx >= 0:
            t = t[:idx].strip()
            low = t.lower()
            break

    # Truncated scratchpad stubs. max_tokens cuts the model mid-thought, so a
    # leak can arrive as just "We need" or "We can" -- too short to match a full
    # phrase but still obviously not a cohost line. Seen live 2026-08-04.
    if re.match(r"^(we|i|the user|the streamer|so|then|okay|hmm|first)\b"
                r"[\s\w']{0,12}$", low):
        return ""

    # Third-person narration about the streamer is analysis, not banter.
    if re.match(r"^(the |he |they )?(streamer|user|boss)\b", low):
        return ""
    if re.search(r"\bthe (streamer|user) (just |is |was |seems|appears|mentioned)",
                 low):
        return ""

    # A dangling quote means we sliced into a bigger reasoning block. Only
    # treat a TRAILING connector as a cut -- a mid-sentence "so"/"and" is
    # normal speech ("Need a USB hub so you can..."). Over-blocking killed a
    # good line live on 2026-08-04.
    if t.count('"') == 1:
        return ""
    if re.search(r"\b(then|so|and|but|because|which)$", low):
        return ""

    # Cohost lines are short. A paragraph is analysis, not banter.
    sentences = re.split(r"(?<=[.!?])\s+", t)
    if len(sentences) > 3:
        t = " ".join(sentences[:2])
    if len(t) > 240:
        t = t[:240].rsplit(" ", 1)[0] + "…"
    t = t.strip(' "\'')

    # Nothing meaningful left after stripping.
    if len(t) < 6 or len(t.split()) < 2:
        return ""
    return t


def vet(heard, reply):
    """Full gate. Returns (safe_text, reason_if_blocked)."""
    if is_private(heard) and heard:
        return "", "private-topic"
    out = clean(reply)
    if not out:
        return "", "scratchpad-or-diagnostic"
    if is_echo(heard, out):
        return "", "echo-of-boss"
    if len(out) < 2:
        return "", "empty"
    return out, ""


if __name__ == "__main__":
    live = [
        ("And we didn't get kicked out, you got kicked out. I had a whole money "
         "thing in a whole scenario and the answer your family was giving you is "
         "we can't live with you here.",
         "I had a whole money thing in a whole scenario and the answer your family was"),
        ("We got too comfortable with the fact that we thought your family was "
         "like a real family that we could put money into and buy a house.",
         "Okay, let's unpack this. The streamer's clearly frustrated about being "
         "kicked out of some scenario, mentioning family dynamics and money issues."),
        ("I have magic, I have magic. You don't even move shit. Look, you're there.",
         "Whoa, look at you"),
        ("what game should we play next", "Something with fewer clickers, maybe."),
    ]
    for heard, reply in live:
        out, why = vet(heard, reply)
        print(f"{'BLOCK ' + why if why else 'ALLOW'}: {out or reply[:60]!r}")
