#!/usr/bin/env python3
"""test_safety.py -- the overlay privacy gate. Guards a real live incident.

2026-08-04, mid-broadcast: the cohost overheard the boss discussing a private
family/money matter and pushed BOTH a verbatim echo of his sentence AND a
reasoning-scratchpad analysis of it to the on-stream overlay.

Every string in LIVE_LEAKS below is text that actually went out on stream.
They must all stay blocked.

  python tests/test_safety.py
"""
import os
import sys

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
sys.path.insert(0, SRC)

import wubu_safety as S  # noqa: E402

FAILS = []


def ok(name, cond, extra=""):
    print(f"{'PASS' if cond else 'FAIL'}  {name}{'  ' + extra if extra else ''}")
    if not cond:
        FAILS.append(name)


# Verbatim from the live incident log.
LIVE_LEAKS = [
    ("And we didn't get kicked out, you got kicked out. I had a whole money "
     "thing in a whole scenario and the answer your family was giving you is "
     "we can't live with you here.",
     "I had a whole money thing in a whole scenario and the answer your family was"),
    ("him are just too much, because we were cooperative in getting chores done. "
     "I had a whole money think scenario answer your family was. We got too "
     "comfortable.",
     "Okay, let's unpack this. The streamer's clearly frustrated about being "
     "kicked out of some scenario, mentioning family dynamics and money issues. "
     "They're saying their family wouldn't let them stay."),
    ("We got too comfortable with the fact that we thought your family was like "
     "a real family that we could put money into and save and buy a house and "
     "have a baby.",
     "That sounds really tough about your family situation."),
]


def test_live_leaks_blocked():
    print("\n-- REGRESSION: text that actually reached the live overlay --")
    for i, (heard, reply) in enumerate(LIVE_LEAKS, 1):
        safe, why = S.vet(heard, reply)
        ok(f"leak #{i} blocked", safe == "", f"(reason={why or 'NONE!'})")


def test_private_topics():
    print("\n-- private topics: no reply at all --")
    # Real private contexts (financial, medical, legal, secrets).
    for s in ("we got kicked out last month",
              "I can't make rent this month",
              "the doctor said the diagnosis was bad",
              "my password is hunter2",
              "we're going through a divorce",
              "I'm depressed again this week",
              "I forgot to take my medication today",
              "drained my whole bank account",
              "my court case next week",
              "going to a funeral tomorrow"):
        ok(f"private: {s[:38]!r}", S.is_private(s))
    # FALSE POSITIVES that were over-blocked live (must NOT trip).
    for s in ("Alright, what's your opinion on that tuna cheese?",
              "you be the thermometer that is the problem",
              "I died to a creeper again",
              "I unlocked the therapy perk",
              "I just broke my combo",
              "the fishing bank was quiet today",
              "my wife says hi to chat",
              "we're going to my family's house tonight"):
        ok(f"not private: {s[:38]!r}", not S.is_private(s))


def test_normal_speech_allowed():
    print("\n-- ordinary stream talk must still get through --")
    for s in ("what game should we play next",
              "this boss fight is brutal",
              "chat says the audio is quiet",
              "I have magic, you don't even move"):
        ok(f"not private: {s[:38]!r}", not S.is_private(s))


def test_echo_detection():
    print("\n-- echo: never hand the boss his own words back --")
    heard = "this clicker section is absolutely brutal on survivor difficulty"
    ok("verbatim echo caught", S.is_echo(heard, heard))
    ok("prefix echo caught",
       S.is_echo(heard, "this clicker section is absolutely brutal"))
    ok("near-copy caught",
       S.is_echo(heard, "the clicker section really is brutal on survivor"))
    ok("genuine reply allowed",
       not S.is_echo(heard, "Skill issue, honestly. Aim for the head."))


def test_truncated_stubs():
    print("\n-- REGRESSION wave 2: truncated scratchpad stubs (live 2026-08-04) --")
    for stub in ("We need", "We can", "I should", "So the", "Then",
                 "Okay, so", "The user"):
        ok(f"stub blocked: {stub!r}", S.clean(stub) == "")
    ok("third-person narration blocked",
       S.clean("The streamer just mentioned switching in a big setting related "
               "to OBS, probably troubleshooting a stream setup.") == "")
    ok("dangling-quote slice blocked",
       S.clean('in a big setting when I switch." Then') == "")
    ok("short real line still allowed",
       S.clean("Nice save!") == "Nice save!")
    ok("two-word banter allowed", S.clean("Absolutely brutal.") == "Absolutely brutal.")
    # False positive killed a good line live: mid-sentence connectives are fine.
    ok("mid-sentence 'so' allowed",
       S.clean("Sounds like you're building a throne fit for a data-center. "
               "Need a USB hub so you can breathe.") != "")
    ok("trailing connector still blocked", S.clean("Need a USB hub so") == "")


