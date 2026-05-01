// Golden-string tests for chat-template formatters.
//
// Each named template's output is pinned to byte-exact strings copied from a
// reviewed dump (Block 2.2 stop-point inspection on 2026-04-29). If a future
// edit changes a formatter, these tests fail with a printable diff. The
// reference patterns are sourced from each model family's published
// tokenizer.json chat_template; see chat_templates.cpp for the Jinja sources.
#include "llm/chat_templates.h"

#include <cassert>
#include <iostream>
#include <string>
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

// Pretty-print a string with newlines/tabs escaped, for diff readability.
static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:   out += c;
        }
    }
    return out;
}

// Compare actual vs expected byte-for-byte; on mismatch, print both escaped.
static void expect_eq(const std::string& expected,
                      const std::string& actual,
                      const std::string& name,
                      TestResults& r) {
    if (expected == actual) {
        print_test(true, name, r);
        return;
    }
    print_test(false, name, r);
    std::cout << "    expected (" << expected.size() << " bytes): "
              << escape(expected) << "\n";
    std::cout << "    actual   (" << actual.size() << " bytes): "
              << escape(actual) << "\n";
}

static std::vector<ChatMessage> two_msg() {
    return {
        {Role::System, "You are a helpful assistant."},
        {Role::User,   "What is the capital of France?"},
    };
}

static std::vector<ChatMessage> multi_turn() {
    return {
        {Role::System,    "You are concise."},
        {Role::User,      "What's 2+2?"},
        {Role::Assistant, "4"},
        {Role::User,      "And 3+3?"},
    };
}

// ─── Llama3 ─────────────────────────────────────────────────────────────────
void test_llama3(TestResults& r) {
    std::cout << "\n--- Llama3 ---\n";

    // Two-msg, gen=true (228 bytes)
    {
        auto out = format_chat(ChatTemplate::Llama3, two_msg(), true);
        const std::string expected =
            "<|begin_of_text|>"
            "<|start_header_id|>system<|end_header_id|>\n\n"
            "You are a helpful assistant.<|eot_id|>"
            "<|start_header_id|>user<|end_header_id|>\n\n"
            "What is the capital of France?<|eot_id|>"
            "<|start_header_id|>assistant<|end_header_id|>\n\n";
        print_test(out.has_value(), "two-msg returns success", r);
        if (out) expect_eq(expected, *out, "two-msg byte-exact", r);
    }

    // Two-msg, gen=false — no trailing assistant marker
    {
        auto out = format_chat(ChatTemplate::Llama3, two_msg(), false);
        const std::string expected =
            "<|begin_of_text|>"
            "<|start_header_id|>system<|end_header_id|>\n\n"
            "You are a helpful assistant.<|eot_id|>"
            "<|start_header_id|>user<|end_header_id|>\n\n"
            "What is the capital of France?<|eot_id|>";
        if (out) expect_eq(expected, *out, "gen=false omits assistant suffix", r);
    }

    // Multi-turn (4 msgs), gen=true (315 bytes)
    {
        auto out = format_chat(ChatTemplate::Llama3, multi_turn(), true);
        const std::string expected =
            "<|begin_of_text|>"
            "<|start_header_id|>system<|end_header_id|>\n\n"
            "You are concise.<|eot_id|>"
            "<|start_header_id|>user<|end_header_id|>\n\n"
            "What's 2+2?<|eot_id|>"
            "<|start_header_id|>assistant<|end_header_id|>\n\n"
            "4<|eot_id|>"
            "<|start_header_id|>user<|end_header_id|>\n\n"
            "And 3+3?<|eot_id|>"
            "<|start_header_id|>assistant<|end_header_id|>\n\n";
        if (out) expect_eq(expected, *out, "4-msg multi-turn byte-exact", r);
    }

    // Trim — leading/trailing whitespace stripped per Llama-3 Jinja `| trim`
    {
        std::vector<ChatMessage> trim = {{Role::User, "  hello  \n"}};
        auto out = format_chat(ChatTemplate::Llama3, trim, true);
        const std::string expected =
            "<|begin_of_text|>"
            "<|start_header_id|>user<|end_header_id|>\n\n"
            "hello<|eot_id|>"
            "<|start_header_id|>assistant<|end_header_id|>\n\n";
        if (out) expect_eq(expected, *out, "content trim applied", r);
    }

    // Single user message, gen=true
    {
        std::vector<ChatMessage> one = {{Role::User, "hi"}};
        auto out = format_chat(ChatTemplate::Llama3, one, true);
        const std::string expected =
            "<|begin_of_text|>"
            "<|start_header_id|>user<|end_header_id|>\n\n"
            "hi<|eot_id|>"
            "<|start_header_id|>assistant<|end_header_id|>\n\n";
        if (out) expect_eq(expected, *out, "single user msg byte-exact", r);
    }
}

