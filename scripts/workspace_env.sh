#!/usr/bin/env bash
# Source this file from a shell: . scripts/workspace_env.sh
P4_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
P4_WORKSPACE_ROOT="$(dirname "$P4_REPO_ROOT")"
export IDF_PATH="${P4_IDF_PATH:-$P4_WORKSPACE_ROOT/esp-idf}"
export IDF_TOOLS_PATH="${P4_IDF_TOOLS_PATH:-$P4_WORKSPACE_ROOT/idf-tools}"
export PYTHONPATH="$P4_WORKSPACE_ROOT/build-tools${PYTHONPATH:+:$PYTHONPATH}"
export PATH="$P4_WORKSPACE_ROOT/build-tools/bin:$PATH"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ESP-IDF missing. Run scripts/setup_workspace.sh first." >&2
    return 1
fi
. "$IDF_PATH/export.sh"