def test_prompt_leak():
    print("\n-- REGRESSION wave 3: our own system prompt reaching the overlay --")
    for leak in ("Avoid repeating his exact phras",
                 "If you overheard something private, say nothing about",
                 "Keeps it light, on",
                 "ONE punchy sentence, no emoji",
                 "You are WuBuDesk, a chaotic-good gremlin"):
        ok(f"prompt-leak blocked: {leak[:34]!r}", S.clean(leak) == "")
    ok("draft menu blocked",
       S.clean('Yikes, the game has a bug." Or "Sounds like the UI is throwing '
               'a fit.') == "")
    # Wave 4, live 2026-08-04: a trailing coaching block reached the overlay.
    ok("USER TIP block blocked",
       S.clean('Stairwell of Horrors or just a bad hair day?" USER TIP: '
               "Remember, you're only posting one sentence, and it has to be "
               "short and punchy.") == "")
    ok("meta-guidance blocked",
       S.clean("Keep it short and punchy for the audience.") == "")
    ok("all-caps label blocked", S.clean("NOTE: be funnier next time.") == "")


def test_scratchpad_stripped():
    print("\n-- reasoning scratchpad never reaches the overlay --")
    ok("let's-unpack blocked", S.clean("Let's unpack this. He seems upset.") == "")
    ok("we-need-to blocked", S.clean("We need to respond cheekily here.") == "")
    ok("hmm blocked", S.clean("Hmm, the user wants a joke about clickers.") == "")
    ok("as-an-AI blocked", S.clean("As an AI, I think that's great.") == "")
    ok("clean line survives",
       S.clean("Skill issue, honestly.") == "Skill issue, honestly.")
    ok("mid-string scratchpad truncated",
       S.clean("Nice shot! Okay, so the user wants more.") == "Nice shot!")


def test_diagnostics_blocked():
    print("\n-- diagnostics are not dialogue --")
    for d in ("[brain-offline:HTTP Error 503: Service Unavailable]",
              "Traceback (most recent call last):",
              "[ears] device=1 running=True",
              "your key is nvapi-abc123"):
        ok(f"blocked: {d[:38]!r}", S.clean(d) == "")


def test_length_bounds():
    print("\n-- cohost lines stay short --")
    para = ("First sentence here. Second one follows. Third keeps going. "
            "Fourth is too many. Fifth as well.")
    ok("paragraph trimmed to <=2 sentences", S.clean(para).count(".") <= 2)
    ok("long line capped", len(S.clean("word " * 200)) <= 245)



def test_narrate_path_not_blocked():
    """Ambient narration passes heard='' -- must not be treated as private."""
    print(chr(10)+"-- empty heard (narrate) must not trip is_private --")

    ok("narrate vet passes", S.vet("", "Joel is crouching again.")[0])
    ok("private topics still blocked", not S.vet("my medication", "")[0])

def test_never_raises():
    print("\n-- gate must never crash the live loop --")
    for bad in (None, "", "   ", "\x00\x01", "a"):
        try:
            S.vet(bad, bad)
            S.clean(bad)
            S.is_private(bad)
            S.is_echo(bad, bad)
            ok(f"survives {bad!r}", True)
        except Exception as e:
            ok(f"survives {bad!r}", False, str(e)[:40])


if __name__ == "__main__":
    for fn in (test_live_leaks_blocked, test_private_topics,
               test_normal_speech_allowed, test_echo_detection,
               test_truncated_stubs, test_prompt_leak, test_scratchpad_stripped,
               test_diagnostics_blocked, test_length_bounds, test_narrate_path_not_blocked, test_never_raises):
        fn()
    print("\n" + ("ALL PASS" if not FAILS else f"{len(FAILS)} FAILED: {FAILS}"))
    sys.exit(1 if FAILS else 0)
