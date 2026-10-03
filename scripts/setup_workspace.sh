#!/usr/bin/env bash
# Install the pinned SDK and tools beside the repository, without apt or Docker.
set -euo pipefail
P4_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
P4_WORKSPACE_ROOT="$(dirname "$P4_REPO_ROOT")"
export IDF_PATH="${P4_IDF_PATH:-$P4_WORKSPACE_ROOT/esp-idf}"
export IDF_TOOLS_PATH="${P4_IDF_TOOLS_PATH:-$P4_WORKSPACE_ROOT/idf-tools}"
python3 -m pip install --upgrade --target "$P4_WORKSPACE_ROOT/build-tools" \
    'cmake>=3.24,<4' ninja virtualenv
export PYTHONPATH="$P4_WORKSPACE_ROOT/build-tools${PYTHONPATH:+:$PYTHONPATH}"
export PATH="$P4_WORKSPACE_ROOT/build-tools/bin:$PATH"
if [[ ! -d "$IDF_PATH" ]]; then
    git clone --depth 1 --branch v5.5.3 --recursive --shallow-submodules --jobs 8 \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi
if [[ "$(git -C "$IDF_PATH" rev-parse HEAD)" != 2c211b236707889e8400c4dc5644dd5c4ee071e0 ]]; then
    echo "Expected ESP-IDF v5.5.3; use a separate P4_IDF_PATH for this setup." >&2
    exit 1
fi
git -C "$IDF_PATH" submodule update --init --recursive --depth 1 --jobs 8
git -C "$P4_REPO_ROOT" submodule update --init --depth 1
P4_PYTHON_VERSION="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
P4_PYTHON_ENV="$IDF_TOOLS_PATH/python_env/idf5.5_py${P4_PYTHON_VERSION}_env"
if [[ ! -x "$P4_PYTHON_ENV/bin/python" ]]; then
    python3 -m virtualenv "$P4_PYTHON_ENV"
fi
"$IDF_PATH/install.sh" esp32p4,esp32
# Pin the dependency manager and apply Linux parent-PID compatibility.
"$P4_PYTHON_ENV/bin/python" -m pip install 'idf-component-manager==2.4.11'
"$P4_PYTHON_ENV/bin/python" "$P4_REPO_ROOT/scripts/fix_workspace_pid.py"
echo "Ready: source scripts/workspace_env.sh"
