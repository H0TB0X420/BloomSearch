#!/usr/bin/env bash
# Downloads a small GGUF model for LocalLLMBackend smoke tests.
#
# Default: Qwen2.5-0.5B-Instruct Q4_K_M (~400 MB) — small enough that CPU
# inference is bearable on the old MacBook Pro reference machine, and ships
# the canonical Qwen2.5 chat template that our hand-rolled formatter targets.
#
# Target dir: ./models/ (gitignored). Skips download if file already present.
#
# Usage:
#   ./scripts/fetch_test_model.sh               # default Qwen2.5-0.5B
#   ./scripts/fetch_test_model.sh <other-url>   # custom URL
#
# After it completes, paste the printed `export LLM_TEST_MODEL=...` line.

set -euo pipefail

DEFAULT_URL="https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf"
URL="${1:-$DEFAULT_URL}"

# Resolve project root from this script's location.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODELS_DIR="${PROJECT_ROOT}/models"

mkdir -p "${MODELS_DIR}"

FILENAME="$(basename "${URL%%\?*}")"
TARGET="${MODELS_DIR}/${FILENAME}"

if [[ -f "${TARGET}" ]]; then
    SIZE=$(du -h "${TARGET}" | cut -f1)
    echo "Already present: ${TARGET}  (${SIZE})"
else
    echo "Downloading: ${URL}"
    echo "Target:      ${TARGET}"
    echo
    if ! curl --fail --location --progress-bar -o "${TARGET}.partial" "${URL}"; then
        rm -f "${TARGET}.partial"
        echo
        echo "Download failed. If you're behind a proxy or HF is blocked, you can:"
        echo "  1. Download the file manually from your browser:"
        echo "     ${URL}"
        echo "  2. Save it to: ${TARGET}"
        echo "  3. Re-run this script (it will detect the existing file)."
        exit 1
    fi
    mv "${TARGET}.partial" "${TARGET}"
    SIZE=$(du -h "${TARGET}" | cut -f1)
    echo
    echo "Downloaded: ${TARGET}  (${SIZE})"
fi

echo
echo "To run the smoke tests, paste:"
echo
echo "  export LLM_TEST_MODEL=${TARGET}"
echo
echo "Then build and run inside Docker:"
echo
echo "  docker-compose exec dev bash -c \"cd /app/build && ninja test_local_llm_smoke && LLM_TEST_MODEL=${TARGET} ./test_local_llm_smoke\""
echo
