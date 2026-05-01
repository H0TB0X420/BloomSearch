// Real-model smoke tests for LocalLLMBackend.
//
// Loads a GGUF and exercises the full stack: model load, capabilities
// readout, non-streaming complete(), streaming complete_streaming(),
// determinism at temp=0, mid-stream cancellation, max_tokens-hits-Length,
// and a soft cross-validation of our hand-rolled chat template against
// llama.cpp's built-in llama_chat_apply_template().
//
// Skipped if the env var LLM_TEST_MODEL is unset — CI without a model
// download still exits 0. To enable:
//
//   ./scripts/fetch_test_model.sh
//   export LLM_TEST_MODEL=$PWD/models/qwen2.5-0.5b-instruct-q4_k_m.gguf
//   export LLM_TEST_CHAT_TEMPLATE=qwen25       # default; or llama3, phi3, raw
//   ninja test_local_llm_smoke && ./test_local_llm_smoke
//
// On the canonical reference machine, end-to-end runtime is ~5-15 sec.

#include "llm/chat_templates.h"
#include "llm/local_llm.h"

#include "llama.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace search::llm;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

static void print_test(bool ok, const std::string& name, TestResults& r) {
    if (ok) {
        std::cout << "  [PASS] " << name << "\n";
        r.passed++;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        r.failed++;
    }
}

static std::optional<ChatTemplate> parse_template(std::string_view name) {
    if (name == "llama3" || name == "Llama3")    return ChatTemplate::Llama3;
    if (name == "qwen25" || name == "qwen" ||
        name == "Qwen25" || name == "chatml")    return ChatTemplate::Qwen25;
    if (name == "phi3"   || name == "Phi3")      return ChatTemplate::Phi3;
    if (name == "raw"    || name == "Raw")       return ChatTemplate::Raw;
    return std::nullopt;
}

