#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace search::llm {

// ─── Chat input ─────────────────────────────────────────────────────────────

enum class Role : uint8_t {
    System,
    User,
    Assistant,
};

inline std::string_view role_name(Role r) noexcept {
    switch (r) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
    }
    return "unknown";
}

struct ChatMessage {
    Role role;
    std::string content;
};

enum class ChatTemplate : uint8_t {
    Raw,     // No template; backend concatenates messages with newlines.
    Llama3,  // <|begin_of_text|>...<|start_header_id|>{role}<|end_header_id|>...
    Qwen25,  // <|im_start|>{role}\n{content}<|im_end|>
    Phi3,    // <|user|>{content}<|end|><|assistant|>
};

// ─── Sampling ───────────────────────────────────────────────────────────────

struct SamplingParams {
    float temperature = 0.7f;
    float top_p       = 0.9f;
    int32_t top_k     = 40;
    uint32_t max_tokens = 512;
    std::optional<uint64_t> seed;            // for determinism eval (Surface 1)
    std::vector<std::string> stop_sequences;
};

// ─── Response ───────────────────────────────────────────────────────────────

enum class FinishReason : uint8_t {
    Stop,       // model emitted EOS or hit a configured stop sequence
    Length,     // hit max_tokens
    Cancelled,  // streaming caller returned false from the token callback
    Error,      // model produced an error mid-stream (also reflected in std::expected)
};

inline std::string_view finish_reason_name(FinishReason r) noexcept {
    switch (r) {
        case FinishReason::Stop:      return "stop";
        case FinishReason::Length:    return "length";
        case FinishReason::Cancelled: return "cancelled";
        case FinishReason::Error:     return "error";
    }
    return "unknown";
}

struct LLMResponse {
    std::string output;
    uint32_t prompt_tokens     = 0;
    uint32_t completion_tokens = 0;
    double latency_ms          = 0.0;
    FinishReason finish_reason = FinishReason::Stop;
    std::string backend_name;
    std::string model_name;
};

// ─── Errors ─────────────────────────────────────────────────────────────────

enum class LLMErrorKind : uint8_t {
    BackendUnavailable,  // model failed to load, network down
    InvalidRequest,      // empty messages, contradictory params, etc.
    ContextOverflow,     // prompt + max_tokens > model's context window
    Internal,            // anything else
};

inline std::string_view llm_error_kind_name(LLMErrorKind k) noexcept {
    switch (k) {
        case LLMErrorKind::BackendUnavailable: return "backend_unavailable";
        case LLMErrorKind::InvalidRequest:     return "invalid_request";
        case LLMErrorKind::ContextOverflow:    return "context_overflow";
        case LLMErrorKind::Internal:           return "internal";
    }
    return "unknown";
}

struct LLMError {
    LLMErrorKind kind = LLMErrorKind::Internal;
    std::string message;
};

// ─── Capabilities ───────────────────────────────────────────────────────────

struct Capabilities {
    bool supports_streaming      = false;
    bool supports_seed           = false;
    bool supports_stop_sequences = false;
    uint32_t max_context_tokens  = 0;
};

// ─── Streaming callback ─────────────────────────────────────────────────────

// Invoked once per token chunk during streaming generation.
// Return false from the callback to cancel; LLMResponse will then carry
// finish_reason == Cancelled and `output` will hold whatever was generated up
// to (and including) the chunk on which cancellation was signalled.
using TokenCallback = std::function<bool(std::string_view)>;

// ─── Backend interface ──────────────────────────────────────────────────────
//
// Concurrency: implementations are NOT required to be thread-safe. Callers
// must serialize calls into a single backend instance.
//
// Errors: backends signal hard failures via std::unexpected. Soft failures
// (max-tokens hit, caller cancelled) are returned as ordinary LLMResponses
// with the appropriate FinishReason.
class LLMBackend {
public:
    virtual ~LLMBackend() = default;

    LLMBackend(const LLMBackend&) = delete;
    LLMBackend& operator=(const LLMBackend&) = delete;
    LLMBackend(LLMBackend&&) = delete;
    LLMBackend& operator=(LLMBackend&&) = delete;

    // Non-streaming completion. Blocks until the model finishes.
    virtual std::expected<LLMResponse, LLMError>
    complete(const std::vector<ChatMessage>& messages,
             const SamplingParams& params) = 0;

    // Streaming completion. The callback is invoked once per token chunk.
    // The final LLMResponse.output equals the concatenation of all chunks
    // delivered to the callback (regardless of finish reason).
    virtual std::expected<LLMResponse, LLMError>
    complete_streaming(const std::vector<ChatMessage>& messages,
                       const SamplingParams& params,
                       const TokenCallback& on_token) = 0;

    virtual Capabilities capabilities() const = 0;
    virtual std::string_view backend_name() const = 0;
    virtual std::string_view model_name() const = 0;

protected:
    LLMBackend() = default;
};

}  // namespace search::llm
