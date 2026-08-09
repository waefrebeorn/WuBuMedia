import numpy as np, wave

def load(p):
    w = wave.open(p,'rb')
    sr = w.getframerate()
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)/32768.0
    w.close()
    return d, sr

def melspect(x, sr, nfft=1024, hop=256, nmel=40, fmin=80, fmax=8000):
    n = len(x)
    nframes = max(1, (n - nfft)//hop)
    S = np.zeros((nfft//2+1, nframes))
    win = np.hanning(nfft)
    for i in range(nframes):
        seg = x[i*hop:i*hop+nfft]
        if len(seg) < nfft: seg = np.pad(seg, (0, nfft-len(seg)))
        S[:, i] = np.abs(np.fft.rfft(seg*win))
    freqs = np.fft.rfftfreq(nfft, 1.0/sr)
    mel = np.zeros((nmel, len(freqs)))
    mel_min = 2595*np.log10(1+fmin/700); mel_max = 2595*np.log10(1+fmax/700)
    mel_pts = np.linspace(mel_min, mel_max, nmel+2)
    hz_pts = 700*(10**(mel_pts/2595)-1)
    for m in range(nmel):
        for k, f in enumerate(freqs):
            if f < hz_pts[m] or f > hz_pts[m+2]: continue
            if f < hz_pts[m+1]: mel[m,k] = (f-hz_pts[m])/(hz_pts[m+1]-hz_pts[m]+1e-9)
            else: mel[m,k] = (hz_pts[m+2]-f)/(hz_pts[m+2]-hz_pts[m+1]+1e-9)
    return np.log10(mel @ S + 1e-6)

def corr_at_sr(a, sr_a, b, sr_b):
    if sr_b != sr_a:
        ratio = sr_a/sr_b
        idx = np.clip(np.arange(0, len(b), 1/ratio).astype(int), 0, len(b)-1)
        b = b[idx]
    n = min(len(a), len(b))
    Ma = melspect(a[:n], sr_a); Mb = melspect(b[:n], sr_a)
    nf = min(Ma.shape[1], Mb.shape[1])
    return np.corrcoef(Ma[:, :nf].ravel(), Mb[:, :nf].ravel())[0,1]

cases = [
    ("Cartman x lead_8s",   "out/demo/lead_8s.wav",      "out/demo/ab_cart_lead_cpu.wav",  "out/demo/ab_cart_lead_vk.wav"),
    ("Cartman x bart_10s",  "out/demo/bart_10s_ak2.wav", "out/demo/ab_cart_bart_cpu.wav",  "out/demo/ab_cart_bart_vk.wav"),
    ("Cleveland x lead_8s", "out/demo/lead_8s.wav",      "out/demo/ab_cleve_lead_cpu.wav", "out/demo/ab_cleve_lead_vk.wav"),
    ("Cleveland x album10s","out/demo/album10s.wav",     "out/demo/ab_cleve_alb_cpu.wav",  "out/demo/ab_cleve_alb_vk.wav"),
    ("Seth x lead_8s",      "out/demo/lead_8s.wav",      "out/demo/ab_seth_lead_cpu.wav",  "out/demo/ab_seth_lead_vk.wav"),
]
print(f"{'case':<24} {'src->cpu':>9} {'src->vk':>9} {'cpu->vk':>9}")
for name, src, cpu, vk in cases:
    s, sr_s = load(src); c, sr_c = load(cpu); v, sr_v = load(vk)
    sc = corr_at_sr(s, sr_s, c, sr_c)
    sv = corr_at_sr(s, sr_s, v, sr_v)
    cv = corr_at_sr(c, sr_c, v, sr_v)
    print(f"{name:<24} {sc:>9.3f} {sv:>9.3f} {cv:>9.3f}")
