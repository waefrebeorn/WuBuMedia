#!/bin/bash
# A/B battery: CPU vs Vulkan on multiple models x examples
cd /c/Users/eman5/WuBuMedia
EXE=build/wubu_rvc_fast.exe

# prepare a 10s slice from the album original
.venv_win/Scripts/python.exe -c "
import wave, struct
w = wave.open('out/demo/album_orig_45s.wav','rb')
sr = w.getframerate(); n = w.getnframes()
w.setpos(sr*8)  # start at 8s
d = w.readframes(sr*10)
out = wave.open('out/demo/album10s.wav','wb')
out.setnchannels(1); out.setsampwidth(2); out.setframerate(sr)
out.writeframes(d); out.close()
print('album10s ready', sr)
" 2>/dev/null

run() {
    local name=$1 model=$2 pth=$3 input=$4 out_cpu=$5 out_vk=$6 noise=$7
    echo "=== $name ==="
    # CPU
    local t0=$(date +%s.%N)
    $EXE "$input" "$model" "$out_cpu" --model "$pth" --noise "$noise" --jobs 4 2>/dev/null | grep -oE 'chunked synth: [0-9.]+ s \([0-9.]+x' | head -1
    local t1=$(date +%s.%N)
    local cpu_t=$(echo "$t1 $t0" | awk '{printf "%.1f", $1-$2}')
    # Vulkan
    local t2=$(date +%s.%N)
    $EXE "$input" "$model" "$out_vk" --model "$pth" --noise "$noise" --vk --jobs 2 2>/dev/null | grep -oE 'chunked synth: [0-9.]+ s \([0-9.]+x' | head -1
    local t3=$(date +%s.%N)
    local vk_t=$(echo "$t3 $t2" | awk '{printf "%.1f", $1-$2}')
    echo "  wall: cpu=${cpu_t}s vk=${vk_t}s"
    .venv_win/Scripts/python.exe -c "
import numpy as np, wave, sys
def load(p):
    w = wave.open(p,'rb'); d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)/32768.0; w.close(); return d
a = load('$out_vk'); b = load('$out_cpu')
n = min(len(a), len(b))
def melcorr(x, y, nfft=1024, hop=256, nmel=32):
    # simple log-magnitude spectrogram correlation (robust to phase/amplification)
    X = np.abs(np.lib.stride_tricks.sliding_window_view(x, nfft)[::hop])
    Y = np.abs(np.lib.stride_tricks.sliding_window_view(y, nfft)[::hop])
    X = np.log10(X + 1e-6); Y = np.log10(Y + 1e-6)
    # average over bins -> rough mel-ish
    Xm = X.reshape(X.shape[0], -1, min(nmel, X.shape[1]))[:, :, 0].T if X.shape[1] >= nmel else X.T
    Ym = Y.reshape(Y.shape[0], -1, min(nmel, Y.shape[1]))[:, :, 0].T if Y.shape[1] >= nmel else Y.T
    return np.corrcoef(Xm.ravel(), Ym.ravel())[0,1]
print('  rms: vk=%.3f cpu=%.3f | peak: vk=%.3f cpu=%.3f | corr: %.3f | spec: %.3f' % (
    np.sqrt((a**2).mean()), np.sqrt((b**2).mean()), np.abs(a).max(), np.abs(b).max(),
    np.corrcoef(a[:n], b[:n])[0,1] if np.std(a[:n])>0 and np.std(b[:n])>0 else 0.0, melcorr(a[:n], b[:n])))
" 2>/dev/null
}

run "Cartman x lead_8s"      models/rvc/cartman    models/rvc/cartman/EricCartmanV1_e650_s10400.pth out/demo/lead_8s.wav      out/demo/ab_cart_lead_cpu.wav out/demo/ab_cart_lead_vk.wav 0.33333
run "Cartman x bart_10s"     models/rvc/cartman    models/rvc/cartman/EricCartmanV1_e650_s10400.pth out/demo/bart_10s_ak2.wav  out/demo/ab_cart_bart_cpu.wav  out/demo/ab_cart_bart_vk.wav  0.33333
run "Cleveland x lead_8s"    models/rvc/cleveland  models/rvc/cleveland/Cleveland_Brown_220e_7920s.pth out/demo/lead_8s.wav   out/demo/ab_cleve_lead_cpu.wav out/demo/ab_cleve_lead_vk.wav 0.33333
run "Cleveland x album10s"   models/rvc/cleveland  models/rvc/cleveland/Cleveland_Brown_220e_7920s.pth out/demo/album10s.wav     out/demo/ab_cleve_alb_cpu.wav  out/demo/ab_cleve_alb_vk.wav  0.33333
run "Seth x lead_8s"         models/rvc/seth       models/rvc/seth/sethmacfarlene.pth out/demo/lead_8s.wav          out/demo/ab_seth_lead_cpu.wav  out/demo/ab_seth_lead_vk.wav  0.33333
echo "=== done ==="
