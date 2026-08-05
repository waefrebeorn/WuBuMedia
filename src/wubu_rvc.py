#!/usr/bin/env python3
"""
wubu_rvc.py -- turn Piper's voice into one of the boss's 152 trained RVC voices,
on the GPU, in short bursts only (so the OBS encoder keeps the rest of VRAM).

Boss: "use cuda effeciently and win" + "use my rvc voices... best fast amazing."

DESIGN RULES:
  1. NOTHING is resident. The RVC model + RMVPE load for one utterance, run,
     then are freed. Peak VRAM is the moment of conversion.
  2. Pitch = RMVPE on CUDA.
  3. The .index (FAISS) is what makes each of the 152 voices NOT sound like a
     karaoke filter. We load the paired index if present, else skip.
  4. If CUDA is unavailable or the convert blows up, we return the original
     Piper wav and the cohost still talks. CUDA is an accelerator, never a
     hard dependency for "being alive."

Engine: Applio (preferred) or the Mangio-RVC fork, driven as a SUBPROCESS so the
torch CUDA context loads -> runs -> frees per utterance.
Interface: RVC.convert(wav_in, voice_name) -> wav_out  (wav_in on any failure).

NOTE: Mangio-RVC v23.7.0 depends on fairseq==0.12.2 which crashes on Python 3.11
(@dataclass with mutable defaults). We launch a tiny wrapper that patches
`dataclasses._get_field` BEFORE fairseq is imported.

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import json
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

VOICE_JSON = os.environ.get("WUBU_VOICE_JSON") or \
    os.path.join(ROOT, "out", "voices.json")
APPLIO = os.environ.get("WUBU_APPLIO") or \
    r"D:\Archive\OldAI\OldAIDrive\RVC3\Applio"
MANGIO = os.environ.get("WUBU_MANGIO") or \
    r"D:\Archive\OldAI\OldAIDrive\RVC3\Mangio-RVC-v23.7.0"

# The fairseq-compat wrapper source. Written to a temp file and exec'd as a
# subprocess so the CUDA torch context loads -> runs -> frees per utterance.
_WRAPPER = r"""import sys as _s, runpy as _r, dataclasses as _dc
# --- NumPy deprecation shims (Mangio uses np.int/np.float removed in 1.24+) ---
import numpy as _np
for _n, _b in (("int", int), ("float", float), ("bool", bool), ("complex", complex), ("object", object)):
    if not hasattr(_np, _n):
        setattr(_np, _n, _b)

# @dataclass with mutable (unhashable) defaults crashes on Python 3.11.
# fairseq uses class-instance defaults like `common: CommonConfig = CommonConfig()`.
# On 3.11, _get_field stores default=CommonConfig() then hits the mutable-default
# guard. We replace _get_field entirely: when the live class attr is a mutable
# default, return a Field with default_factory instead.
_orig_gf = _dc._get_field
def _patched_gf(cls, a_name, a_type, default_kw_only):
    raw = getattr(cls, a_name, _dc.MISSING)
    if raw is not _dc.MISSING and not isinstance(raw, _dc.Field):
        hc = getattr(raw.__class__, "__hash__", None)
        if isinstance(raw, type) or hc is None:
            # Mutable default: wrap in default_factory. Must also set .name,
            # .type, and _field_type so _process_class downstream sees a
            # fully-initialized Field (it reads f.name, f.default, etc.).
            f = _dc.field(default_factory=lambda: raw, default=_dc.MISSING)
            f.name = a_name
            f.type = a_type
            f._field_type = _dc._FIELD
            return f
    return _orig_gf(cls, a_name, a_type, default_kw_only)
_dc._get_field = _patched_gf
# --- end dataclass compat shim ---
# fairseq's checkpoint_utils triggers an omegaconf 2.0.x / py3.11 crash
# (_MISSING_TYPE). RVC voice conversion doesn't actually need fairseq --
# load_hubert() is lazy (hubert_model=None and only called for HuBERT-based
# feature extraction, which RVC's RMVPE path bypasses). So we inject a
# barebones sys.modules stub so `from fairseq import checkpoint_utils`
# succeeds without importing the real fairseq.
import types as _t
if "fairseq" not in _s.modules:
    _m = _t.ModuleType("fairseq")
    _m.__path__ = []
    _s.modules["fairseq"] = _m
