#pragma once

#include "llm/llm_backend.h"

#include <deque>
#include <variant>

namespace search::llm {

// MockLLM — programmable backend for tests.
//
// Tests push canned responses or canned errors via enqueue_response / enqueue_error.
// Calls to complete() / complete_streaming() consume the queue in FIFO order.
//
// Streaming splits the canned `output` on whitespace runs and re-emits whitespace,
// so the concatenation of streamed chunks reconstructs `output` exactly.
//
// Token counts are word-counts (whitespace-delimited) — sufficient for tests.
class MockLLM final : public LLMBackend {
public:
    MockLLM();

    // Push a canned successful response. `output` is what the next call will return.
    // `finish` lets a test assert finish-reason handling without driving the model
    // through a real stop sequence or length cap.
    void enqueue_response(std::string output,
                          FinishReason finish = FinishReason::Stop);

    // Push a canned hard error. The next call returns std::unexpected(err).
    void enqueue_error(LLMError err);

    // Add a per-token sleep during streaming. Useful for asserting that
    // latency_ms accumulates reasonably. Default 0.
    void set_per_token_delay_ms(double ms) { per_token_delay_ms_ = ms; }

    // How many canned items remain in the queue.
    size_t pending() const noexcept { return queue_.size(); }

    std::expected<LLMResponse, LLMError>
    complete(const std::vector<ChatMessage>& messages,
             const SamplingParams& params) override;

    std::expected<LLMResponse, LLMError>
    complete_streaming(const std::vector<ChatMessage>& messages,
                       const SamplingParams& params,
                       const TokenCallback& on_token) override;

    Capabilities capabilities() const override;
    std::string_view backend_name() const override { return "mock"; }
    std::string_view model_name() const override { return "mock-1"; }

private:
    struct Canned {
        std::variant<std::pair<std::string, FinishReason>, LLMError> payload;
    };

    std::deque<Canned> queue_;
    double per_token_delay_ms_ = 0.0;
};

}  // namespace search::llm
