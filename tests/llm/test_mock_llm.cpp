#include "llm/mock_llm.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace search::llm;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool ok, const std::string& name, TestResults& r) {
    if (ok) {
        std::cout << "  [PASS] " << name << "\n";
        r.passed++;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        r.failed++;
    }
}

static std::vector<ChatMessage> simple_user_prompt(const std::string& s = "hello") {
    return { ChatMessage{Role::User, s} };
}

// ─── Test 1: basic non-streaming completion ────────────────────────────────
void test_basic_complete(TestResults& r) {
    std::cout << "\n--- Test: Basic complete() ---\n";

    MockLLM m;
    m.enqueue_response("hello world");

    auto resp = m.complete(simple_user_prompt(), SamplingParams{});
    print_test(resp.has_value(), "complete returns success", r);
    if (!resp) return;
    print_test(resp->output == "hello world", "output matches canned response", r);
    print_test(resp->finish_reason == FinishReason::Stop, "default finish=Stop", r);
    print_test(resp->completion_tokens == 2, "completion_tokens counts words", r);
    print_test(resp->prompt_tokens > 0, "prompt_tokens populated", r);
    print_test(resp->latency_ms > 0.0, "latency_ms > 0", r);
    print_test(resp->backend_name == "mock", "backend_name='mock'", r);
    print_test(!resp->model_name.empty(), "model_name populated", r);
}

// ─── Test 2: FIFO ordering across multiple calls ───────────────────────────
void test_fifo_order(TestResults& r) {
    std::cout << "\n--- Test: FIFO queue order ---\n";

    MockLLM m;
    m.enqueue_response("first");
    m.enqueue_response("second");
    m.enqueue_response("third");
    print_test(m.pending() == 3, "queue has 3 pending", r);

    auto a = m.complete(simple_user_prompt(), {});
    auto b = m.complete(simple_user_prompt(), {});
    auto c = m.complete(simple_user_prompt(), {});
    print_test(a && a->output == "first",  "1st returns 'first'", r);
    print_test(b && b->output == "second", "2nd returns 'second'", r);
    print_test(c && c->output == "third",  "3rd returns 'third'", r);
    print_test(m.pending() == 0, "queue drained", r);
}

// ─── Test 3: empty queue returns Internal error ────────────────────────────
void test_empty_queue(TestResults& r) {
    std::cout << "\n--- Test: Empty queue ---\n";

    MockLLM m;
    auto resp = m.complete(simple_user_prompt(), {});
    print_test(!resp.has_value(), "empty queue returns unexpected", r);
    if (!resp) {
        print_test(resp.error().kind == LLMErrorKind::Internal,
                   "kind=Internal", r);
        print_test(!resp.error().message.empty(),
                   "error message present", r);
    }
}

// ─── Test 4: programmed error returns std::unexpected ──────────────────────
void test_programmed_error(TestResults& r) {
    std::cout << "\n--- Test: Programmed error ---\n";

    MockLLM m;
    m.enqueue_error(LLMError{LLMErrorKind::BackendUnavailable, "model not loaded"});

    auto resp = m.complete(simple_user_prompt(), {});
    print_test(!resp.has_value(), "returns unexpected", r);
    if (!resp) {
        print_test(resp.error().kind == LLMErrorKind::BackendUnavailable,
                   "kind=BackendUnavailable", r);
        print_test(resp.error().message == "model not loaded",
                   "message round-trips", r);
    }
}

// ─── Test 5: empty messages → InvalidRequest ───────────────────────────────
void test_empty_messages(TestResults& r) {
    std::cout << "\n--- Test: Empty messages ---\n";

    MockLLM m;
    m.enqueue_response("ignored");
    auto resp = m.complete({}, {});
    print_test(!resp.has_value(), "empty messages rejected", r);
    if (!resp) {
        print_test(resp.error().kind == LLMErrorKind::InvalidRequest,
                   "kind=InvalidRequest", r);
    }
}

