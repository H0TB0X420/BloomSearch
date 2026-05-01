#include "llm/chat_templates.h"

#include <cctype>
#include <string_view>

namespace search::llm {

namespace {

// Trim leading and trailing whitespace. Llama-3's official Jinja template
// applies `| trim` to message content; the other templates do not.
std::string_view strip(std::string_view s) noexcept {
    size_t lo = 0;
    while (lo < s.size() &&
           std::isspace(static_cast<unsigned char>(s[lo]))) {
        ++lo;
    }
    size_t hi = s.size();
    while (hi > lo &&
           std::isspace(static_cast<unsigned char>(s[hi - 1]))) {
        --hi;
    }
    return s.substr(lo, hi - lo);
}

// ── Llama-3 ─────────────────────────────────────────────────────────────────
// Reference: meta-llama/Meta-Llama-3-Instruct tokenizer.json chat_template:
//
//   {{ bos_token }}{% for message in messages %}
//     {{ '<|start_header_id|>' + message['role'] + '<|end_header_id|>\n\n'
//        + message['content'] | trim + '<|eot_id|>' }}
//   {% endfor %}
//   {% if add_generation_prompt %}
//     {{ '<|start_header_id|>assistant<|end_header_id|>\n\n' }}
//   {% endif %}
//
// `bos_token` for Llama-3 = "<|begin_of_text|>".
std::string format_llama3(const std::vector<ChatMessage>& msgs, bool add_gen) {
    std::string out;
    out.reserve(64 + msgs.size() * 64);
    out += "<|begin_of_text|>";
    for (const auto& m : msgs) {
        out += "<|start_header_id|>";
        out += role_name(m.role);
        out += "<|end_header_id|>\n\n";
        auto trimmed = strip(m.content);
        out.append(trimmed.data(), trimmed.size());
        out += "<|eot_id|>";
    }
    if (add_gen) {
        out += "<|start_header_id|>assistant<|end_header_id|>\n\n";
    }
    return out;
}

// ── Qwen2.5 ─────────────────────────────────────────────────────────────────
// Reference: Qwen/Qwen2.5-Instruct tokenizer.json chat_template (simplified):
//
//   {% for message in messages %}
//     {{ '<|im_start|>' + message['role'] + '\n' + message['content']
//        + '<|im_end|>' + '\n' }}
//   {% endfor %}
//   {% if add_generation_prompt %}
//     {{ '<|im_start|>assistant\n' }}
//   {% endif %}
//
// No BOS embedded. Qwen2.5's published template auto-prepends a default system
// message if the messages list lacks one — we do NOT replicate that. Caller
// supplies the exact messages they want sent.
std::string format_qwen25(const std::vector<ChatMessage>& msgs, bool add_gen) {
    std::string out;
    out.reserve(32 + msgs.size() * 64);
    for (const auto& m : msgs) {
        out += "<|im_start|>";
        out += role_name(m.role);
        out += '\n';
        out += m.content;
        out += "<|im_end|>\n";
    }
    if (add_gen) {
        out += "<|im_start|>assistant\n";
    }
    return out;
}

// ── Phi-3 ───────────────────────────────────────────────────────────────────
// Reference: microsoft/Phi-3-mini-4k-instruct tokenizer.json chat_template:
//
//   {% for message in messages %}
//     {% if   message['role'] == 'system'    %}{{'<|system|>\n'    + ...}}
//     {% elif message['role'] == 'user'      %}{{'<|user|>\n'      + ...}}
//     {% elif message['role'] == 'assistant' %}{{'<|assistant|>\n' + ...}}
//     {% endif %}
//   {% endfor %}
//   {% if add_generation_prompt %}{{ '<|assistant|>\n' }}{% endif %}
//
// Each role's `<|...|>` literal matches role_name() exactly, so we can build
// the marker generically rather than branching by role.
//
// No BOS in template. Tokenizer's add_special=true handles BOS injection.
std::string format_phi3(const std::vector<ChatMessage>& msgs, bool add_gen) {
    std::string out;
    out.reserve(32 + msgs.size() * 48);
    for (const auto& m : msgs) {
        out += "<|";
        out += role_name(m.role);
        out += "|>\n";
        out += m.content;
        out += "<|end|>\n";
    }
    if (add_gen) {
        out += "<|assistant|>\n";
    }
    return out;
}

// ── Raw ─────────────────────────────────────────────────────────────────────
// Newline-joined contents. No role markers, no BOS, no EOT. Useful as a
// pre-formatted-prompt path or as a placeholder before LocalLLMBackend wires
// Raw through llama.cpp's built-in llama_chat_apply_template().
std::string format_raw(const std::vector<ChatMessage>& msgs, bool /*add_gen*/) {
    std::string out;
    out.reserve(msgs.size() * 32);
    for (size_t i = 0; i < msgs.size(); ++i) {
        if (i > 0) out += '\n';
        out += msgs[i].content;
    }
    return out;
}

}  // namespace

std::expected<std::string, LLMError>
format_chat(ChatTemplate tmpl,
            const std::vector<ChatMessage>& messages,
            bool add_generation_prompt) {
    if (messages.empty()) {
        return std::unexpected(
            LLMError{LLMErrorKind::InvalidRequest,
                     "format_chat: messages must not be empty"});
    }
    switch (tmpl) {
        case ChatTemplate::Llama3:
            return format_llama3(messages, add_generation_prompt);
        case ChatTemplate::Qwen25:
            return format_qwen25(messages, add_generation_prompt);
        case ChatTemplate::Phi3:
            return format_phi3(messages, add_generation_prompt);
        case ChatTemplate::Raw:
            return format_raw(messages, add_generation_prompt);
    }
    return std::unexpected(
        LLMError{LLMErrorKind::Internal,
                 "format_chat: unhandled ChatTemplate enum value"});
}

}  // namespace search::llm
