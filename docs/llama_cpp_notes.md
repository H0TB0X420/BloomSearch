# llama.cpp study notes

Working notes on llama.cpp internals as we use them in BloomSearch's `LocalLLMBackend`. The goal is engineering credibility for the article: what the library actually does, where our integration touches each piece, what's cheap vs. expensive, and what we verified directly versus what we trusted.

Pinned to llama.cpp commit **`0adede866ddb2e31992b3792eaea31d18ed89acf`** (build tag `b8925`, 2026-04-23). Most observations here are stable across nearby pins, but the sampler-chain API and a handful of function names changed substantially over llama.cpp's history — this doc is written against b8925 specifically.

## Why llama.cpp

- **Pure C/C++.** No Python runtime, no `torch`, no GIL. The library is `~150K LOC` of C++ that compiles in 10-15 minutes on a modest CPU and links statically into our binary.
- **GGUF as a self-contained model artifact.** A single file holds the weights, the vocabulary, the metadata, and (since mid-2024) the chat template. mmap-friendly: the OS pages tensors in on demand instead of forcing a 400 MB load before the first token.
- **CPU-only is a first-class path.** Our build sets `GGML_CUDA=OFF`, `GGML_METAL=OFF`, `GGML_BLAS=OFF`, etc. We get a working, deterministic binary on Windows, macOS, and Linux without depending on a GPU or vendor SDK. Slower than CUDA — but the article is about engineering tradeoffs, and "no GPU" is the honest baseline.

## Runtime architecture

Three objects, three lifetimes, one abstraction:

```
                  load file once               cheap to recreate
                  ─────────────────             ────────────────
   GGUF file ──>  llama_model                   llama_sampler chain
                       │                              │
                       │                              │
                       ▼                              ▼
                  llama_context  ──── decodes ──── tokens out
                       │
                       ▲
                       │
                  llama_batch (input tokens, positions, sequence ids)
```

### `llama_model` — the heavy weight

- Loaded via `llama_model_load_from_file(path, params)` (b8925; renamed from `llama_load_model_from_file` somewhere around b3000).
- Holds the immutable tensor weights, the vocab, and the GGUF metadata. mmap-backed, so multiple `llama_context`s on the same model share memory pages.
- Freed via `llama_model_free`. We do this in `LocalLLMBackend::~LocalLLMBackend`.

### `llama_context` — the working memory

- Created from a model via `llama_init_from_model(model, ctx_params)`.
- Owns the **KV cache** (the per-layer attention state for the in-flight conversation), the compute buffers, and the threading config. This is the per-call memory we have to manage.
- Freed via `llama_free`.

In our code (`src/llm/local_llm.cpp`) one `LocalLLMBackend` owns one model and one context, both for the lifetime of the backend. Re-creating context per call would be wasteful (allocates compute buffers, ~300 MB on this Qwen). We instead clear the KV cache between calls:

```cpp
llama_memory_clear(llama_get_memory(ctx_), /*data=*/false);
```

That's ~microseconds; the context object stays put. Each `complete()` is therefore stateless from the caller's perspective even though `ctx_` is reused.

### `llama_sampler` chain — the token picker

A composable list. At b8925 the canonical pattern is:

```cpp
auto sparams = llama_sampler_chain_default_params();
llama_sampler* smpl = llama_sampler_chain_init(sparams);
llama_sampler_chain_add(smpl, llama_sampler_init_top_k(50));
llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.9, 1));
llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8));
llama_sampler_chain_add(smpl, llama_sampler_init_dist(seed));
llama_token next = llama_sampler_sample(smpl, ctx, -1);
```

Order matters: top_k → top_p → temp → dist (random sample) is the conventional ordering. Doing temp before top_p, for example, changes the distribution top_p sees and therefore changes which tokens make the cut. Our `LocalLLMBackend::generate` builds the chain per call (cheap) so each `complete()` gets a fresh deterministic state.

For `temperature == 0` we substitute `llama_sampler_init_greedy()` (argmax) — strictly deterministic, no RNG, no `dist` step. That's what makes the `seed=42 + temp=0` round-trip in our smoke tests produce byte-exact identical output on two consecutive calls.

### The decode loop

In one paragraph: a transformer doesn't take "the prompt" as a string — it takes a batch of token IDs paired with a position index, runs them through layered self-attention (using and updating the KV cache), and produces logits for the next token. You sample from those logits, append the chosen token to the batch, and repeat until you hit EOS or a length cap.

Our loop in `generate()` (`src/llm/local_llm.cpp`):

