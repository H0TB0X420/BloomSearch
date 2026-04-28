#include "llm/mock_llm.h"

#include <cctype>
#include <chrono>
#include <thread>
#include <utility>

namespace search::llm {

namespace {

// Word count for token bookkeeping. Whitespace runs collapse; counts only
// tokens that contain at least one non-whitespace character.
uint32_t word_count(std::string_view s) noexcept {
    uint32_t n = 0;
    bool in_word = false;
    for (char c : s) {
        bool ws = std::isspace(static_cast<unsigned char>(c));
        if (!ws && !in_word) {
            ++n;
            in_word = true;
        } else if (ws) {
            in_word = false;
        }
    }
    return n;
}

uint32_t prompt_token_count(const std::vector<ChatMessage>& msgs) noexcept {
    uint32_t n = 0;
    for (const auto& m : msgs) {
        n += word_count(m.content);
        n += 1;  // crude allowance for the role marker token
    }
    return n;
}

// Split `s` into chunks alternating word / whitespace, preserving original
// content so that concatenation reconstructs `s`. A token here is any
// maximal run of whitespace OR maximal run of non-whitespace.
std::vector<std::string_view> split_chunks(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        bool ws = std::isspace(static_cast<unsigned char>(s[i]));
        size_t j = i + 1;
        while (j < s.size() &&
               static_cast<bool>(std::isspace(static_cast<unsigned char>(s[j]))) == ws) {
            ++j;
        }
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// Truncate a chunk list to at most `max_words` non-whitespace chunks, returning
// the prefix sub-vector. If truncation occurred, *truncated is set to true.
std::vector<std::string_view> apply_max_tokens(
    const std::vector<std::string_view>& chunks,
    uint32_t max_words,
    bool* truncated) {
    *truncated = false;
    if (max_words == 0) {
        // 0 is treated as "no limit" — backends generally interpret 0 this way.
        return chunks;
    }
    uint32_t kept_words = 0;
    std::vector<std::string_view> out;
    out.reserve(chunks.size());
    for (const auto& c : chunks) {
        bool is_word = !c.empty() &&
                       !std::isspace(static_cast<unsigned char>(c.front()));
        if (is_word) {
            if (kept_words >= max_words) {
                *truncated = true;
                break;
            }
            ++kept_words;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

MockLLM::MockLLM() = default;

void MockLLM::enqueue_response(std::string output, FinishReason finish) {
    Canned c;
    c.payload = std::make_pair(std::move(output), finish);
    queue_.push_back(std::move(c));
}

void MockLLM::enqueue_error(LLMError err) {
    Canned c;
    c.payload = std::move(err);
    queue_.push_back(std::move(c));
}

Capabilities MockLLM::capabilities() const {
    Capabilities caps;
    caps.supports_streaming      = true;
    caps.supports_seed           = true;
    caps.supports_stop_sequences = true;
    caps.max_context_tokens      = 4096;
    return caps;
}

std::expected<LLMResponse, LLMError>
MockLLM::complete(const std::vector<ChatMessage>& messages,
                  const SamplingParams& params) {
    if (messages.empty()) {
        return std::unexpected(
            LLMError{LLMErrorKind::InvalidRequest, "messages must not be empty"});
    }
    if (queue_.empty()) {
        return std::unexpected(
            LLMError{LLMErrorKind::Internal, "MockLLM: no canned response queued"});
    }

    auto start = std::chrono::steady_clock::now();
    Canned item = std::move(queue_.front());
    queue_.pop_front();

    if (std::holds_alternative<LLMError>(item.payload)) {
        return std::unexpected(std::get<LLMError>(std::move(item.payload)));
    }

    auto& [output, finish] = std::get<std::pair<std::string, FinishReason>>(item.payload);

    // Apply max_tokens truncation (word-level) if it would actually cut the response.
    auto chunks = split_chunks(output);
    bool truncated = false;
    auto kept = apply_max_tokens(chunks, params.max_tokens, &truncated);
    std::string final_output;
    final_output.reserve(output.size());
    for (auto c : kept) final_output.append(c);

    FinishReason fr = truncated ? FinishReason::Length : finish;

    LLMResponse resp;
    resp.output            = std::move(final_output);
    resp.prompt_tokens     = prompt_token_count(messages);
    resp.completion_tokens = word_count(resp.output);
    resp.finish_reason     = fr;
    resp.backend_name      = std::string{backend_name()};
    resp.model_name        = std::string{model_name()};

    auto end = std::chrono::steady_clock::now();
    resp.latency_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    // Floor latency at a tiny non-zero value so tests can rely on `> 0`.
    if (resp.latency_ms <= 0.0) resp.latency_ms = 0.001;
    return resp;
}

std::expected<LLMResponse, LLMError>
MockLLM::complete_streaming(const std::vector<ChatMessage>& messages,
                            const SamplingParams& params,
                            const TokenCallback& on_token) {
    if (messages.empty()) {
        return std::unexpected(
            LLMError{LLMErrorKind::InvalidRequest, "messages must not be empty"});
    }
    if (!on_token) {
        return std::unexpected(
            LLMError{LLMErrorKind::InvalidRequest, "on_token callback required"});
    }
    if (queue_.empty()) {
        return std::unexpected(
            LLMError{LLMErrorKind::Internal, "MockLLM: no canned response queued"});
    }

    auto start = std::chrono::steady_clock::now();
    Canned item = std::move(queue_.front());
    queue_.pop_front();

    if (std::holds_alternative<LLMError>(item.payload)) {
        return std::unexpected(std::get<LLMError>(std::move(item.payload)));
    }

    auto& [output, finish] = std::get<std::pair<std::string, FinishReason>>(item.payload);

    auto chunks = split_chunks(output);
    bool truncated = false;
    auto kept = apply_max_tokens(chunks, params.max_tokens, &truncated);

    std::string streamed;
    streamed.reserve(output.size());
    bool cancelled = false;
    for (auto c : kept) {
        if (per_token_delay_ms_ > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::duration<double, std::milli>(per_token_delay_ms_));
        }
        streamed.append(c);
        if (!on_token(c)) {
            cancelled = true;
            break;
        }
    }

    FinishReason fr;
    if (cancelled)       fr = FinishReason::Cancelled;
    else if (truncated)  fr = FinishReason::Length;
    else                 fr = finish;

    LLMResponse resp;
    resp.output            = std::move(streamed);
    resp.prompt_tokens     = prompt_token_count(messages);
    resp.completion_tokens = word_count(resp.output);
    resp.finish_reason     = fr;
    resp.backend_name      = std::string{backend_name()};
    resp.model_name        = std::string{model_name()};

    auto end = std::chrono::steady_clock::now();
    resp.latency_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    if (resp.latency_ms <= 0.0) resp.latency_ms = 0.001;
    return resp;
}

}  // namespace search::llm
