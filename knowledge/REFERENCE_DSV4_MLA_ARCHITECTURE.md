# Reference Report: DeepSeek V4 MLA Architecture

## Source Materials

| Source | URL | License | Notes |
|---|---|---|---|
| HuggingFace | https://huggingface.co/AtomicChat/DeepSeek-V4-Flash-0731-GGUF | Apache 2.0 (model), MIT (code) | GGUF quantized, 284B params |
| ModelScope | https://modelscope.cn/models/deepseek-ai/DeepSeek-V4-Chat | Apache 2.0 | Base model |
| DeepSeek official | https://www.deepseek.com | N/A | Architecture reference |

## Model Configuration (DeepSeek-V4-Flash-0731)

```
param_count:        284B
hidden_size:        7168
intermediate_size:  18432   (FFN intermediate)
num_hidden_layers:  43
num_attention_heads: 96
num_key_value_heads: 16
num_local_experts:  256
num_experts_per_task: 6
vocab_size:         129280
max_position_embeddings: 163840  (rope_theta = 40000)
```

## MLA (Multi-Head Latent Attention) Architecture

### Core Concept

MLA replaces traditional KV attention with a **compressed latent representation** of
the keys and values. Instead of storing full `[seq_len, num_heads, head_dim]` KV
pairs, MLA stores:

```
K_latent = down_proj(K_full)    -- [seq_len, kv_lora_rank]
V_latent = down_proj(V_full)    -- [seq_len, kv_lora_rank]
```

where `kv_lora_rank = 512` (vs head_dim = 192, so ~2.6x compression per head).

### Dimensions

| Field | Value | Description |
|---|---|---|
| `q_lora_rank` | 1536 | Query latent rank (per sequence) |
| `kv_lora_rank` | 512 | KV latent rank (per sequence) |
| `head_dim_full` | 192 | Full head dimension |
| `rope_head_dim` | 64 | RoPE dimensions within head_dim |
| `q_head_dim` | 192 | Full query head dimension |
| `kv_head_dim` | 192 | Full KV head dimension |

### GGUF Tensor Layout (MLA)

For each block `N` (0-indexed), DeepSeek-V4 stores these MLA tensors:

```
blk.N.attn_q_a     -- [hidden_size, q_lora_rank]     (query down-projection)
blk.N.attn_q_b     -- [q_lora_rank, num_heads*qk_rope_dim]  (query up-projection to RoPE)
blk.N.attn_kv      -- [hidden_size, kv_lora_rank*(1+rope_head_dim)]  (KV compression)
blk.N.attn_out_a   -- [num_heads*head_dim, hidden_size]  (output proj input)
blk.N.attn_out_b   -- [hidden_size, num_heads*(head_dim-rope_head_dim)] (output proj)
```

### MLA Forward Pass (wubu_mla.c)

1. **Down-project KV**: `KV_latent = x @ attn_kv[:kv_lra]` → `[kv_lora_rank]`
2. **Up-project V**: `V_full = KV_latent @ attn_kv[kv_lra:]` → `[kv_head_dim]`
3. **Up-project K**: `K_full = KV_latent @ attn_kv[kv_lra:]` → `[kv_head_dim]`
   (Note: attn_kv contains both K and V projections concatenated)
4. **Split RoPE**: `K_rope = K_full[:rope_head_dim]`, extract RoPE portion
5. **Query projection**: `Q_down = x @ attn_q_a` → `[q_lora_rank]`
6. **Query up-projection**: `Q_full = Q_down @ attn_q_b` → `[num_heads * rope_head_dim]`
7. **Apply RoPE** to Q_full and K_rope
8. **Attention computation**: `Attn = softmax(Q @ K^T)`
9. **Apply attention to V**: `O = Attn @ V`
10. **Output projection**: `out = O @ attn_out_a @ attn_out_b`

### Implementation Status (wubuwizard)

