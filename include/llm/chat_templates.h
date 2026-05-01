#pragma once

#include "llm/llm_backend.h"

#include <expected>
#include <string>
#include <vector>

namespace search::llm {

// Format a list of chat messages into the byte-exact prompt string that the
// named model family was trained to recognize.
//
// `add_generation_prompt`: when true, append the assistant's start-of-turn
// marker so the model knows it's their turn to respond. Set false when
// formatting for echo-back, training-data shape, or non-inference contexts.
//
// Returns InvalidRequest if `messages` is empty.
//
// Reference patterns (canonical sources of byte-exact truth):
//   ChatTemplate::Llama3 — Meta-Llama-3-Instruct tokenizer.json chat_template
//   ChatTemplate::Qwen25 — Qwen/Qwen2.5-Instruct  tokenizer.json chat_template
//   ChatTemplate::Phi3   — microsoft/Phi-3-mini-4k-instruct chat_template
//   ChatTemplate::Raw    — newline-joined contents only; no role markers,
//                          no BOS, no EOT. For families we don't ship a hand-
//                          rolled template for. LocalLLMBackend (Block 2.3)
//                          may route Raw through llama_chat_apply_template,
//                          which reads the GGUF's bundled template metadata.
//
// BOS handling:
//   - Llama-3's official template embeds `<|begin_of_text|>` at the start;
//     this formatter includes it. Caller must tokenize with add_special=false
//     to avoid a double-BOS.
//   - Qwen2.5 and Phi-3 official templates do NOT include BOS; this formatter
//     omits it. Caller should tokenize with add_special=true so the tokenizer
//     adds BOS as needed.
//   - Raw includes no BOS.
std::expected<std::string, LLMError>
format_chat(ChatTemplate tmpl,
            const std::vector<ChatMessage>& messages,
            bool add_generation_prompt);

}  // namespace search::llm