// ─── Test 6: streaming fires callback per chunk; output reassembles ────────
void test_streaming_basic(TestResults& r) {
    std::cout << "\n--- Test: Streaming basic ---\n";

    MockLLM m;
    m.enqueue_response("alpha beta gamma");

    int word_calls = 0;
    std::string assembled;
    auto cb = [&](std::string_view chunk) {
        assembled.append(chunk);
        bool is_word = !chunk.empty() &&
                       !std::isspace(static_cast<unsigned char>(chunk.front()));
        if (is_word) ++word_calls;
        return true;
    };

    auto resp = m.complete_streaming(simple_user_prompt(), {}, cb);
    print_test(resp.has_value(), "streaming returns success", r);
    if (!resp) return;
    print_test(word_calls == 3, "callback fired 3 times for 3 words", r);
    print_test(assembled == "alpha beta gamma", "chunks reassemble exactly", r);
    print_test(resp->output == "alpha beta gamma", "response.output matches", r);
    print_test(resp->finish_reason == FinishReason::Stop, "finish=Stop", r);
    print_test(resp->completion_tokens == 3, "completion_tokens=3", r);
}

// ─── Test 7: cancellation captures partial output ──────────────────────────
void test_streaming_cancel(TestResults& r) {
    std::cout << "\n--- Test: Streaming cancellation ---\n";

    MockLLM m;
    m.enqueue_response("a b c d e");

    int word_calls = 0;
    auto cb = [&](std::string_view chunk) {
        bool is_word = !chunk.empty() &&
                       !std::isspace(static_cast<unsigned char>(chunk.front()));
        if (is_word) ++word_calls;
        // Cancel after the second word.
        return word_calls < 2;
    };

    auto resp = m.complete_streaming(simple_user_prompt(), {}, cb);
    print_test(resp.has_value(), "cancellation is success path", r);
    if (!resp) return;
    print_test(resp->finish_reason == FinishReason::Cancelled,
               "finish=Cancelled", r);
    // After 2 word callbacks, output should contain "a b" (and be shorter than full).
    print_test(resp->output.find("a") != std::string::npos &&
               resp->output.find("b") != std::string::npos,
               "partial output contains pre-cancel words", r);
    print_test(resp->output.find("e") == std::string::npos,
               "partial output excludes post-cancel words", r);
}

// ─── Test 8: max_tokens truncates and sets finish=Length ───────────────────
void test_max_tokens_truncate(TestResults& r) {
    std::cout << "\n--- Test: max_tokens truncation ---\n";

    MockLLM m;
    m.enqueue_response("one two three four five");

    SamplingParams p;
    p.max_tokens = 3;
    auto resp = m.complete(simple_user_prompt(), p);
    print_test(resp.has_value(), "complete returns success", r);
    if (!resp) return;
    print_test(resp->finish_reason == FinishReason::Length,
               "finish=Length when truncated", r);
    print_test(resp->completion_tokens == 3,
               "exactly 3 words emitted", r);
    print_test(resp->output.find("four") == std::string::npos,
               "fourth word truncated", r);
}

// ─── Test 9: max_tokens does NOT truncate when response fits ───────────────
void test_max_tokens_fits(TestResults& r) {
    std::cout << "\n--- Test: max_tokens does not affect fitting response ---\n";

    MockLLM m;
    m.enqueue_response("one two");
    SamplingParams p;
    p.max_tokens = 10;
    auto resp = m.complete(simple_user_prompt(), p);
    print_test(resp && resp->finish_reason == FinishReason::Stop,
               "finish=Stop", r);
    print_test(resp && resp->completion_tokens == 2,
               "completion_tokens=2", r);
    print_test(resp && resp->output == "one two",
               "full output preserved", r);
}

// ─── Test 10: streaming respects max_tokens ────────────────────────────────
void test_streaming_max_tokens(TestResults& r) {
    std::cout << "\n--- Test: Streaming respects max_tokens ---\n";

    MockLLM m;
    m.enqueue_response("one two three four five");
    SamplingParams p;
    p.max_tokens = 2;

    int word_calls = 0;
    auto cb = [&](std::string_view chunk) {
        bool is_word = !chunk.empty() &&
                       !std::isspace(static_cast<unsigned char>(chunk.front()));
        if (is_word) ++word_calls;
        return true;
    };

    auto resp = m.complete_streaming(simple_user_prompt(), p, cb);
    print_test(resp && resp->finish_reason == FinishReason::Length,
               "finish=Length", r);
    print_test(word_calls == 2,
               "callback fired exactly 2 times", r);
}