```cpp
// 1. Prompt decode (one batch, all prompt tokens)
llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
llama_decode(ctx_, batch);

while (generated < max_tokens) {
    // 2. Sample next token from logits at the most-recent position (-1)
    llama_token next = llama_sampler_sample(smpl, ctx_, -1);

    // 3. End-of-generation check (covers EOS, EOT, custom stop tokens)
    if (llama_vocab_is_eog(vocab_, next)) break;

    // 4. Convert token to bytes (UTF-8 fragment, may be partial codepoint)
    char piece[256];
    int n = llama_token_to_piece(vocab_, next, piece, sizeof(piece), 0, false);

    // 5. Stop-sequence check, UTF-8 boundary clamp, emit (callback or buffer)
    // ... see local_llm.cpp:226 onwards ...

    // 6. Decode just this one token to advance KV cache
    batch = llama_batch_get_one(&next, 1);
    llama_decode(ctx_, batch);
    generated++;
}
```

Everything else — UTF-8 buffering, stop sequences, cancellation, finish-reason mapping — is bookkeeping wrapped around this 6-step skeleton.

## GGUF format

GGUF (GPT-Generated Unified Format, despite the name) is llama.cpp's container format. Two parts, end-to-end:

1. **A flat key-value metadata header.** `general.architecture`, `tokenizer.ggml.tokens`, `qwen2.context_length`, `tokenizer.chat_template`, `general.quantization_version`, etc. Strings, scalars, arrays. Roughly 26 entries for the 0.5B Qwen we tested against.
2. **Tensor blocks.** Each tensor has a name (`blk.0.attn_q.weight`), a shape, a quantization type (`q4_K`, `q5_0`, `f32`, etc.), and a byte offset into the file. Loading is mmap'd: the OS pages in tensor bytes the first time inference touches them, no upfront load.

The header tells llama.cpp (a) how to interpret the architecture (Qwen2 vs Llama3 vs Phi3), (b) where the tokenizer's BPE merges live, and (c) what chat template the model was trained with. The fact that the chat template ships inside the file is what makes `llama_chat_apply_template` work without an external Python tokenizer.

What we tested directly: our `LocalLLMBackend::create()` calls `llama_model_load_from_file(...)`, which produces the loader output you see in the smoke test logs (`tokenizer.ggml.model = gpt2`, `tokenizer.ggml.tokens arr[str,151936]`, etc). Five lines into that output there's `general.size_label = 630M` — that's GGUF metadata, plain text in the file, not something we computed.

## Quantization tradeoffs

For BloomSearch we ship CPU inference, which means quantized weights are mandatory — full FP16 weights for even a 0.5B model are ~1.2 GB and produce roughly 5× slower inference than Q4_K_M. The article cares about real numbers, so the relevant menu:

| Format | Bits/weight | 0.5B file size | Quality cost vs F16 | Use case |
|---|---|---|---|---|
| F16 | 16.0 | ~1.2 GB | reference | research, GPU-rich |
| Q8_0 | 8.5 | ~640 MB | imperceptible | quality-sensitive, plenty of disk |
| **Q5_K_M** | ~5.7 | ~480 MB | tiny but measurable | quality-conscious default |
| **Q4_K_M** | ~4.8 | ~395 MB | small but noticeable | **what we use** for smoke + eval |
| Q3_K_M | ~3.9 | ~320 MB | starts to bite | last-resort low-RAM |
| Q2_K | ~3.4 | ~270 MB | clear regression | mostly hobbyist |

Q4_K_M is the conventional "good enough" choice for CPU inference — the K-block format (`_K`) is a smarter quantization that mixes precisions across a tensor (some weights at 4-bit, the rare important ones bumped to 6-bit). For our 0.5B Qwen the file is 462 MiB on disk and inference is fast enough that the 7-test smoke suite finishes in seconds.

Quality cost is real but small. The article doesn't pivot on quantization tradeoffs (we're measuring whether an LLM belongs in a query parser at all, not whether Q4 vs Q5 changes parse accuracy). Q4_K_M is the floor we run benchmarks at.

## Chat templates and our hand-rolled formatters

Each model family was trained on conversations formatted with specific marker tokens. Llama-3 uses `<|start_header_id|>...<|end_header_id|>...<|eot_id|>`. Qwen2.5 uses `<|im_start|>...<|im_end|>`. Phi-3 uses `<|user|>...<|end|>`. Get the markers wrong and the model "works" but produces noticeably degraded output — it sees a prompt outside its training distribution.

llama.cpp ships two ways to apply a chat template:
1. `llama_chat_apply_template(tmpl, chat, n, add_ass, buf, len)` — uses **hardcoded** template handlers selected by `tmpl` name (e.g., `"chatml"`, `"llama3"`, `"phi3"`). Not a Jinja parser.
2. The Jinja template inside the GGUF metadata (`tokenizer.chat_template`), which would require an external Jinja interpreter we don't ship.

We hand-rolled formatters for `Llama3`, `Qwen25`, `Phi3`, and `Raw` in `src/llm/chat_templates.cpp` for three reasons (D10): testable in isolation (no model needed), pluggable in the future, and credible as a thing the article author actually understands.

