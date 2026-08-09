import numpy as np, wave, sys

def load(p):
    w = wave.open(p,'rb')
    sr = w.getframerate()
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)/32768.0
    w.close()
    return d, sr

def melspect(x, sr, nfft=1024, hop=256, nmel=40, fmin=80, fmax=8000):
    """proper mel-scaled log-magnitude spectrogram"""
    n = len(x)
    nframes = max(1, (n - nfft)//hop)
    S = np.zeros((nfft//2+1, nframes))
    win = np.hanning(nfft)
    for i in range(nframes):
        seg = x[i*hop:i*hop+nfft]
        if len(seg) < nfft: seg = np.pad(seg, (0, nfft-len(seg)))
        S[:, i] = np.abs(np.fft.rfft(seg*win))
    freqs = np.fft.rfftfreq(nfft, 1.0/sr)
    # mel filterbank (triangular)
    mel = np.zeros((nmel, len(freqs)))
    mel_min = 2595*np.log10(1+fmin/700); mel_max = 2595*np.log10(1+fmax/700)
    mel_pts = np.linspace(mel_min, mel_max, nmel+2)
    hz_pts = 700*(10**(mel_pts/2595)-1)
    for m in range(nmel):
        for k, f in enumerate(freqs):
            if f < hz_pts[m] or f > hz_pts[m+2]: continue
            if f < hz_pts[m+1]: mel[m,k] = (f-hz_pts[m])/(hz_pts[m+1]-hz_pts[m]+1e-9)
            else: mel[m,k] = (hz_pts[m+2]-f)/(hz_pts[m+2]-hz_pts[m+1]+1e-9)
    M = np.log10(mel @ S + 1e-6)
    return M

def spec_corr(a, sr_a, b, sr_b):
    # resample b to a's sr if needed (simple decimate/interp)
    if sr_b != sr_a:
        ratio = sr_a/sr_b
        idx = np.arange(0, len(b), 1/ratio).astype(int)
        idx = np.clip(idx, 0, len(b)-1)
        b = b[idx]
    n = min(len(a), len(b))
    Ma = melspect(a[:n], sr_a)
    Mb = melspect(b[:n], sr_a)
    nf = min(Ma.shape[1], Mb.shape[1])
    return np.corrcoef(Ma[:, :nf].ravel(), Mb[:, :nf].ravel())[0,1]

pairs = [
    ("Cartman x lead_8s",    "out/demo/ab_cart_lead_cpu.wav",  "out/demo/ab_cart_lead_vk.wav"),
    ("Cartman x bart_10s",   "out/demo/ab_cart_bart_cpu.wav",  "out/demo/ab_cart_bart_vk.wav"),
    ("Cleveland x lead_8s",  "out/demo/ab_cleve_lead_cpu.wav", "out/demo/ab_cleve_lead_vk.wav"),
    ("Cleveland x album10s", "out/demo/ab_cleve_alb_cpu.wav",  "out/demo/ab_cleve_alb_vk.wav"),
    ("Seth x lead_8s",       "out/demo/ab_seth_lead_cpu.wav",  "out/demo/ab_seth_lead_vk.wav"),
]
print(f"{'case':<24} {'len(s)':>7} {'mel-corr':>9} {'rms vk/cpu':>12} {'peak vk/cpu':>12}")
for name, cpu, vk in pairs:
    try:
        a, sr_a = load(cpu); b, sr_b = load(vk)
        c = spec_corr(a, sr_a, b, sr_b)
        print(f"{name:<24} {min(len(a),len(b))/sr_a:>7.1f} {c:>9.3f} "
              f"{np.sqrt((b**2).mean()):>5.3f}/{np.sqrt((a**2).mean()):>5.3f} "
              f"{np.abs(b).max():>5.3f}/{np.abs(a).max():>5.3f}")
    except Exception as e:
        print(name, "ERR", e)