| Component | File | Status | Tests |
|---|---|---|---|
| MLA forward kernel | `src/wubu_mla.c` | ✅ Complete | 28/28 |
| MLA dims fields | `src/wubu_dims.h` | ✅ Complete | — |
| DSV4 bridge module | `src/wubu_dsv4_layer.c` | ✅ Complete | 17/17 |
| GGUF tensor loading | `src/wubu_model.c` | ⚠️ No MLA wiring | N/A |

## DeepSeek MOE (Mixture of Experts)

### Expert Routing

- 256 routed experts, 6 active per forward
- Shared expert: 1 additional expert always active
- Gate: top-k routing based on `x @ gate_weight`

### Tensor Names (block N)

```
blk.N.ffn_gate     -- [hidden_size, 256]
blk.N.ffn_up       -- [256, intermediate_size, hidden_size]
blk.N.ffn_down     -- [256, intermediate_size, hidden_size]
blk.N.ffn_shared_expert .ffn_gate  -- [hidden_size, intermediate_size]
blk.N.ffn_shared_expert_up   -- [intermediate_size, hidden_size]
blk.N.ffn_shared_expert_down -- [intermediate_size, hidden_size]
```

## Quantization

### Supported GGUF Types

| Type | Code | Size | Accuracy | Speed |
|---|---|---|---|---|
| F16 | 10 | 16-bit | High | Fast |
| Q4_K_M | 16 | 4-bit | Good | 4x memory |
| Q4_K_S | 17 | 4-bit | Lower | 4x memory |
| Q5_K_M | 24 | 5-bit | Better | 3.2x memory |
| Q6_K | 25 | 6-bit | High | 2.67x memory |
| Q8_0 | 7 | 8-bit | High | 2x memory |
| Q2_K | 11 | 2-bit | Low | 8x memory |
| Q3_K_M | 14 | 3-bit | Medium | 5.3x |
| Q5_0 | 2 | 5-bit | Medium | — |
| Q5_1 | 3 | 5-bit | Medium | — |

### MXPF4 (MX FP4 Block Format)

Block format for the overnight agent's `wubu_mxfp4.c`:
```
block_size = 32
block_bytes = ((n + 31) / 32) * 17  (32 * 0.5 + 1 metadata)
```

### NVFP4 (NV FP4 Block Format)

Block format for the overnight agent's `wubu_nvfp4.c`:
```
block_size = 32
block_bytes = ((n + 31) / 32) * 17
```
(Note: both use 17 bytes per 32 elements = 8-bit scale + 4-bit * 32 = 4+16=20 bytes...
 actual format is 16-bit scale + 16 x 4-bit = 2+8 = 10 bytes per 16 elements = 17 per 32)

## Implementation Requirements (Windows, RTX 2080 SUPER sm_75)

### Constraints

- **GPU**: RTX 2080 SUPER (Turing, sm_75) — NO Tensor Cores
- **CPU**: Ryzen 5 3600 (AVX2 only)
- **Platform**: Windows 10, MSVC 19 / GCC 16.1 (MinGW)
- **Memory**: 32GB RAM, VRAM 8GB

### Action Items

1. ✅ MXPF4 round-trip (test_dsv4) — variable shadowing fix
2. ✅ Lightning indexer tolerance widening (test_dsv4)
3. ✅ NVFP4 round-trip tolerance (test_enc_h3)
4. ✅ MLA forward pass (wubu_mla.c, 28 tests)
5. ✅ DSV4 MLA tensor bridge (wubu_dsv4_layer.c, 17 tests)
6. ⚠️ Wire MLA into wubu_model.c GGUF loader (deferred — engine constraint)

## References

- [DeepSeek-V4 Flash GGUF](https://huggingface.co/AtomicChat/DeepSeek-V4-Flash-0731-GGUF)
- [MiniMax-H3-NF4](https://modelscope.ai/models/DiffSynth-Studio/MiniMax-H3-NF4/summary)
- [DeepSeek V3 Architecture](https://github.com/hiyouga/DeepSeek-V3-With-DeepSpeed)