// ─── Test 11: capabilities ─────────────────────────────────────────────────
void test_capabilities(TestResults& r) {
    std::cout << "\n--- Test: Capabilities ---\n";

    MockLLM m;
    auto caps = m.capabilities();
    print_test(caps.supports_streaming,      "supports_streaming",      r);
    print_test(caps.supports_seed,           "supports_seed",           r);
    print_test(caps.supports_stop_sequences, "supports_stop_sequences", r);
    print_test(caps.max_context_tokens > 0,  "max_context_tokens > 0",  r);
}

// ─── Test 12: empty response edge case ─────────────────────────────────────
void test_empty_response(TestResults& r) {
    std::cout << "\n--- Test: Empty programmed response ---\n";

    MockLLM m;
    m.enqueue_response("");

    auto resp = m.complete(simple_user_prompt(), {});
    print_test(resp.has_value(), "empty response is success", r);
    if (resp) {
        print_test(resp->output.empty(), "output empty", r);
        print_test(resp->completion_tokens == 0, "completion_tokens=0", r);
        print_test(resp->finish_reason == FinishReason::Stop, "finish=Stop", r);
    }

    // Streaming version: callback should fire 0 times.
    m.enqueue_response("");
    int calls = 0;
    auto cb = [&](std::string_view) { ++calls; return true; };
    auto sresp = m.complete_streaming(simple_user_prompt(), {}, cb);
    print_test(sresp && calls == 0, "streaming empty: 0 callback invocations", r);
}

// ─── Test 13: explicit non-Stop finish reason from canned ──────────────────
void test_finish_reason_passthrough(TestResults& r) {
    std::cout << "\n--- Test: Canned finish_reason passthrough ---\n";

    MockLLM m;
    m.enqueue_response("hi", FinishReason::Error);
    auto resp = m.complete(simple_user_prompt(), {});
    print_test(resp && resp->finish_reason == FinishReason::Error,
               "canned Error finish_reason surfaces", r);

    // But truncation overrides the canned value:
    m.enqueue_response("a b c d", FinishReason::Stop);
    SamplingParams p; p.max_tokens = 2;
    auto resp2 = m.complete(simple_user_prompt(), p);
    print_test(resp2 && resp2->finish_reason == FinishReason::Length,
               "truncation overrides canned finish_reason", r);
}

// ─── Test 14: streaming cancellation overrides truncation ──────────────────
void test_cancel_priority(TestResults& r) {
    std::cout << "\n--- Test: Cancel beats Length ---\n";

    MockLLM m;
    m.enqueue_response("a b c d e");
    SamplingParams p; p.max_tokens = 4;

    int word_calls = 0;
    auto cb = [&](std::string_view chunk) {
        bool is_word = !chunk.empty() &&
                       !std::isspace(static_cast<unsigned char>(chunk.front()));
        if (is_word) ++word_calls;
        return word_calls < 2;  // cancel after 1st word
    };

    auto resp = m.complete_streaming(simple_user_prompt(), p, cb);
    print_test(resp && resp->finish_reason == FinishReason::Cancelled,
               "Cancelled > Length when both would apply", r);
}

// ─── Test 15: latency injection accumulates ────────────────────────────────
void test_latency_injection(TestResults& r) {
    std::cout << "\n--- Test: Per-token latency injection ---\n";

    MockLLM m;
    m.set_per_token_delay_ms(2.0);
    m.enqueue_response("a b c");

    auto cb = [](std::string_view) { return true; };
    auto t0 = std::chrono::steady_clock::now();
    auto resp = m.complete_streaming(simple_user_prompt(), {}, cb);
    auto wall_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count();

    print_test(resp.has_value(), "streaming returns success", r);
    if (!resp) return;
    // 3 words + 2 whitespace chunks = 5 chunks, 2ms each = ~10ms minimum.
    print_test(resp->latency_ms >= 6.0,
               "latency_ms reflects sleeps (>= 6ms)", r);
    print_test(wall_ms + 1.0 >= resp->latency_ms,
               "wall time >= reported latency", r);
}

// ─── Test 16: required callback for streaming ──────────────────────────────
void test_streaming_requires_callback(TestResults& r) {
    std::cout << "\n--- Test: Streaming requires callback ---\n";

    MockLLM m;
    m.enqueue_response("hi");
    auto resp = m.complete_streaming(simple_user_prompt(), {}, TokenCallback{});
    print_test(!resp && resp.error().kind == LLMErrorKind::InvalidRequest,
               "null callback rejected", r);
}

