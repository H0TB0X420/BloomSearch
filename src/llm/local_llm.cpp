#include "llm/local_llm.h"

#include "llm/utf8.h"

#include "ggml-backend.h"
#include "llama.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <thread>

namespace search::llm {

namespace {

// One-shot ggml backend + llama backend init. Idempotent across multiple
// LocalLLMBackend instances thanks to std::call_once.
void init_llama_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        ggml_backend_load_all();
        llama_backend_init();
    });
}

int32_t resolve_threads(int32_t requested) {
    if (requested > 0) return requested;
    auto hw = std::thread::hardware_concurrency();
    if (hw == 0) return 1;
    int32_t n = static_cast<int32_t>(hw / 2);
    return std::max(1, n);
}

std::string derive_model_name(const std::string& path) {
    std::filesystem::path p(path);
    auto stem = p.stem().string();
    return stem.empty() ? path : stem;
}

bool template_emits_bos(ChatTemplate t) {
    return t == ChatTemplate::Llama3;
}

}  // namespace

std::expected<std::unique_ptr<LocalLLMBackend>, LLMError>
LocalLLMBackend::create(const LocalLLMConfig& config) {
    if (config.model_path.empty()) {
        return std::unexpected(LLMError{
            LLMErrorKind::InvalidRequest, "model_path must not be empty"});
    }
    if (!std::filesystem::exists(config.model_path)) {
        return std::unexpected(LLMError{
            LLMErrorKind::BackendUnavailable,
            "model file not found: " + config.model_path});
    }
    if (config.n_ctx == 0) {
        return std::unexpected(LLMError{
            LLMErrorKind::InvalidRequest, "n_ctx must be > 0"});
    }

    init_llama_once();

    auto backend = std::unique_ptr<LocalLLMBackend>(new LocalLLMBackend());
    backend->config_ = config;
    backend->model_name_ = derive_model_name(config.model_path);

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;  // CPU-only

    backend->model_ = llama_model_load_from_file(config.model_path.c_str(), mparams);
    if (backend->model_ == nullptr) {
        return std::unexpected(LLMError{
            LLMErrorKind::Internal,
            "llama_model_load_from_file failed for: " + config.model_path});
    }

    backend->vocab_ = llama_model_get_vocab(backend->model_);

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx   = config.n_ctx;
    cparams.n_batch = std::min<uint32_t>(512, config.n_ctx);
    cparams.no_perf = true;

    backend->ctx_ = llama_init_from_model(backend->model_, cparams);
    if (backend->ctx_ == nullptr) {
        return std::unexpected(LLMError{
            LLMErrorKind::Internal,
            "llama_init_from_model failed (n_ctx=" +
                std::to_string(config.n_ctx) + ")"});
    }

    int32_t nt = resolve_threads(config.n_threads);
    llama_set_n_threads(backend->ctx_, nt, nt);

    return backend;
}