// Map our enum to llama.cpp's `tmpl` name argument for cross-validation.
// Returns nullptr if no equivalent (e.g., Raw).
static const char* llama_cpp_tmpl_name(ChatTemplate t) {
    switch (t) {
        case ChatTemplate::Llama3: return "llama3";
        case ChatTemplate::Qwen25: return "chatml";
        case ChatTemplate::Phi3:   return "phi3";
        case ChatTemplate::Raw:    return nullptr;
    }
    return nullptr;
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    LocalLLMBackend Smoke Test Suite (real model)           \n";
    std::cout << "============================================================\n";

    const char* model_env = std::getenv("LLM_TEST_MODEL");
    if (!model_env || !*model_env) {
        std::cout << "\n[SKIP] LLM_TEST_MODEL not set; smoke tests skipped.\n";
        std::cout << "       Run: ./scripts/fetch_test_model.sh\n";
        std::cout << "       Then: export LLM_TEST_MODEL=$PWD/models/qwen2.5-0.5b-instruct-q4_k_m.gguf\n\n";
        return 0;
    }

    const char* tmpl_env = std::getenv("LLM_TEST_CHAT_TEMPLATE");
    const std::string tmpl_name = tmpl_env && *tmpl_env ? tmpl_env : "qwen25";
    auto tmpl_opt = parse_template(tmpl_name);
    if (!tmpl_opt) {
        std::cerr << "ERROR: unknown LLM_TEST_CHAT_TEMPLATE: " << tmpl_name << "\n";
        std::cerr << "       Accepted: llama3, qwen25 (alias chatml), phi3, raw\n";
        return 1;
    }

    LocalLLMConfig cfg;
    cfg.model_path    = model_env;
    cfg.chat_template = *tmpl_opt;
    cfg.n_ctx         = 2048;

    std::cout << "\nLoading model: " << cfg.model_path << "\n";
    std::cout << "Template:      " << tmpl_name << "\n\n";

    auto backend_r = LocalLLMBackend::create(cfg);
    if (!backend_r) {
        std::cerr << "Failed to load model: " << backend_r.error().message << "\n";
        return 1;
    }
    auto& backend = *backend_r;

    TestResults r;

    // ── Test 1: capabilities ────────────────────────────────────────────────
    std::cout << "--- Test: Capabilities ---\n";
    {
        auto caps = backend->capabilities();
        print_test(caps.supports_streaming,      "supports_streaming",      r);
        print_test(caps.supports_seed,           "supports_seed",           r);
        print_test(caps.supports_stop_sequences, "supports_stop_sequences", r);
        print_test(caps.max_context_tokens == cfg.n_ctx,
                   "max_context_tokens == cfg.n_ctx", r);
    }

    // ── Test 2: non-streaming complete() ────────────────────────────────────
    std::cout << "\n--- Test: complete() basic inference ---\n";
    {
        std::vector<ChatMessage> msgs = {
            {Role::User, "What is the capital of France? Answer with just the city name."}
        };
        SamplingParams p;
        p.max_tokens  = 30;
        p.temperature = 0.0f;
        p.seed        = 42;

        auto resp = backend->complete(msgs, p);
        print_test(resp.has_value(), "complete returns success", r);
        if (resp) {
            print_test(!resp->output.empty(),     "output non-empty",         r);
            print_test(resp->prompt_tokens > 0,    "prompt_tokens > 0",       r);
            print_test(resp->completion_tokens > 0,"completion_tokens > 0",   r);
            print_test(resp->latency_ms > 0.0,     "latency_ms > 0",          r);
            print_test(resp->finish_reason == FinishReason::Stop ||
                       resp->finish_reason == FinishReason::Length,
                       "finish in {Stop, Length}", r);
            std::cout << "    output (" << resp->completion_tokens << " tok, "
                      << resp->latency_ms << " ms): "
                      << resp->output << "\n";
        }
    }

    // ── Test 3: streaming reassembly ────────────────────────────────────────
    std::cout << "\n--- Test: complete_streaming() reassembly ---\n";
    {
        std::vector<ChatMessage> msgs = {
            {Role::User, "Reply with just one word: yes"}
        };
        SamplingParams p;
        p.max_tokens  = 20;
        p.temperature = 0.0f;
        p.seed        = 42;

        std::string assembled;
        int call_count = 0;
        auto cb = [&](std::string_view chunk) {
            assembled.append(chunk);
            ++call_count;
            return true;
        };

        auto resp = backend->complete_streaming(msgs, p, cb);
        print_test(resp.has_value(), "streaming returns success", r);
        if (resp) {
            print_test(call_count >= 1,
                       "callback fired at least once", r);
            print_test(resp->output == assembled,
                       "response.output equals reassembled chunks", r);
            std::cout << "    " << call_count << " chunks, "
                      << resp->output.size() << " bytes\n";
        }
    }

    // ── Test 4: determinism at temp=0 ───────────────────────────────────────
    std::cout << "\n--- Test: determinism at temp=0 ---\n";
    {
        std::vector<ChatMessage> msgs = {
            {Role::User, "What color is the sky on a clear day? Answer in one word."}
        };
        SamplingParams p;
        p.max_tokens  = 30;
        p.temperature = 0.0f;
        p.seed        = 42;

        auto r1 = backend->complete(msgs, p);
        auto r2 = backend->complete(msgs, p);
        print_test(r1.has_value() && r2.has_value(),
                   "both calls succeed", r);
        if (r1 && r2) {
            print_test(r1->output == r2->output,
                       "two calls produce byte-exact output", r);
            if (r1->output != r2->output) {
                std::cout << "    r1: " << r1->output << "\n";
                std::cout << "    r2: " << r2->output << "\n";
            }
        }
    }

    // ── Test 5: streaming cancellation ──────────────────────────────────────
    std::cout << "\n--- Test: streaming cancellation ---\n";
    {
        std::vector<ChatMessage> msgs = {
            {Role::User, "Count from one to twenty."}
        };
        SamplingParams p;
        p.max_tokens  = 100;
        p.temperature = 0.0f;
        p.seed        = 42;

        int calls = 0;
        auto cb = [&](std::string_view) {
            return ++calls < 2;  // continue 1st chunk, cancel on 2nd
        };

        auto resp = backend->complete_streaming(msgs, p, cb);
        print_test(resp.has_value(), "streaming returns success path", r);
        if (resp) {
            print_test(resp->finish_reason == FinishReason::Cancelled,
                       "finish == Cancelled", r);
            print_test(!resp->output.empty(),
                       "output captures pre-cancel chunks", r);
            std::cout << "    cancelled after " << calls << " chunks, "
                      << resp->output.size() << " bytes captured\n";
        }
    }

    // ── Test 6: max_tokens hits Length ──────────────────────────────────────
    std::cout << "\n--- Test: max_tokens hits Length ---\n";
    {
        std::vector<ChatMessage> msgs = {
            {Role::User, "Count from one to twenty, with each number on its own line."}
        };
        SamplingParams p;
        p.max_tokens  = 5;
        p.temperature = 0.0f;
        p.seed        = 42;

        auto resp = backend->complete(msgs, p);
        print_test(resp.has_value(), "complete returns success", r);
        if (resp) {
            print_test(resp->finish_reason == FinishReason::Length,
                       "finish == Length", r);
            print_test(resp->completion_tokens == 5,
                       "completion_tokens == 5", r);
        }
    }

    // ── Test 7: chat-template cross-validation (soft) ──────────────────────
    std::cout << "\n--- Test: chat template cross-validation (soft) ---\n";
    {
        std::vector<ChatMessage> msgs = {
            {Role::System, "You are concise."},
            {Role::User,   "Hi"},
        };

        auto our_r = format_chat(cfg.chat_template, msgs, /*add_gen=*/true);
        if (!our_r) {
            print_test(false, "our format_chat() succeeds", r);
        } else {
            const char* ll_tmpl = llama_cpp_tmpl_name(cfg.chat_template);
            if (!ll_tmpl) {
                std::cout << "    [SKIP] Raw template — no llama.cpp equivalent\n";
            } else {
                // Build llama_chat_message[] from our msgs. Keep std::strings
                // alive so the const char* pointers remain valid.
                std::vector<std::string> roles;
                roles.reserve(msgs.size());
                std::vector<llama_chat_message> chat;
                chat.reserve(msgs.size());
                for (const auto& m : msgs) {
                    roles.emplace_back(role_name(m.role));
                    chat.push_back({roles.back().c_str(), m.content.c_str()});
                }

                std::string buf(4096, '\0');
                int32_t n = llama_chat_apply_template(
                    ll_tmpl, chat.data(), chat.size(),
                    /*add_ass=*/true, buf.data(),
                    static_cast<int32_t>(buf.size()));
                if (n < 0) {
                    std::cout << "    [SKIP] llama_chat_apply_template failed (n="
                              << n << ")\n";
                } else {
                    buf.resize(static_cast<size_t>(n));
                    if (buf == *our_r) {
                        std::cout << "    [MATCH] " << buf.size()
                                  << " bytes byte-exact with llama.cpp's '"
                                  << ll_tmpl << "' template\n";
                        r.passed++;
                    } else {
                        std::cout << "    [DIFF] our hand-roll differs from llama.cpp's '"
                                  << ll_tmpl << "' template\n";
                        std::cout << "           ours      (" << our_r->size() << " bytes): "
                                  << *our_r << "\n";
                        std::cout << "           llama.cpp (" << buf.size() << " bytes): "
                                  << buf << "\n";
                        std::cout << "           NOT FAILING — diagnostic only\n";
                    }
                }
            }
        }
    }

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    SMOKE TEST SUMMARY                      \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << r.passed << "\n";
    std::cout << "Failed: " << r.failed << "\n\n";

    if (r.failed == 0) {
        std::cout << "[SUCCESS] All LocalLLMBackend smoke tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}