// ─── Test 17: prompt_tokens accounts for multiple messages ─────────────────
void test_prompt_token_counting(TestResults& r) {
    std::cout << "\n--- Test: Prompt token counting ---\n";

    MockLLM m;
    m.enqueue_response("ok");
    std::vector<ChatMessage> msgs = {
        {Role::System,    "you are a helpful assistant"},   // 5 words
        {Role::User,      "tell me about C++"},             // 4 words
    };
    auto resp = m.complete(msgs, {});
    print_test(resp.has_value(), "complete returns success", r);
    if (resp) {
        // 5 + 4 = 9 word tokens, plus 1 role marker per message → 11 total.
        print_test(resp->prompt_tokens >= 9,
                   "prompt_tokens accounts for content (>=9)", r);
    }
}

// ─── Test 18: backend_name and model_name surfaced on the response ─────────
void test_response_metadata(TestResults& r) {
    std::cout << "\n--- Test: Response metadata ---\n";

    MockLLM m;
    m.enqueue_response("ok");
    auto resp = m.complete(simple_user_prompt(), {});
    print_test(resp && resp->backend_name == "mock",
               "response.backend_name='mock'", r);
    print_test(resp && !resp->model_name.empty(),
               "response.model_name populated", r);
}

// ─── Test 19: helper enum names ────────────────────────────────────────────
void test_enum_helpers(TestResults& r) {
    std::cout << "\n--- Test: Enum name helpers ---\n";
    print_test(role_name(Role::System)    == "system",    "role_name(System)",    r);
    print_test(role_name(Role::User)      == "user",      "role_name(User)",      r);
    print_test(role_name(Role::Assistant) == "assistant", "role_name(Assistant)", r);

    print_test(finish_reason_name(FinishReason::Stop)      == "stop",      "fr_name(Stop)",      r);
    print_test(finish_reason_name(FinishReason::Length)    == "length",    "fr_name(Length)",    r);
    print_test(finish_reason_name(FinishReason::Cancelled) == "cancelled", "fr_name(Cancelled)", r);
    print_test(finish_reason_name(FinishReason::Error)     == "error",     "fr_name(Error)",     r);

    print_test(llm_error_kind_name(LLMErrorKind::BackendUnavailable) == "backend_unavailable",
               "ek_name(BackendUnavailable)", r);
    print_test(llm_error_kind_name(LLMErrorKind::InvalidRequest)     == "invalid_request",
               "ek_name(InvalidRequest)", r);
    print_test(llm_error_kind_name(LLMErrorKind::ContextOverflow)    == "context_overflow",
               "ek_name(ContextOverflow)", r);
    print_test(llm_error_kind_name(LLMErrorKind::Internal)           == "internal",
               "ek_name(Internal)", r);
}

// ─── Test 20: error item does not consume future canned responses ─────────
void test_error_then_response(TestResults& r) {
    std::cout << "\n--- Test: Error followed by response ---\n";

    MockLLM m;
    m.enqueue_error(LLMError{LLMErrorKind::Internal, "boom"});
    m.enqueue_response("recovery");

    auto bad = m.complete(simple_user_prompt(), {});
    print_test(!bad.has_value(), "first call fails", r);

    auto good = m.complete(simple_user_prompt(), {});
    print_test(good && good->output == "recovery",
               "second call returns next canned response", r);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    MockLLM Contract Test Suite                             \n";
    std::cout << "============================================================\n";

    TestResults r;

    test_basic_complete(r);
    test_fifo_order(r);
    test_empty_queue(r);
    test_programmed_error(r);
    test_empty_messages(r);
    test_streaming_basic(r);
    test_streaming_cancel(r);
    test_max_tokens_truncate(r);
    test_max_tokens_fits(r);
    test_streaming_max_tokens(r);
    test_capabilities(r);
    test_empty_response(r);
    test_finish_reason_passthrough(r);
    test_cancel_priority(r);
    test_latency_injection(r);
    test_streaming_requires_callback(r);
    test_prompt_token_counting(r);
    test_response_metadata(r);
    test_enum_helpers(r);
    test_error_then_response(r);

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << r.passed << "\n";
    std::cout << "Failed: " << r.failed << "\n\n";

    if (r.failed == 0) {
        std::cout << "[SUCCESS] All MockLLM contract tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}
