// Standalone tests for utf8_complete_prefix_end. No model required.
#include "llm/utf8.h"

#include <iostream>
#include <string>
#include <string_view>

using namespace search::llm;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

static void expect(std::size_t expected,
                   std::string_view input,
                   const std::string& name,
                   TestResults& r) {
    std::size_t actual = utf8_complete_prefix_end(input);
    bool ok = (actual == expected);
    if (ok) {
        std::cout << "  [PASS] " << name
                  << " (=" << actual << ")\n";
        r.passed++;
    } else {
        std::cout << "  [FAIL] " << name
                  << "  expected " << expected << ", got " << actual << "\n";
        r.failed++;
    }
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    UTF-8 Prefix-End Test Suite                             \n";
    std::cout << "============================================================\n\n";

    TestResults r;

    std::cout << "--- Trivial ---\n";
    expect(0, "",     "empty",                 r);
    expect(1, "a",    "single ASCII",          r);
    expect(3, "abc",  "ASCII-only",            r);

    std::cout << "\n--- 2-byte codepoints (e-acute = 0xC3 0xA9) ---\n";
    expect(2, "\xC3\xA9",      "complete 2-byte",                  r);
    expect(0, "\xC3",          "partial 2-byte (leading only)",    r);
    expect(3, "a\xC3\xA9",     "ASCII + complete 2-byte",          r);
    expect(1, "a\xC3",         "ASCII + partial 2-byte (leading)", r);

    std::cout << "\n--- 3-byte codepoints (euro sign = 0xE2 0x82 0xAC) ---\n";
    expect(3, "\xE2\x82\xAC",  "complete 3-byte",                  r);
    expect(0, "\xE2",          "partial 3-byte (1 of 3)",          r);
    expect(0, "\xE2\x82",      "partial 3-byte (2 of 3)",          r);
    expect(4, "a\xE2\x82\xAC", "ASCII + complete 3-byte",          r);
    expect(1, "a\xE2",         "ASCII + partial 3-byte (1 of 3)",  r);
    expect(1, "a\xE2\x82",     "ASCII + partial 3-byte (2 of 3)",  r);

    std::cout << "\n--- 4-byte codepoints (crab emoji 0xF0 0x9F 0xA6 0x80) ---\n";
    expect(4, "\xF0\x9F\xA6\x80",      "complete 4-byte (crab)",          r);
    expect(0, "\xF0",                  "partial 4-byte (1 of 4)",         r);
    expect(0, "\xF0\x9F",              "partial 4-byte (2 of 4)",         r);
    expect(0, "\xF0\x9F\xA6",          "partial 4-byte (3 of 4)",         r);
    expect(7, "abc\xF0\x9F\xA6\x80",   "ASCII + complete 4-byte",         r);
    expect(3, "abc\xF0",               "ASCII + partial 4-byte (1 of 4)", r);
    expect(3, "abc\xF0\x9F",           "ASCII + partial 4-byte (2 of 4)", r);
    expect(3, "abc\xF0\x9F\xA6",       "ASCII + partial 4-byte (3 of 4)", r);

    std::cout << "\n--- Mixed text 'Hello, 世界!' (CJK = 3-byte each) ---\n";
    // "Hello, " = 7 bytes; 世 = 0xE4 0xB8 0x96; 界 = 0xE7 0x95 0x8C; ! = 1
    // Total: 7 + 3 + 3 + 1 = 14
    expect(14, "Hello, \xE4\xB8\x96\xE7\x95\x8C!", "all complete",        r);
    expect(7,  "Hello, \xE4",                       "split mid-1st-CJK 1", r);
    expect(7,  "Hello, \xE4\xB8",                   "split mid-1st-CJK 2", r);
    expect(10, "Hello, \xE4\xB8\x96",               "1st CJK complete",    r);
    expect(10, "Hello, \xE4\xB8\x96\xE7",           "1st done + 2nd 1of3", r);
    expect(10, "Hello, \xE4\xB8\x96\xE7\x95",       "1st done + 2nd 2of3", r);

    std::cout << "\n--- Malformed inputs (defensive degradation) ---\n";
    // Lone continuation byte: after 1 walk, all-continuation fallback
    // drops 1 byte → returns 0.
    expect(0, "\x80",           "lone continuation byte",     r);
    // Trailing orphan after ASCII: 'c' is 1-byte complete at i=2,
    // walked=2 > total=1, return i+total = 3 (drop the orphan).
    expect(3, "abc\x80",        "ASCII + orphan continuation", r);
    // Four consecutive continuation bytes — pathological. All-continuation
    // fallback returns n - max_walk = 0.
    expect(0, "\x80\x80\x80\x80", "4x orphan continuation",    r);

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << r.passed << "\n";
    std::cout << "Failed: " << r.failed << "\n\n";

    if (r.failed == 0) {
        std::cout << "[SUCCESS] All UTF-8 tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}