// ─── Qwen25 ─────────────────────────────────────────────────────────────────
void test_qwen25(TestResults& r) {
    std::cout << "\n--- Qwen25 ---\n";

    // Two-msg, gen=true (138 bytes)
    {
        auto out = format_chat(ChatTemplate::Qwen25, two_msg(), true);
        const std::string expected =
            "<|im_start|>system\n"
            "You are a helpful assistant.<|im_end|>\n"
            "<|im_start|>user\n"
            "What is the capital of France?<|im_end|>\n"
            "<|im_start|>assistant\n";
        print_test(out.has_value(), "two-msg returns success", r);
        if (out) expect_eq(expected, *out, "two-msg byte-exact", r);
    }

    // Two-msg, gen=false
    {
        auto out = format_chat(ChatTemplate::Qwen25, two_msg(), false);
        const std::string expected =
            "<|im_start|>system\n"
            "You are a helpful assistant.<|im_end|>\n"
            "<|im_start|>user\n"
            "What is the capital of France?<|im_end|>\n";
        if (out) expect_eq(expected, *out, "gen=false omits assistant suffix", r);
    }

    // Multi-turn, gen=true (177 bytes)
    {
        auto out = format_chat(ChatTemplate::Qwen25, multi_turn(), true);
        const std::string expected =
            "<|im_start|>system\n"
            "You are concise.<|im_end|>\n"
            "<|im_start|>user\n"
            "What's 2+2?<|im_end|>\n"
            "<|im_start|>assistant\n"
            "4<|im_end|>\n"
            "<|im_start|>user\n"
            "And 3+3?<|im_end|>\n"
            "<|im_start|>assistant\n";
        if (out) expect_eq(expected, *out, "4-msg multi-turn byte-exact", r);
    }

    // No-trim — Qwen2.5 preserves whitespace verbatim (contrast with Llama3)
    {
        std::vector<ChatMessage> ws = {{Role::User, "  hello  \n"}};
        auto out = format_chat(ChatTemplate::Qwen25, ws, true);
        const std::string expected =
            "<|im_start|>user\n"
            "  hello  \n"
            "<|im_end|>\n"
            "<|im_start|>assistant\n";
        if (out) expect_eq(expected, *out, "content NOT trimmed (verbatim)", r);
    }
}

// ─── Phi3 ───────────────────────────────────────────────────────────────────
void test_phi3(TestResults& r) {
    std::cout << "\n--- Phi3 ---\n";

    // Two-msg, gen=true (108 bytes)
    {
        auto out = format_chat(ChatTemplate::Phi3, two_msg(), true);
        const std::string expected =
            "<|system|>\n"
            "You are a helpful assistant.<|end|>\n"
            "<|user|>\n"
            "What is the capital of France?<|end|>\n"
            "<|assistant|>\n";
        print_test(out.has_value(), "two-msg returns success", r);
        if (out) expect_eq(expected, *out, "two-msg byte-exact", r);
    }

    // Two-msg, gen=false
    {
        auto out = format_chat(ChatTemplate::Phi3, two_msg(), false);
        const std::string expected =
            "<|system|>\n"
            "You are a helpful assistant.<|end|>\n"
            "<|user|>\n"
            "What is the capital of France?<|end|>\n";
        if (out) expect_eq(expected, *out, "gen=false omits assistant suffix", r);
    }

    // Multi-turn (125 bytes)
    {
        auto out = format_chat(ChatTemplate::Phi3, multi_turn(), true);
        const std::string expected =
            "<|system|>\n"
            "You are concise.<|end|>\n"
            "<|user|>\n"
            "What's 2+2?<|end|>\n"
            "<|assistant|>\n"
            "4<|end|>\n"
            "<|user|>\n"
            "And 3+3?<|end|>\n"
            "<|assistant|>\n";
        if (out) expect_eq(expected, *out, "4-msg multi-turn byte-exact", r);
    }

    // No BOS in Phi-3 output (caller's tokenizer adds it via add_special=true)
    {
        auto out = format_chat(ChatTemplate::Phi3, two_msg(), true);
        if (out) {
            bool has_bos = out->find("<s>") != std::string::npos
                        || out->find("<|begin_of_text|>") != std::string::npos;
            print_test(!has_bos,
                       "no BOS in formatter output (tokenizer adds it)", r);
        }
    }
}

// ─── Raw ────────────────────────────────────────────────────────────────────
void test_raw(TestResults& r) {
    std::cout << "\n--- Raw ---\n";

    // Two-msg — newline-joined, no markers
    {
        auto out = format_chat(ChatTemplate::Raw, two_msg(), true);
        const std::string expected =
            "You are a helpful assistant.\n"
            "What is the capital of France?";
        print_test(out.has_value(), "two-msg returns success", r);
        if (out) expect_eq(expected, *out, "newline-joined contents", r);
    }

    // add_generation_prompt is ignored for Raw — same output either way
    {
        auto with_gen    = format_chat(ChatTemplate::Raw, two_msg(), true);
        auto without_gen = format_chat(ChatTemplate::Raw, two_msg(), false);
        if (with_gen && without_gen) {
            print_test(*with_gen == *without_gen,
                       "add_generation_prompt ignored", r);
        }
    }

    // Single message — just the content, no separator
    {
        std::vector<ChatMessage> one = {{Role::User, "hi"}};
        auto out = format_chat(ChatTemplate::Raw, one, true);
        if (out) expect_eq("hi", *out, "single msg = bare content", r);
    }
}

// ─── Errors ─────────────────────────────────────────────────────────────────
void test_errors(TestResults& r) {
    std::cout << "\n--- Errors ---\n";

    for (auto t : {ChatTemplate::Llama3, ChatTemplate::Qwen25,
                   ChatTemplate::Phi3,   ChatTemplate::Raw}) {
        auto out = format_chat(t, {}, true);
        bool ok = !out.has_value()
               && out.error().kind == LLMErrorKind::InvalidRequest;
        std::string label = "empty messages -> InvalidRequest";
        print_test(ok, label, r);
    }
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Chat Template Test Suite                                \n";
    std::cout << "============================================================\n";

    TestResults r;
    test_llama3(r);
    test_qwen25(r);
    test_phi3(r);
    test_raw(r);
    test_errors(r);

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << r.passed << "\n";
    std::cout << "Failed: " << r.failed << "\n\n";

    if (r.failed == 0) {
        std::cout << "[SUCCESS] All chat template tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}
