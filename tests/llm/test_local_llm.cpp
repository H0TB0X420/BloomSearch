// Constructor-edge tests for LocalLLMBackend.
//
// Real-model tests (loading a GGUF, running complete()/complete_streaming())
// live in Block 2.5. This file only covers paths that fail before any model
// bytes are read.
#include "llm/local_llm.h"

#include <cassert>
#include <iostream>
#include <string>

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

// ── Test 1: empty model_path → InvalidRequest ──────────────────────────────
void test_empty_path(TestResults& r) {
    std::cout << "\n--- Empty model_path ---\n";

    LocalLLMConfig cfg;
    cfg.model_path = "";
    cfg.chat_template = ChatTemplate::Llama3;

    auto result = LocalLLMBackend::create(cfg);
    print_test(!result.has_value(), "create returns unexpected", r);
    if (!result) {
        print_test(result.error().kind == LLMErrorKind::InvalidRequest,
                   "kind=InvalidRequest", r);
        print_test(!result.error().message.empty(),
                   "error message non-empty", r);
    }
}

// ── Test 2: nonexistent model file → BackendUnavailable ────────────────────
void test_nonexistent_path(TestResults& r) {
    std::cout << "\n--- Nonexistent model file ---\n";

    LocalLLMConfig cfg;
    cfg.model_path = "/tmp/this_file_does_not_exist_bloom_test_xyzzy.gguf";
    cfg.chat_template = ChatTemplate::Qwen25;

    auto result = LocalLLMBackend::create(cfg);
    print_test(!result.has_value(), "create returns unexpected", r);
    if (!result) {
        print_test(result.error().kind == LLMErrorKind::BackendUnavailable,
                   "kind=BackendUnavailable", r);
        print_test(result.error().message.find("not found") != std::string::npos,
                   "message mentions 'not found'", r);
    }
}

// ── Test 3: zero n_ctx → InvalidRequest ────────────────────────────────────
void test_zero_n_ctx(TestResults& r) {
    std::cout << "\n--- n_ctx == 0 ---\n";

    LocalLLMConfig cfg;
    // Use a path that *exists* so we get past the file-existence check; the
    // n_ctx validation should fire before any model loading happens.
    cfg.model_path = "/etc/hostname";  // exists on Linux containers
    cfg.chat_template = ChatTemplate::Phi3;
    cfg.n_ctx = 0;

    auto result = LocalLLMBackend::create(cfg);
    print_test(!result.has_value(), "create returns unexpected", r);
    if (!result) {
        // /etc/hostname won't load as a GGUF, but n_ctx==0 should reject
        // BEFORE we get to llama_model_load_from_file. So we expect
        // InvalidRequest (n_ctx) not Internal (load failed).
        print_test(result.error().kind == LLMErrorKind::InvalidRequest,
                   "kind=InvalidRequest (validation before load)", r);
    }
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    LocalLLMBackend Constructor-Edge Test Suite             \n";
    std::cout << "============================================================\n";

    TestResults r;
    test_empty_path(r);
    test_nonexistent_path(r);
    test_zero_n_ctx(r);

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << r.passed << "\n";
    std::cout << "Failed: " << r.failed << "\n\n";

    if (r.failed == 0) {
        std::cout << "[SUCCESS] All LocalLLMBackend constructor-edge tests passed!\n";
        std::cout << "(complete()/complete_streaming() with a real model arrive in Block 2.5.)\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}