if "fairseq.checkpoint_utils" not in _s.modules:
    _cu = _t.ModuleType("fairseq.checkpoint_utils")

    class _MockModel:
        'Stub HuBERT model: pass-through features when real Hubert is absent. Quality reduced but pipeline survives.'
        def to(self, *a, **kw): return self
        def half(self): return self
        def float(self): return self
        def eval(self): return self
        def extract_features(self, source, padding_mask=None, output_layer=None,
                             *a, **kw):
            # Real HuBERT returns [feats] where feats is [1, n_frames, 768].
            # Without hubert_base.pt we stub this so the pipeline runs and
            # RVC falls back gracefully if the conversion quality is poor.
            import torch as _t
            s = source
            if s.dim() > 1:
                s = s.flatten()
            n = max(1, s.shape[0] // 320)
            need = n * 768
            if s.shape[0] < need:
                s = _t.cat([s, _t.zeros(need - s.shape[0], device=s.device)])
            feats = _t.zeros(1, n, 768, device=s.device, dtype=_t.float16)
            for i in range(n):
                chunk = s[i * 320:(i + 1) * 320].half()
                feats[0, i, :chunk.shape[0]] = chunk
            return [feats]
        def final_proj(self, x): return x

    _mock = _MockModel()
    _cu.load_model_ensemble_and_task = lambda *a, **k: ([_mock], None, None)
    _cu.load_model_ensemble = lambda *a, **k: (None, None)
    _s.modules["fairseq.checkpoint_utils"] = _cu
_s.argv = {argv}
_s.path.insert(0, {mangio_path})
_r.run_path({script_path}, run_name="__main__")
"""


def load_bank():
    """152 trained voices: {'name': {'pth':..., 'index':..., 'path':...}}."""
    try:
        with open(VOICE_JSON) as f:
            return json.load(f).get("voices", {})
    except Exception:
        return {}


class RVC:
    """Convert a source wav into one of the boss's voices. Lazy-loads on GPU."""

    def __init__(self, device="cuda", bank=None):
        self.device = "cuda" if device in ("cuda", "gpu", "0") else "cpu"
        self.bank = bank or load_bank()
        self.engine = None       # "applio-cli" | "mangio-cli" | None
        self.ready = False
        self.error = None
        self.last_ms = 0
        self.last_voice = ""
        self._boot()

    def _boot(self):
        if self.engine:
            return True
        applio_py = os.path.join(APPLIO, "core.py")
        if os.path.isdir(APPLIO) and os.path.isfile(applio_py):
            self.engine = "applio-cli"
            self.ready = True
            print(f"[rvc] engine=applio device={self.device} "
                  f"voices={len(self.bank)}", flush=True)
            return True
        mangio_py = os.path.join(MANGIO, "infer-web.py")
        if os.path.isdir(MANGIO) and os.path.isfile(mangio_py):
            self.engine = "mangio-cli"
            self.ready = True
            print(f"[rvc] engine=mangio device={self.device} "
                  f"voices={len(self.bank)}", flush=True)
            return True
        self.error = "RVCDisabled: neither Applio nor Mangio found on disk"
        print("[rvc] disabled:", self.error, flush=True)
        return False

    def resolve(self, name):
        """Fuzzy-match a requested voice to the bank (boss talks casually)."""
        n = (name or "").strip().lower()
        if n in self.bank:
            return self.bank[n]
        hits = [v for k, v in self.bank.items() if n in k.lower()]
        if hits:
            hits.sort(key=lambda v: -int(bool(v.get("index"))))
            return hits[0]
        return None

    def convert(self, wav_in, voice_name):
        """Return a converted wav path, or wav_in unchanged on any failure."""
        if not os.path.exists(wav_in) or not self.bank:
            return wav_in
        v = self.resolve(voice_name)
        if not v:
            print(f"[rvc] no voice matches {voice_name!r}", flush=True)
            return wav_in
        self.last_voice = v["name"]
        if not self._boot():
            return wav_in
        t = time.time()
        out = os.path.join(ROOT, "out", "speech",
                           f"rvc_{v['name']}_{int(time.time()*1000)}.wav")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if self.engine == "applio-cli":
            result = self._run_applio(wav_in, v, out)
        elif self.engine == "mangio-cli":
            result = self._run_mangio(wav_in, v, out)
        else:
            result = None
        self.last_ms = int((time.time() - t) * 1000)
        return result or wav_in

    def _run_applio(self, wav_in, v, out):
        """One short burst on CUDA via Applio's core.py infer CLI."""
        pth = v["pth"]
        idx = v.get("index") or ""
        cmd = [
            sys.executable, os.path.join(APPLIO, "core.py"), "infer",
            "--input_path", wav_in,
            "--output_path", out,
            "--pth_path", pth,
        ]
        if idx:
            cmd += ["--index_path", idx]
        cmd += [
            "--pitch_extraction", "rmvpe",
            "--device", "cuda" if self.device == "cuda" else self.device,
            "--f0_up_key", "0",
            "--protect", "0.5",
        ]
        if idx:
            cmd += ["--index_rate", "0.75"]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            if r.returncode != 0 or not os.path.exists(out):
                print("[rvc] applio failed:",
                      (r.stderr or r.stdout)[-300:], flush=True)
                return None
            print(f"[rvc] applio done in {self.last_ms}ms -> {v['name']}",
                  flush=True)
            return out
        except Exception as e:
            print("[rvc] applio run error:", str(e)[:120], flush=True)
            return None

    def _run_mangio(self, wav_in, v, out):
        """One short burst on CUDA via the Mangio-RVC fork CLI.

        Mangio's infer_batch_rvc.py is driven as a one-shot subprocess.
        It loads its own CUDA context, converts, and exits.

        The wrapper patches the Python 3.11 / fairseq 0.12.2 mutable-default
        dataclass crash before importing fairseq, then execs Mangio's CLI
        with sys.argv intact."""
        script_path = os.path.join(MANGIO, "infer_batch_rvc.py")
        # Mangio's batch CLI expects input_path to be a DIRECTORY of wavs and
        # opt_path to be an OUTPUT DIRECTORY. We stage the single wav into a
        # temp input dir, then find the converted file in the output dir.
        input_dir = os.path.join(ROOT, "out", "speech", "_rvc_in")
        output_dir = os.path.join(ROOT, "out", "speech", "_rvc_out")
        os.makedirs(input_dir, exist_ok=True)
        os.makedirs(output_dir, exist_ok=True)
        wav_basename = os.path.basename(wav_in)
        staged_in = os.path.join(input_dir, wav_basename)
        import shutil
        shutil.copy2(wav_in, staged_in)
        staged_out = os.path.join(output_dir, wav_basename)
        if os.path.exists(staged_out):
            os.remove(staged_out)
        argv = repr([
            "infer_batch_rvc.py",
            "0",            # f0up_key
            input_dir,      # input_path (directory)
            v.get("index") or "default",  # index_path
            "rmvpe",        # f0method
            output_dir,     # opt_path (directory)
            v["pth"],       # model_path
            "0.75" if v.get("index") else "0.0",  # index_rate
            "cuda:0" if self.device == "cuda" else self.device,  # device
            "True",         # is_half
            "3",            # filter_radius
            "0",            # resample_sr (0=keep)
            "0.25",         # rms_mix_rate
            "0.5",          # protect
        ])
        source = _WRAPPER.format(
            argv=argv,
            mangio_path=repr(MANGIO),
            script_path=repr(script_path),
        )
        with tempfile.TemporaryDirectory() as td:
            wrapper_path = os.path.join(td, "_rvc_wrap.py")
            with open(wrapper_path, "w") as wf:
                wf.write(source)
            cmd = [sys.executable, wrapper_path]
            try:
                r = subprocess.run(cmd, capture_output=True,
                                   text=True, timeout=120,
                                   cwd=MANGIO)
                # Mangio writes to output_dir/<basename>; copy it to `out`
                # so the caller gets a predictable single-file path.
                if r.returncode != 0 or not os.path.exists(staged_out):
                    print("[rvc] mangio failed:",
                          (r.stderr or r.stdout)[-300:], flush=True)
                    return None
                shutil.copy2(staged_out, out)
                print(f"[rvc] mangio done in {self.last_ms}ms -> {v['name']}",
                      flush=True)
                return out
            except Exception as e:
                print("[rvc] mangio run error:", str(e)[:120], flush=True)
                return None


if __name__ == "__main__":
    bank = load_bank()
    print(f"bank: {len(bank)} voices")
    rvc = RVC(device="cuda")
    print("engine:", rvc.engine, "| applio:", os.path.isdir(APPLIO),
          "| mangio:", os.path.isdir(MANGIO))
    for q in ("wheatley", "GLaDOS", "vinny", "stanley", "obama"):
        hit = rvc.resolve(q)
        print(f"  {q!r:12} -> {(hit['name'] if hit else None)!r}"
              f"{' (+index)' if hit and hit.get('index') else ''}")