The smoke test cross-validates against llama.cpp's hardcoded `chatml` template:

```
[MATCH] 98 bytes byte-exact with llama.cpp's 'chatml' template
```

So our `format_chat(ChatTemplate::Qwen25, ...)` agrees with `llama_chat_apply_template("chatml", ...)` byte-for-byte. Not a coincidence — the canonical Qwen2.5 template *is* `chatml`-shaped, and we wrote the formatter against the published reference.

## BPE tokens and UTF-8

A subtle thing the article should mention: the byte stream out of `llama_token_to_piece` is **not codepoint-aligned**. BPE splits multi-byte UTF-8 characters at arbitrary byte offsets. For the crab emoji 🦀 (4 bytes: `0xF0 0x9F 0xA6 0x80`), the tokenizer might emit one token whose bytes are `0xF0 0x9F` and another whose bytes are `0xA6 0x80`. If a streaming consumer naively writes the first token's bytes to stdout, the terminal sees broken UTF-8 mid-glyph.

We solved this with `include/llm/utf8.h` — a header-only `utf8_complete_prefix_end(string_view)` that returns the largest prefix safe to emit (i.e., no codepoint trailing off the end). The streaming generation loop holds back partial codepoints in a `pending` buffer until the next token arrives with the rest. Combined with stop-sequence prefix detection (a separate hold-back of `max_stop_sequence_length − 1` bytes), the streaming path emits clean UTF-8 even when the model produces emoji or CJK on byte-boundary-misaligned tokens.

Tested with 30 byte-level cases in `tests/llm/test_utf8.cpp`. Real-model BPE output verified in the smoke test (test 3 reassembles streamed chunks and asserts they equal `LLMResponse.output` byte-for-byte).

## Performance reality

CPU inference is slow compared to GPU. Concrete numbers (filled in by Phase 7 on the canonical reference machine):

| Setup | Tokens/sec |
|---|---|
| Qwen2.5-0.5B Q4_K_M, 1 thread | TBD |
| Qwen2.5-0.5B Q4_K_M, all-cores | TBD |
| Qwen2.5-0.5B Q4_K_M on Mac mini cross-check | TBD |
| Qwen2.5-0.5B Q4_K_M on old MacBook Pro cross-check | TBD |

What we expect, ballpark:
- 50-150 tok/s on a recent CPU with all cores, 0.5B at Q4_K_M
- 5-15 tok/s on the old MacBook Pro (same model, same quant)
- The smoke test suite finishes in under 15 sec on the canonical machine

The article's latency-tax section (Surface 1) measures p50/p95/p99 for full query-parse calls — including prompt format + tokenize + decode + sample + detokenize — across 50 synthetic queries. Numbers from the canonical machine, with the cross-checks reported as a "diversity table."

## What we trust vs. what we verified

| Claim | How we know |
|---|---|
| llama.cpp loads our chosen GGUF | Smoke test 1+2 pass (model loads, capabilities populated, complete() returns valid output) |
| Our hand-rolled chat templates match the canonical patterns | 24 byte-exact golden-string tests in `test_chat_templates.cpp` + smoke test 7 cross-validates against llama.cpp's `chatml` |
| Streaming chunks reassemble to `LLMResponse.output` exactly | Smoke test 3 |
| `temp=0` is byte-exact deterministic across calls | Smoke test 4 (two calls, same seed, byte-exact equal) |
| Stop sequences trim cleanly without leaking bytes | Cancellation path tested in smoke test 5 (close-enough proxy); explicit stop-sequence trim verified in chat-template hand-roll tests but not yet against a real model under generation. To pin in Phase 7 with adversarial prompts. |
| Q4_K_M doesn't catastrophically degrade Qwen2.5 | We run it in smoke; smoke produces sensible output. Quantitative perplexity comparison out of scope for this side quest. |

What we accept on trust without independent verification: the GGUF tensor data is correct (i.e., llama.cpp's loader and Qwen's published weights agree); the K-quantization math is right; ggml's CPU SIMD kernels produce correct results across architectures. These are llama.cpp's claims, not ours, and we don't re-prove them.

## References

- llama.cpp source — `third_party/llama.cpp` pinned at `0adede866` (b8925).
- Canonical generation pattern — `third_party/llama.cpp/examples/simple/simple.cpp` (the 220-line reference our `generate()` mirrors).
- Karpathy, "Let's build GPT from scratch" — transformer fundamentals if you want to ground the decode loop in first-principles math.
- BEIR paper (arXiv 2104.08663) — Surface 3's eval methodology.
- Faruqui & Das 2018, "Identifying Well-Formed Natural Language Questions" — Surface 2's source dataset.
- The original llama.cpp README's quantization comparison table (older but still the cleanest published comparison of `Q4_K_M` vs `Q5_K_M` vs `Q8_0` perplexity tradeoffs).
