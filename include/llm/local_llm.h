#pragma once

#include "llm/chat_templates.h"
#include "llm/llm_backend.h"

#include <expected>
#include <memory>
#include <string>

// Forward-declare llama types so this header doesn't pull <llama.h> into
// every translation unit that consumes the LLM interface.
struct llama_model;
struct llama_context;
struct llama_vocab;

namespace search::llm {

struct LocalLLMConfig {
    std::string  model_path;                            // path to GGUF
    ChatTemplate chat_template = ChatTemplate::Raw;     // see chat_templates.h
    uint32_t     n_ctx     = 4096;                      // context window
    int32_t      n_threads = -1;                        // -1 = auto
};

// LocalLLMBackend — llama.cpp-backed inference, CPU-only.
//
// Construction goes through LocalLLMBackend::create() which returns either a
// fully-initialized backend or an LLMError. There is no public constructor —
// this keeps the "no exceptions across the boundary" rule consistent with
// the rest of the LLM interface.
//
// Concurrency: a single instance is NOT thread-safe. Callers must serialize
// per-instance. Multiple instances (e.g., one per worker thread) coexist
// safely; llama_backend_init() is one-shot via std::call_once.
//
// Tokenization details (handled internally; documented for review):
//   - Llama-3 chat template emits its own BOS, so we tokenize with
//     add_special=false to avoid a duplicate <|begin_of_text|>.
//   - Qwen2.5, Phi-3, Raw do NOT emit BOS, so we tokenize with
//     add_special=true and let the tokenizer add BOS as configured.
//   - parse_special=true everywhere, so chat-template marker tokens
//     (<|im_start|>, <|eot_id|>, <|end|>, etc.) tokenize to their
//     designated special-token IDs rather than as plain text.
class LocalLLMBackend final : public LLMBackend {
public:
    static std::expected<std::unique_ptr<LocalLLMBackend>, LLMError>
    create(const LocalLLMConfig& config);

    ~LocalLLMBackend() override;

    std::expected<LLMResponse, LLMError>
    complete(const std::vector<ChatMessage>& messages,
             const SamplingParams& params) override;

    std::expected<LLMResponse, LLMError>
    complete_streaming(const std::vector<ChatMessage>& messages,
                       const SamplingParams& params,
                       const TokenCallback& on_token) override;

    Capabilities     capabilities() const override;
    std::string_view backend_name() const override { return "local"; }
    std::string_view model_name()   const override { return model_name_; }

private:
    LocalLLMBackend() = default;

    // Shared implementation for complete() and complete_streaming(). When
    // `on_token` is empty the call is non-streaming; bytes are accumulated
    // into LLMResponse.output and the callback is never invoked. When
    // `on_token` is non-empty the same loop drives streaming emission with
    // UTF-8 boundary respect, stop-sequence suffix detection, and
    // cancellation via the callback's return value.
    std::expected<LLMResponse, LLMError>
    generate(const std::vector<ChatMessage>& messages,
             const SamplingParams& params,
             const TokenCallback& on_token);

    LocalLLMConfig     config_{};
    llama_model*       model_   = nullptr;
    llama_context*     ctx_     = nullptr;
    const llama_vocab* vocab_   = nullptr;
    std::string        model_name_;
};

}  // namespace search::llm
