#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace search::llm {

// Returns the largest k such that s[0..k] is complete valid UTF-8 — i.e.,
// every codepoint in s[0..k] has all of its bytes present.
//
// Used by the streaming generation loop to decide how much of an internal
// byte buffer can safely be emitted to a callback. BPE tokens often split
// multi-byte codepoints (e.g., a token's bytes might be the first 2 bytes
// of a 4-byte emoji); without this clamp, a streaming consumer would see
// invalid UTF-8 mid-glyph.
//
// Behavior on malformed UTF-8 is conservative: orphan continuation bytes
// or 5+ consecutive continuation bytes cause the function to drop the
// suspect tail rather than emit broken bytes. The dropped bytes are simply
// re-evaluated on the next call (this function is idempotent on its
// already-emitted prefix).
//
// Header-only inline implementation — no .cpp dependency. Pure function,
// no allocations, noexcept.
inline std::size_t utf8_complete_prefix_end(std::string_view s) noexcept {
    if (s.empty()) return 0;

    const std::size_t n = s.size();
    // No UTF-8 codepoint exceeds 4 bytes, so the start of the last
    // codepoint (if it exists) is within the trailing 4 bytes.
    const std::size_t max_walk = std::min<std::size_t>(4, n);

    for (std::size_t walk = 1; walk <= max_walk; ++walk) {
        const std::size_t i = n - walk;
        const unsigned char b = static_cast<unsigned char>(s[i]);

        // Continuation byte (10xxxxxx) — keep walking back to find the leader.
        if ((b & 0xC0) == 0x80) continue;

        // Non-continuation byte at position i. Determine codepoint width.
        int total = 1;
        if      ((b & 0x80) == 0x00) total = 1;  // 0xxxxxxx — ASCII
        else if ((b & 0xE0) == 0xC0) total = 2;  // 110xxxxx
        else if ((b & 0xF0) == 0xE0) total = 3;  // 1110xxxx
        else if ((b & 0xF8) == 0xF0) total = 4;  // 11110xxx
        // else: malformed leading byte (0xFE/0xFF or stray 11111xxx) —
        // treat as a 1-byte unit so we can move past it.

        const std::size_t walked = walk;
        if (walked == static_cast<std::size_t>(total)) {
            return n;  // last codepoint occupies exactly the trailing bytes
        }
        if (walked < static_cast<std::size_t>(total)) {
            return i;  // last codepoint started but is incomplete
        }
        // walked > total: there are continuation bytes BEYOND where this
        // codepoint should have ended — those are orphan bytes. Emit
        // through the codepoint's true end and drop the orphans.
        return i + static_cast<std::size_t>(total);
    }

    // All trailing bytes (up to 4) were continuation bytes — malformed.
    // Drop the suspect tail; the next call may have more context.
    return n - max_walk;
}

}  // namespace search::llm