LocalLLMBackend::~LocalLLMBackend() {
    if (ctx_)   llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

Capabilities LocalLLMBackend::capabilities() const {
    Capabilities c;
    c.supports_streaming      = true;
    c.supports_seed           = true;
    c.supports_stop_sequences = true;
    c.max_context_tokens      = ctx_ ? llama_n_ctx(ctx_) : 0;
    return c;
}

std::expected<LLMResponse, LLMError>
LocalLLMBackend::complete(const std::vector<ChatMessage>& messages,
                          const SamplingParams& params) {
    return generate(messages, params, TokenCallback{});
}

std::expected<LLMResponse, LLMError>
LocalLLMBackend::complete_streaming(const std::vector<ChatMessage>& messages,
                                    const SamplingParams& params,
                                    const TokenCallback& on_token) {
    if (!on_token) {
        return std::unexpected(LLMError{
            LLMErrorKind::InvalidRequest, "on_token callback required"});
    }
    return generate(messages, params, on_token);
}

std::expected<LLMResponse, LLMError>
LocalLLMBackend::generate(const std::vector<ChatMessage>& messages,
                          const SamplingParams& params,
                          const TokenCallback& on_token) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    if (messages.empty()) {
        return std::unexpected(LLMError{
            LLMErrorKind::InvalidRequest, "messages must not be empty"});
    }
    if (!ctx_ || !model_ || !vocab_) {
        return std::unexpected(LLMError{
            LLMErrorKind::Internal, "backend not initialized"});
    }

    // 1) Format chat into byte-exact prompt for the configured family.
    auto prompt_r = format_chat(config_.chat_template, messages, true);
    if (!prompt_r) return std::unexpected(prompt_r.error());
    const std::string& prompt = *prompt_r;

    const bool add_special = !template_emits_bos(config_.chat_template);

    // 2) Two-pass tokenize.
    int32_t n_needed = -llama_tokenize(
        vocab_, prompt.data(), static_cast<int32_t>(prompt.size()),
        nullptr, 0, add_special, /*parse_special=*/true);
    if (n_needed <= 0) {
        return std::unexpected(LLMError{
            LLMErrorKind::Internal, "llama_tokenize sizing returned <= 0"});
    }
    std::vector<llama_token> tokens(static_cast<size_t>(n_needed));
    int32_t n_prompt = llama_tokenize(
        vocab_, prompt.data(), static_cast<int32_t>(prompt.size()),
        tokens.data(), static_cast<int32_t>(tokens.size()),
        add_special, /*parse_special=*/true);
    if (n_prompt < 0) {
        return std::unexpected(LLMError{
            LLMErrorKind::Internal, "llama_tokenize 2nd pass failed"});
    }
    tokens.resize(static_cast<size_t>(n_prompt));

    // 3) Context fit check.
    const uint32_t n_ctx = llama_n_ctx(ctx_);
    if (static_cast<uint64_t>(n_prompt) + params.max_tokens >
        static_cast<uint64_t>(n_ctx)) {
        return std::unexpected(LLMError{
            LLMErrorKind::ContextOverflow,
            "prompt (" + std::to_string(n_prompt) +
                ") + max_tokens (" + std::to_string(params.max_tokens) +
                ") exceeds n_ctx (" + std::to_string(n_ctx) + ")"});
    }

    // 4) Stateless: clear KV cache before this call.
    llama_memory_clear(llama_get_memory(ctx_), /*data=*/false);

    // 5) Decode the prompt.
    {
        llama_batch batch =
            llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
        int32_t rc = llama_decode(ctx_, batch);
        if (rc != 0) {
            return std::unexpected(LLMError{
                rc == 1 ? LLMErrorKind::ContextOverflow : LLMErrorKind::Internal,
                "llama_decode (prompt) returned " + std::to_string(rc)});
        }
    }

    // 6) Build per-call sampler chain.
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);
    if (params.temperature <= 0.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    } else {
        if (params.top_k > 0) {
            llama_sampler_chain_add(smpl,
                llama_sampler_init_top_k(params.top_k));
        }
        if (params.top_p > 0.0f && params.top_p < 1.0f) {
            llama_sampler_chain_add(smpl,
                llama_sampler_init_top_p(params.top_p, /*min_keep=*/1));
        }
        llama_sampler_chain_add(smpl,
            llama_sampler_init_temp(params.temperature));
        const uint32_t seed = static_cast<uint32_t>(
            params.seed.value_or(LLAMA_DEFAULT_SEED));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(seed));
    }

    // 7) Streaming bookkeeping.
    //
    // pending      — bytes decoded but not yet emitted (held back due to
    //                UTF-8 codepoint mid-emit OR potential stop-sequence
    //                prefix). Always represents the latest tail of decoded
    //                output; `output` accumulates everything that has been
    //                emitted (callback-delivered or non-streaming-buffered).
    // hold_back    — number of trailing bytes to keep in pending so that a
    //                stop-sequence completion can still be detected. Equals
    //                max_stop_seq_len - 1; zero when no stop sequences.
    std::size_t max_stop = 0;
    for (const auto& s : params.stop_sequences) {
        if (s.size() > max_stop) max_stop = s.size();
    }
    const std::size_t hold_back = max_stop > 0 ? max_stop - 1 : 0;

    std::string output;
    output.reserve(static_cast<size_t>(params.max_tokens) * 4u);
    std::string pending;
    pending.reserve(64);

    uint32_t generated = 0;
    FinishReason finish = FinishReason::Stop;
    bool cancelled = false;

    // Emit a chunk: append to output, optionally call on_token, return
    // false iff streaming caller signalled cancellation. Empty chunks are
    // a no-op.
    auto emit = [&](std::string_view chunk) -> bool {
        if (chunk.empty()) return true;
        output.append(chunk);
        if (on_token) {
            return on_token(chunk);
        }
        return true;
    };

    // 8) Generation loop.
    while (generated < params.max_tokens) {
        llama_token next = llama_sampler_sample(smpl, ctx_, -1);

        if (llama_vocab_is_eog(vocab_, next)) {
            finish = FinishReason::Stop;
            break;
        }

        char piece[256];
        int32_t n = llama_token_to_piece(
            vocab_, next, piece, static_cast<int32_t>(sizeof(piece)),
            /*lstrip=*/0, /*special=*/false);
        if (n < 0) {
            llama_sampler_free(smpl);
            return std::unexpected(LLMError{
                LLMErrorKind::Internal,
                "llama_token_to_piece failed (need " +
                    std::to_string(-n) + " bytes)"});
        }
        pending.append(piece, static_cast<size_t>(n));

        // 8a) Stop-sequence check: does pending end with any configured stop?
        bool stopped_by_seq = false;
        for (const auto& s : params.stop_sequences) {
            if (s.empty()) continue;
            if (pending.size() >= s.size() &&
                std::memcmp(pending.data() + pending.size() - s.size(),
                            s.data(), s.size()) == 0) {
                pending.resize(pending.size() - s.size());
                stopped_by_seq = true;
                break;
            }
        }
        if (stopped_by_seq) {
            // Flush whatever's safe (UTF-8 boundary) of the pre-stop bytes.
            std::size_t safe = utf8_complete_prefix_end(pending);
            if (safe > 0) {
                emit(std::string_view{pending.data(), safe});
            }
            pending.clear();
            finish = FinishReason::Stop;
            break;
        }

        // 8b) Compute safe-to-emit prefix:
        //     pending.size() - hold_back, clamped to UTF-8 boundary.
        std::size_t emit_end = pending.size() > hold_back
                               ? pending.size() - hold_back
                               : 0;
        if (emit_end > 0) {
            emit_end = utf8_complete_prefix_end(
                std::string_view{pending.data(), emit_end});
        }
        if (emit_end > 0) {
            std::string_view chunk{pending.data(), emit_end};
            const bool keep = emit(chunk);
            pending.erase(0, emit_end);
            if (!keep) {
                cancelled = true;
                finish = FinishReason::Cancelled;
                break;
            }
        }

        ++generated;
        if (generated >= params.max_tokens) {
            finish = FinishReason::Length;
            break;
        }

        // 8c) Decode the just-sampled token to advance KV cache.
        llama_batch batch = llama_batch_get_one(&next, 1);
        int32_t rc = llama_decode(ctx_, batch);
        if (rc != 0) {
            llama_sampler_free(smpl);
            return std::unexpected(LLMError{
                rc == 1 ? LLMErrorKind::ContextOverflow : LLMErrorKind::Internal,
                "llama_decode (gen step) returned " + std::to_string(rc)});
        }
    }

    // 9) After-loop flush. For Stop/Length finish reasons, deliver any
    //    remaining bytes that are valid UTF-8. Cancellation does NOT
    //    flush — output already contains exactly the chunk-sequence the
    //    callback saw before saying "stop."
    if (!cancelled && !pending.empty()) {
        std::size_t safe = utf8_complete_prefix_end(pending);
        if (safe > 0) {
            emit(std::string_view{pending.data(), safe});
        }
        // Anything past `safe` is broken UTF-8 from a partial codepoint
        // hitting max_tokens or EOS — drop it. Real models won't routinely
        // hit this; if they do, the emitted text is at most a glyph short.
    }

    llama_sampler_free(smpl);

    const auto t1 = clock::now();
    LLMResponse resp;
    resp.output            = std::move(output);
    resp.prompt_tokens     = static_cast<uint32_t>(n_prompt);
    resp.completion_tokens = generated;
    resp.latency_ms        =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    resp.finish_reason     = finish;
    resp.backend_name      = std::string{backend_name()};
    resp.model_name        = model_name_;
    return resp;
}

}  // namespace search::llm
