import numpy as np, wave, sys

def load(p):
    w = wave.open(p,'rb')
    sr = w.getframerate()
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)/32768.0
    w.close()
    return d, sr

def mel_spec_corr(a, b, sr=40000, n_fft=1024, hop=256, n_mels=64):
    """simple band-energy spectral correlation (content similarity)"""
    def bands(x):
        S = np.abs(np.fft.rfft(x[:len(x)//hop*hop].reshape(-1, hop), n=n_fft, axis=1))
        # mel-ish bin grouping
        n = S.shape[1]
        idx = np.linspace(0, n-1, n_mels+1).astype(int)
        out = np.zeros((S.shape[0], n_mels))
        for m in range(n_mels):
            out[:,m] = S[:, idx[m]:idx[m+1]].sum(axis=1) if idx[m+1]>idx[m] else S[:, idx[m]]
        return out
    ba, bb = bands(a), bands(b)
    return np.corrcoef(ba.ravel(), bb.ravel())[0,1]

a, _ = load(sys.argv[1])
b, _ = load(sys.argv[2])
n = min(len(a), len(b))
rms_a = np.sqrt((a[:n]**2).mean()); rms_b = np.sqrt((b[:n]**2).mean())
peak_a = np.abs(a[:n]).max(); peak_b = np.abs(b[:n]).max()
corr = mel_spec_corr(a[:n], b[:n])
print(f"rms {rms_a:.4f} vs {rms_b:.4f} | peak {peak_a:.4f} vs {peak_b:.4f} | mel-corr {corr:.4f}")
